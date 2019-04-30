#ifndef __ZMQ_BIP15X_DATACONNECTION_H__
#define __ZMQ_BIP15X_DATACONNECTION_H__

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <spdlog/spdlog.h>
#include <chrono>

#include "ActiveStreamClient.h"
#include "AuthorizedPeers.h"
//#include "ArmoryServersProvider.h"
#include "BIP150_151.h"
#include "EncryptionUtils.h"
#include "FastLock.h"
#include "MessageHolder.h"
#include "SystemFileUtils.h"
#include "ZMQ_BIP15X_DataConnection.h"
#include "ZmqDataConnection.h"
#include "ZMQ_BIP15X_Msg.h"

#define CLIENT_AUTH_PEER_FILENAME "client.peers"
#define HEARTBEAT_PACKET_SIZE 23

// DESIGN NOTES: Remote data connections must have a callback for when unknown
// server keys are seen. The callback should ask the user if they'll accept
// the new key, or otherwise properly handle new keys arriving.
//
// Cookies are used for local connections, and are the default unless remote
// callbacks are added. When the server is invoked by a binary containing a
// client connection, the binary must be invoked with the client connection's
// public BIP 150 ID key. In turn, the binary with the server connection must
// generate a cookie with its public BIP 150 ID key. The client will read the
// cookie and get the server key. This allows both sides to verify each other.
//
// When adding authorized keys to a connection, the name needs to be the
// IP:Port of the server connection. This is the only reliable information
// available to the connection that can be used to ID who's on the other side.
// It's okay to use other names in the GUI and elsewhere. However, the IP:Port
// must be used when searching for keys.
//
// The key acceptance functionality is as follows:
//
// LOCAL SIGNER
// Accept only a single key from the server cookie.
//
// REMOTE SIGNER
// New key + No callbacks - Reject the new keys.
// New key + Callbacks - Depends on what the user wants.
// Previously verified key - Accept the key and skip the callbacks.

template<class _S>
class ZmqBIP15XDataConnection : public ZmqDataConnection, public _S
{
public:
   ZmqBIP15XDataConnection(const std::shared_ptr<spdlog::logger>& logger
      , const bool& ephemeralPeers = false, const bool& monitored = false
      , const bool& genIDCookie = false) : ZmqDataConnection(logger), _S(logger)
   {
      outKeyTimePoint_ = std::chrono::steady_clock::now();
      currentReadMessage_.reset();
      std::string datadir = SystemFilePaths::appDataLocation();
      std::string filename(CLIENT_AUTH_PEER_FILENAME);

      // In general, load the server key from a special Armory wallet file.
      if (!ephemeralPeers) {
         authPeers_ = std::make_shared<AuthorizedPeers>(
            datadir, filename);
      }
      else {
         authPeers_ = std::make_shared<AuthorizedPeers>();
      }

      // Create a random four-byte ID for the client.
      msgID_ = READ_UINT32_LE(CryptoPRNG::generateRandom(4));

      // BIP 151 connection setup. Technically should be per-socket or something
      // similar but data connections will only connect to one machine at a time.
      auto lbds = getAuthPeerLambda();
      bip151Connection_ = std::make_shared<BIP151Connection>(lbds);
      if (genIDCookie) {
         genBIPIDCookie();
         bipIDCookieExists_ = true;
      }

      const auto &heartbeatProc = [this] {
         while (hbThreadRunning_) {
            {
               std::unique_lock<std::mutex> lock(hbMutex_);
               hbCondVar_.wait_for(lock, std::chrono::seconds{ 1 });
               if (!hbThreadRunning_) {
                  break;
               }
            }
            const auto curTime = std::chrono::steady_clock::now();
            const auto diff =
               std::chrono::duration_cast<std::chrono::milliseconds>(
               curTime - lastHeartbeat_);
            if (diff.count() > heartbeatInterval_) {
               triggerHeartbeat();
            }
         }
      };
      hbThreadRunning_ = true;
      lastHeartbeat_ = std::chrono::steady_clock::now();
      hbThread_ = std::thread(heartbeatProc);
   }
/*   ZmqBIP15XDataConnection(const std::shared_ptr<spdlog::logger>& logger
      , const ArmoryServersProvider& trustedServer, const bool& ephemeralPeers
      , bool monitored);*/
   ~ZmqBIP15XDataConnection() noexcept override
   {
      hbThreadRunning_ = false;
      hbCondVar_.notify_one();
      hbThread_.join();

      // If it exists, delete the identity cookie.
      if (bipIDCookieExists_) {
         const std::string absCookiePath =
            SystemFilePaths::appDataLocation() + "/" + kIDCookieName;
         if (SystemFileUtils::fileExist(absCookiePath)) {
            if (!SystemFileUtils::rmFile(absCookiePath)) {
               logger_->error("[{}] Unable to delete client identity cookie ({})."
                  , __func__, absCookiePath);
            }
         }
      }
   }

   using cbNewKey = std::function<void(const std::string&, const std::string&
      , std::shared_ptr<std::promise<bool>>)>;
   using invokeCB = std::function<void(const std::string&
      , const std::string&
      , std::shared_ptr<std::promise<bool>>
      , const cbNewKey&)>;

   ZmqBIP15XDataConnection(const ZmqBIP15XDataConnection&) = delete;
   ZmqBIP15XDataConnection& operator= (const ZmqBIP15XDataConnection&) = delete;
   ZmqBIP15XDataConnection(ZmqBIP15XDataConnection&&) = delete;
   ZmqBIP15XDataConnection& operator= (ZmqBIP15XDataConnection&&) = delete;

   bool getServerIDCookie(BinaryData& cookieBuf, const std::string& cookieName)
   {
      if (!useServerIDCookie_) {
         return false;
      }

      const std::string absCookiePath = SystemFilePaths::appDataLocation() + "/"
         + cookieName;
      if (!SystemFileUtils::fileExist(absCookiePath)) {
         logger_->error("[{}] Server identity cookie ({}) doesn't exist. Unable "
            "to verify server identity.", __func__, absCookiePath);
         return false;
      }

      // Ensure that we only read a compressed key.
      std::ifstream cookieFile(absCookiePath, std::ios::in | std::ios::binary);
      cookieFile.read(cookieBuf.getCharPtr(), BIP151PUBKEYSIZE);
      cookieFile.close();
      if (!(CryptoECDSA().VerifyPublicKeyValid(cookieBuf))) {
         logger_->error("[{}] Server identity key ({}) isn't a valid compressed "
            "key. Unable to verify server identity.", __func__
            , cookieBuf.toBinStr());
         return false;
      }

      return true;
   }

   void setCBs(const cbNewKey& inNewKeyCB)
   {
      // Set callbacks only if callbacks actually exist.
      if (inNewKeyCB) {
         cbNewKey_ = inNewKeyCB;
         useServerIDCookie_ = false;
      }
   }

   BinaryData getOwnPubKey() const
   {
      const auto pubKey = authPeers_->getOwnPublicKey();
      return BinaryData(pubKey.pubkey, pubKey.compressed
         ? BTC_ECKEY_COMPRESSED_LENGTH : BTC_ECKEY_UNCOMPRESSED_LENGTH);
   }

   bool genBIPIDCookie()
   {
      const std::string absCookiePath = SystemFilePaths::appDataLocation() + "/"
         + kIDCookieName;
      if (SystemFileUtils::fileExist(absCookiePath)) {
         if (!SystemFileUtils::rmFile(absCookiePath)) {
            logger_->error("[{}] Unable to delete client identity cookie ({}). "
               "Will not write a new cookie.", __func__, absCookiePath);
            return false;
         }
      }

      // Ensure that we only write the compressed key.
      std::ofstream cookieFile(absCookiePath, std::ios::out | std::ios::binary);
      const BinaryData ourIDKey = getOwnPubKey();
      if (ourIDKey.getSize() != BTC_ECKEY_COMPRESSED_LENGTH) {
         logger_->error("[{}] Client identity key ({}) is uncompressed. Will not "
            "write the identity cookie.", __func__, absCookiePath);
         return false;
      }

      logger_->debug("[{}] Writing a new client identity cookie ({}).", __func__
         ,  absCookiePath);
      cookieFile.write(getOwnPubKey().getCharPtr(), BTC_ECKEY_COMPRESSED_LENGTH);
      cookieFile.close();
      bipIDCookieExists_ = true;

      return true;
   }

   void addAuthPeer(const BinaryData& inKey, const std::string& inKeyName)
   {
      if (!(CryptoECDSA().VerifyPublicKeyValid(inKey))) {
         logger_->error("[{}] BIP 150 authorized key ({}) for user {} is invalid."
            , __func__,  inKey.toHexStr(), inKeyName);
         return;
      }
      authPeers_->eraseName(inKeyName);
      authPeers_->addPeer(inKey, std::vector<std::string>{ inKeyName });
   }

   // Overridden functions from ZmqDataConnection.
   bool send(const std::string& data) override
   {
      bool retVal = false;

      // If we need to rekey, do it before encrypting the data.
      rekeyIfNeeded(data.size());

      // Encrypt data here only after the BIP 150 handshake is complete, and if
      // the incoming encryption flag is true.
      std::string sendData = data;
      if (bip151Connection_->getBIP150State() == BIP150State::SUCCESS) {
         ZmqBIP15XSerializedMessage msg;
         BIP151Connection* connPtr = nullptr;
         if (bip151HandshakeCompleted_) {
            connPtr = bip151Connection_.get();
         }

         BinaryData payload(data);
         msg.construct(payload.getDataVector(), connPtr
            , ZMQ_MSGTYPE_FRAGMENTEDPACKET_HEADER, msgID_);

         // Cycle through all packets.
         while (!msg.isDone())
         {
            auto& packet = msg.getNextPacket();
            if (packet.getSize() == 0) {
               logger_->error("[ZmqBIP15XClientConnection::{}] failed to "
                  "serialize data (size {})", __func__, data.size());
               return retVal;
            }

            retVal = sendPacket(packet.toBinStr());
            if (!retVal)
            {
               logger_->error("[ZmqBIP15XServerConnection::{}] fragment send failed"
                  , __func__);
               return retVal;
            }
         }
      }

      return retVal;
   }

   bool closeConnection() override
   {
      // If a future obj is still waiting, satisfy it to prevent lockup. This
      // shouldn't happen here but it's an emergency fallback.
      if (serverPubkeyProm_ && !serverPubkeySignalled_) {
         serverPubkeyProm_->set_value(false);
         serverPubkeySignalled_ = true;
      }
      currentReadMessage_.reset();
      return ZmqDataConnection::closeConnection();
   }

protected:
   bool startBIP151Handshake(const std::function<void()> &cbCompleted)
   {
      ZmqBIP15XSerializedMessage msg;
      cbCompleted_ = cbCompleted;
      BinaryData nullPayload;

      msg.construct(nullPayload.getDataVector(), nullptr, ZMQ_MSGTYPE_AEAD_SETUP,
         0);
      auto& packet = msg.getNextPacket();
      return sendPacket(packet.toBinStr());
   }

   bool handshakeCompleted() {
      return (bip150HandshakeCompleted_ && bip151HandshakeCompleted_);
   }

   // Use to send a packet that this class has generated.
   bool sendPacket(const std::string& data)
   {
      int result = -1;
      const auto rightNow = std::chrono::steady_clock::now();

      {
         FastLock locker(lockSocket_);
         result = zmq_send(dataSocket_.get(), data.c_str(), data.size(), 0);
      }
      if (result != (int)data.size()) {
         if (logger_) {
            logger_->error("[ZmqBIP15XDataConnection::{}] {} failed to send "
               "data: {} (result={}, data size={})", __func__, connectionName_
               , zmq_strerror(zmq_errno()), result, data.size());
         }
         return false;
      }

      lastHeartbeat_ = rightNow; // Reset the heartbeat timer, as data was sent.
      return true;
   }

   // Overridden functions from ZmqDataConnection.
   void onRawDataReceived(const std::string& rawData) override
   {
      BinaryData payload(rawData);

      // If decryption "failed" due to fragmentation, put the pieces together.
      // (Unlikely but we need to plan for it.)
      if (leftOverData_.getSize() != 0)
      {
         leftOverData_.append(payload);
         payload = std::move(leftOverData_);
         leftOverData_.clear();
      }

      // Perform decryption if we're ready.
      if (bip151Connection_->connectionComplete())
      {
         auto result = bip151Connection_->decryptPacket(
            payload.getPtr(), payload.getSize(),
            payload.getPtr(), payload.getSize());

         // Failure isn't necessarily a problem if we're dealing with fragments.
         if (result != 0)
         {
            // If decryption "fails" but the result indicates fragmentation, save
            // the fragment and wait before doing anything, otherwise treat it as a
            // legit error.
            if (result <= ZMQ_MESSAGE_PACKET_SIZE && result > -1)
            {
               leftOverData_ = std::move(payload);
               return;
            }
            else
            {
               logger_->error("[{}] Packet decryption failed - Error {}", __func__
                  , result);
               return;
            }
         }

         payload.resize(payload.getSize() - POLY1305MACLEN);
      }

      ProcessIncomingData(payload);
   }

   void notifyOnConnected() override
   {
      startBIP151Handshake([this] {
         ZmqDataConnection::notifyOnConnected();
      });
   }

   bool recvData() override
   {
      MessageHolder data;

      int result = zmq_msg_recv(&data, dataSocket_.get(), ZMQ_DONTWAIT);
      if (result == -1) {
         if (logger_) {
            logger_->error("[ZmqBIP15XDataConnection::{}] {} failed to recv data "
               "frame from stream: {}" , __func__, connectionName_
               , zmq_strerror(zmq_errno()));
         }
         return false;
      }

      // Process the raw data.
      onRawDataReceived(data.ToString());
      return true;
   }

   void triggerHeartbeat()
   {
      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
         logger_->error("[ZmqBIP15XDataConnection::{}] {} invalid state: {}"
            , __func__, connectionName_, (int)bip151Connection_->getBIP150State());
         return;
      }
      BIP151Connection* connPtr = nullptr;
      if (bip151HandshakeCompleted_) {
         connPtr = bip151Connection_.get();
      }

      // If a rekey is needed, rekey before encrypting. Estimate the size of the
      // final packet first in order to get the # of bytes transmitted.
      rekeyIfNeeded(HEARTBEAT_PACKET_SIZE);

      ZmqBIP15XSerializedMessage msg;
      BinaryData emptyPayload;
      msg.construct(emptyPayload.getDataVector(), connPtr, ZMQ_MSGTYPE_HEARTBEAT
         , msgID_);
      auto& packet = msg.getNextPacket();

      // An error message is already logged elsewhere if the send fails.
      if (sendPacket(packet.toBinStr()), false) {
         lastHeartbeat_ = std::chrono::steady_clock::now();
      }
   }

private:
   void ProcessIncomingData(BinaryData& payload)
   {
      // Deserialize packet.
      auto payloadRef = currentReadMessage_.insertDataAndGetRef(payload);
      auto result = currentReadMessage_.message_.parsePacket(payloadRef);
      if (!result) {
         if (logger_) {
            logger_->error("[ZmqBIP15XDataConnection::{}] Deserialization failed "
               "(connection {})", __func__, connectionName_);
         }

         currentReadMessage_.reset();
         notifyOnError(DataConnectionListener::SerializationFailed);
         return;
      }

      // Fragmented messages may not be marked as fragmented when decrypted but may
      // still be a fragment. That's fine. Just wait for the other fragments.
      if (!currentReadMessage_.message_.isReady())
      {
         return;
      }

      // If we're still handshaking, take the next step. (No fragments allowed.)
      if (currentReadMessage_.message_.getType() > ZMQ_MSGTYPE_AEAD_THRESHOLD)
      {
         if (!processAEADHandshake(currentReadMessage_.message_)) {
            if (logger_) {
               logger_->error("[ZmqBIP15XDataConnection::{}] Handshake failed "
                  "(connection {})", __func__, connectionName_);
            }

            notifyOnError(DataConnectionListener::HandshakeFailed);
            return;
         }

         currentReadMessage_.reset();
         return;
      }

      // We can now safely obtain the full message.
      BinaryData inMsg;
      currentReadMessage_.message_.getMessage(&inMsg);

      // We shouldn't get here but just in case....
      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
         if (logger_) {
            logger_->error("[ZmqBIP15XDataConnection::{}] Encryption handshake "
               "is incomplete (connection {})", __func__, connectionName_);
         }
         notifyOnError(DataConnectionListener::HandshakeFailed);
         return;
      }

      // For now, ignore the BIP message ID. If we need callbacks later, we can go
      // back to what's in Armory and add support based off that.
   /*   auto& msgid = currentReadMessage_.message_.getId();
      switch (msgid)
      {
      case ZMQ_CALLBACK_ID:
      {
         break;
      }

      default:
         break;
      }*/

      // Pass the final data up the chain.
      ZmqDataConnection::notifyOnData(inMsg.toBinStr());
      currentReadMessage_.reset();
   }

   bool processAEADHandshake(const ZmqBIP15XMsgPartial& msgObj)
   {
      // Function used to send data out on the wire.
      auto writeData = [this](BinaryData& payload, uint8_t type, bool encrypt) {
         ZmqBIP15XSerializedMessage msg;
         BIP151Connection* connPtr = nullptr;
         if (encrypt) {
            connPtr = bip151Connection_.get();
         }

         msg.construct(payload.getDataVector(), connPtr, type, 0);
         auto& packet = msg.getNextPacket();
         sendPacket(packet.toBinStr());
      };

      // Read the message, get the type, and process as needed. Code mostly copied
      // from Armory.
      auto msgbdr = msgObj.getSingleBinaryMessage();
      switch (msgObj.getType()) {
      case ZMQ_MSGTYPE_AEAD_PRESENT_PUBKEY:
      {
         /*packet is server's pubkey, do we have it?*/

         //init server promise
         serverPubkeyProm_ = std::make_shared<std::promise<bool>>();

         // If it's a local connection, get a cookie with the server's key.
         if (useServerIDCookie_) {
            // Read the cookie with the key to check.
            BinaryData cookieKey(static_cast<size_t>(BTC_ECKEY_COMPRESSED_LENGTH));
            if (!getServerIDCookie(cookieKey, kServerCookieName)) {
               return false;
            }
            else {
               // Add the host and the key to the list of verified peers. Be sure
               // to erase any old keys first.
               std::vector<std::string> keyName;
               std::string localAddrV4 = hostAddr_ + ":" + hostPort_;
               keyName.push_back(localAddrV4);
               authPeers_->eraseName(localAddrV4);
               authPeers_->addPeer(cookieKey, keyName);
            }
         }

         //compute server name
         std::stringstream ss;
         ss << hostAddr_ << ":" << hostPort_;

         // If we don't have the key already, we may ask the the user if they wish
         // to continue. (Remote signer only.)
         if (!bip151Connection_->havePublicKey(msgbdr, ss.str())) {
            //we don't have this key, call user prompt lambda
            verifyNewIDKey(msgbdr, ss.str());
         }
         else {
            //set server key promise
            if (serverPubkeyProm_ && !serverPubkeySignalled_) {
               serverPubkeyProm_->set_value(true);
               serverPubkeySignalled_ = true;
            }
            else {
               logger_->warn("[processHandshake] server public key was already set");
            }
         }

         break;
      }

      case ZMQ_MSGTYPE_AEAD_ENCINIT:
      {
         if (bip151Connection_->processEncinit(msgbdr.getPtr(), msgbdr.getSize()
            , false) != 0) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCINIT not processed");
            return false;
         }

         //valid encinit, send client side encack
         BinaryData encackPayload(BIP151PUBKEYSIZE);
         if (bip151Connection_->getEncackData(encackPayload.getPtr()
            , BIP151PUBKEYSIZE) != 0) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCACK data not obtained");
            return false;
         }

         writeData(encackPayload, ZMQ_MSGTYPE_AEAD_ENCACK, false);

         //start client side encinit
         BinaryData encinitPayload(ENCINITMSGSIZE);
         if (bip151Connection_->getEncinitData(encinitPayload.getPtr()
            , ENCINITMSGSIZE, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCINIT data not obtained");
            return false;
         }

         writeData(encinitPayload, ZMQ_MSGTYPE_AEAD_ENCINIT, false);

         break;
      }

      case ZMQ_MSGTYPE_AEAD_ENCACK:
      {
         if (bip151Connection_->processEncack(msgbdr.getPtr(), msgbdr.getSize()
            , true) == -1) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCACK not processed");
            return false;
         }

         // Do we need to check the server's ID key?
         if (serverPubkeyProm_ != nullptr) {
            //if so, wait on the promise
            auto fut = serverPubkeyProm_->get_future();
            fut.wait();

            if (fut.get()) {
               serverPubkeyProm_.reset();
               serverPubkeySignalled_ = false;
            }
            else {
               logger_->error("[processHandshake] BIP 150/151 handshake process "
                  "failed - AEAD_ENCACK - Server public key not verified");
               return false;
            }
         }

         //bip151 handshake completed, time for bip150
         std::stringstream ss;
         ss << hostAddr_ << ":" << hostPort_;

         BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
         if (bip151Connection_->getAuthchallengeData(
            authchallengeBuf.getPtr(), authchallengeBuf.getSize(), ss.str()
            , true //true: auth challenge step #1 of 6
            , false) != 0) { //false: have not processed an auth propose yet
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_CHALLENGE data not obtained");
            return false;
         }

         writeData(authchallengeBuf, ZMQ_MSGTYPE_AUTH_CHALLENGE, true);
         bip151HandshakeCompleted_ = true;
         break;
      }

      case ZMQ_MSGTYPE_AEAD_REKEY:
      {
         // Rekey requests before auth are invalid.
         if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - Not ready to rekey");
            return false;
         }

         // If connection is already setup, we only accept rekey enack messages.
         if (bip151Connection_->processEncack(msgbdr.getPtr(), msgbdr.getSize()
            , false) == -1) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_REKEY not processed");
            return false;
         }

         ++innerRekeyCount_;
         break;
      }

      case ZMQ_MSGTYPE_AUTH_REPLY:
      {
         if (bip151Connection_->processAuthreply(msgbdr.getPtr(), msgbdr.getSize()
            , true //true: step #2 out of 6
            , false) != 0) { //false: haven't seen an auth challenge yet
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_REPLY not processed");
            return false;
         }

         BinaryData authproposeBuf(BIP151PRVKEYSIZE);
         if (bip151Connection_->getAuthproposeData(
            authproposeBuf.getPtr(),
            authproposeBuf.getSize()) != 0) {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_PROPOSE data not obtained");
            return false;
         }

         writeData(authproposeBuf, ZMQ_MSGTYPE_AUTH_PROPOSE, true);
         break;
      }

      case ZMQ_MSGTYPE_AUTH_CHALLENGE:
      {
         bool goodChallenge = true;
         auto challengeResult =
            bip151Connection_->processAuthchallenge(msgbdr.getPtr()
               , msgbdr.getSize(), false); //true: step #4 of 6

         if (challengeResult == -1) {
            //auth fail, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_CHALLENGE not processed");
            return false;
         }
         else if (challengeResult == 1) {
            goodChallenge = false;
         }

         BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
         auto validReply = bip151Connection_->getAuthreplyData(
            authreplyBuf.getPtr(), authreplyBuf.getSize()
            , false //true: step #5 of 6
            , goodChallenge);

         if (validReply != 0) {
            //auth setup failure, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_REPLY data not obtained");
            return false;
         }

         writeData(authreplyBuf, ZMQ_MSGTYPE_AUTH_REPLY, true);

         // Rekey.
         bip151Connection_->bip150HandshakeRekey();
         bip150HandshakeCompleted_ = true;
         outKeyTimePoint_ = std::chrono::steady_clock::now();
         if (cbCompleted_) {
            cbCompleted_();
         }

         break;
      }

      default:
         logger_->error("[processHandshake] Unknown message type.");
         return false;
      }

      return true;
   }

   void verifyNewIDKey(const BinaryDataRef& newKey
      , const std::string& srvAddrPort)
   {
      if (useServerIDCookie_) {
         // If we get here, it's because the cookie add failed or the cookie was
         // incorrect. Satisfy the promise to prevent lockup.
         logger_->error("[{}] Server ID key cookie could not be verified", __func__);
         if (serverPubkeyProm_ && !serverPubkeySignalled_) {
            serverPubkeyProm_->set_value(false);
            serverPubkeySignalled_ = true;
         }
         notifyOnError(DataConnectionListener::HandshakeFailed);
         return;
      }

      // Check to see if we've already verified the key. If not, fire the
      // callback allowing the user to verify the new key.
      auto authPeerNameMap = authPeers_->getPeerNameMap();
      auto authPeerNameSearch = authPeerNameMap.find(srvAddrPort);
      if (authPeerNameSearch == authPeerNameMap.end()) {
         logger_->info("[{}] New key ({}) for server [{}] arrived.", __func__
            , newKey.toHexStr(), srvAddrPort);

         // Ask the user if they wish to accept the new identity key.
         BinaryData oldKey; // there shouldn't be any old key, at least in authPeerNameSearch
         cbNewKey_(oldKey.toHexStr(), newKey.toHexStr(), serverPubkeyProm_);
         serverPubkeySignalled_ = true;

         //have we seen the server's pubkey?
         if (serverPubkeyProm_ != nullptr) {
            //if so, wait on the promise
            auto fut = serverPubkeyProm_->get_future();
            fut.wait();
            serverPubkeyProm_.reset();
            serverPubkeySignalled_ = false;
         }

         // Add the key. Old keys aren't deleted automatically. Do it to be safe.
         std::vector<std::string> keyName;
         keyName.push_back(srvAddrPort);
         authPeers_->eraseName(srvAddrPort);
         authPeers_->addPeer(newKey.copy(), keyName);
         logger_->info("[{}] Server at {} has had its identity key (0x{}) "
            "replaced with (0x{}). Connection accepted.", __func__, srvAddrPort
            , oldKey.toHexStr(), newKey.toHexStr());
      }
   }

   AuthPeersLambdas getAuthPeerLambda() const
   {
      auto authPeerPtr = authPeers_;

      auto getMap = [authPeerPtr](void)->const std::map<std::string, btc_pubkey>& {
         return authPeerPtr->getPeerNameMap();
      };

      auto getPrivKey = [authPeerPtr](
         const BinaryDataRef& pubkey)->const SecureBinaryData& {
         return authPeerPtr->getPrivateKey(pubkey);
      };

      auto getAuthSet = [authPeerPtr](void)->const std::set<SecureBinaryData>& {
         return authPeerPtr->getPublicKeySet();
      };

      return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
   }

   void rekeyIfNeeded(const size_t& dataSize)
   {
      bool needsRekey = false;
      const auto rightNow = std::chrono::steady_clock::now();

      if (bip150HandshakeCompleted_)
      {
         // Rekey off # of bytes sent or length of time since last rekey.
         if (bip151Connection_->rekeyNeeded(dataSize))
         {
            needsRekey = true;
         }
         else
         {
            auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
               rightNow - outKeyTimePoint_);
            if (time_sec.count() >= ZMQ_AEAD_REKEY_INVERVAL_SECS)
            {
               needsRekey = true;
            }
         }

         if (needsRekey)
         {
            outKeyTimePoint_ = rightNow;
            BinaryData rekeyData(BIP151PUBKEYSIZE);
            memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

            ZmqBIP15XSerializedMessage rekeyPacket;
            rekeyPacket.construct(rekeyData.getRef(), bip151Connection_.get()
               , ZMQ_MSGTYPE_AEAD_REKEY);

            auto& packet = rekeyPacket.getNextPacket();
            if (!sendPacket(packet.toBinStr()))
            {
               if (logger_) {
                  logger_->error("[ZmqBIP15XDataConnection::{}] {} failed to send "
                     "rekey: {} (result={})", __func__, connectionName_
                     , zmq_strerror(zmq_errno()));
               }
            }
            bip151Connection_->rekeyOuterSession();
            ++outerRekeyCount_;
         }
      }
   }

   std::shared_ptr<std::promise<bool>> serverPubkeyProm_;
   bool  serverPubkeySignalled_ = false;
   std::shared_ptr<AuthorizedPeers> authPeers_;
   std::shared_ptr<BIP151Connection> bip151Connection_;
   std::chrono::time_point<std::chrono::steady_clock> outKeyTimePoint_;
   uint32_t outerRekeyCount_ = 0;
   uint32_t innerRekeyCount_ = 0;
   ZmqBIP15XMsgFragments currentReadMessage_;
   BinaryData leftOverData_;
   std::atomic_flag lockSocket_ = ATOMIC_FLAG_INIT;
   bool bip150HandshakeCompleted_ = false;
   bool bip151HandshakeCompleted_ = false;
   bool useServerIDCookie_ = true;
   bool bipIDCookieExists_ = false;
   uint32_t msgID_ = 0;
   std::function<void()>   cbCompleted_ = nullptr;
   const int   heartbeatInterval_ = 30000;
   const std::string kServerCookieName = "serverID";
   const std::string kIDCookieName = "clientID";

   cbNewKey cbNewKey_;

   std::chrono::steady_clock::time_point  lastHeartbeat_;
   std::atomic_bool        hbThreadRunning_;
   std::thread             hbThread_;
   std::mutex              hbMutex_;
   std::condition_variable hbCondVar_;
};

#endif // __ZMQ_BIP15X_DATACONNECTION_H__
