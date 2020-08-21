/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TransportBIP15x.h"

#include <chrono>

#include "BIP150_151.h"
#include "EncryptionUtils.h"
#include "FastLock.h"
#include "FutureValue.h"
#include "MessageHolder.h"
#include "StringUtils.h"
#include "SystemFileUtils.h"
#include "ThreadName.h"
#include "BIP15xMessage.h"

using namespace bs::network;


TransportBIP15x::TransportBIP15x(const std::shared_ptr<spdlog::logger> &logger
   , const std::string &cookiePath)
   : logger_(logger), cookiePath_(cookiePath)
{
   assert(logger_);
   authPeers_ = std::make_unique<AuthorizedPeers>();
}

// static
BinaryData TransportBIP15x::getOwnPubKey(const std::string &ownKeyFileDir
   , const std::string &ownKeyFileName)
{
   try {
      AuthorizedPeers authPeers(ownKeyFileDir, ownKeyFileName, [](const std::set<BinaryData> &)
      {
         return SecureBinaryData{};
      });
      return getOwnPubKey(authPeers);
   } catch (const std::exception &) {}
   return {};
}

// static
BinaryData TransportBIP15x::getOwnPubKey(const AuthorizedPeers &authPeers)
{
   try {
      const auto &pubKey = authPeers.getOwnPublicKey();
      return BinaryData(pubKey.pubkey, pubKey.compressed
         ? BTC_ECKEY_COMPRESSED_LENGTH : BTC_ECKEY_UNCOMPRESSED_LENGTH);
   } catch (...) {
      return {};
   }
}

// Get the server's compressed BIP 150 identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: A buffer with the compressed ECDSA ID pub key. (BinaryData)
BinaryData TransportBIP15x::getOwnPubKey() const
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   return getOwnPubKey(*authPeers_);
}

// Add an authorized peer's BIP 150 identity key manually.
void TransportBIP15x::addAuthPeer(const BIP15xPeer &peer)
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   bip15x::addAuthPeer(authPeers_.get(), peer);
}

void TransportBIP15x::updatePeerKeys(const BIP15xPeers &peers)
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   bip15x::updatePeerKeys(authPeers_.get(), peers);
}

// Get the counterpart's identity public key. Intended for use with local clients.
//
// INPUT:  The accompanying key IP:Port or name. (const string)
// OUTPUT: The buffer that will hold the compressed ID key. (BinaryData)
// RETURN: True if success, false if failure.
bool TransportBIP15x::getCookie(BinaryData& cookieBuf)
{
   if (!SystemFileUtils::fileExist(cookiePath_)) {
      logger_->error("[TransportBIP15x::getCookie] client identity cookie {} "
         "doesn't exist - unable to verify server identity", cookiePath_);
      return false;
   }

   // Ensure that we only read a compressed key.
   std::ifstream cookieFile(cookiePath_, std::ios::in | std::ios::binary);
   cookieFile.read(cookieBuf.getCharPtr(), BIP151PUBKEYSIZE);
   cookieFile.close();
   if (!(CryptoECDSA().VerifyPublicKeyValid(cookieBuf))) {
      logger_->error("[TransportBIP15x::getCookie] identity key {} "
         "isn't a valid compressed key - unable to verify", cookieBuf.toHexStr());
      return false;
   }
   return true;
}

// Generate a cookie with the signer's identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: True if success, false if failure.
bool TransportBIP15x::createCookie()
{
   if (SystemFileUtils::fileExist(cookiePath_)) {
      if (!SystemFileUtils::rmFile(cookiePath_)) {
         logger_->error("[TransportBIP15x::genBIPIDCookie] unable to delete"
            " identity cookie {} - will not write a new one", cookiePath_);
         return false;
      }
   }

   if (cookieFile_ != nullptr) {
      logger_->error("[TransportBIP15x::genBIPIDCookie] identity key file stream"
         " {} is already opened - aborting", cookiePath_);
      return false;
   }

   // Ensure that we only write the compressed key.
   cookieFile_ = std::make_unique<std::ofstream>(cookiePath_, std::ios::out | std::ios::binary);
   if (!cookieFile_->is_open()) {
      logger_->error("[TransportBIP15x::genBIPIDCookie] can't open identity key"
         " {} for writing", cookiePath_);
      cookieFile_.reset();
      return false;
   }
   const BinaryData ourIDKey = getOwnPubKey();
   if (ourIDKey.getSize() != BTC_ECKEY_COMPRESSED_LENGTH) {
      logger_->error("[TransportBIP15x::genBIPIDCookie] server identity key {}"
         " is uncompressed - will not write the identity cookie", cookiePath_);
      cookieFile_.reset();
      return false;
   }

   logger_->debug("[TransportBIP15x::genBIPIDCookie] writing a new identity "
      "cookie {}", cookiePath_);
   cookieFile_->write(getOwnPubKey().getCharPtr(), BTC_ECKEY_COMPRESSED_LENGTH);
   cookieFile_->flush();

   return true;
}

bool TransportBIP15x::rmCookieFile()
{
//      const string absCookiePath =
//         SystemFilePaths::appDataLocation() + "/" + bipIDCookieName_;
   cookieFile_.reset();
   if (SystemFileUtils::fileExist(cookiePath_)) {
      if (!SystemFileUtils::rmFile(cookiePath_)) {
         logger_->error("[TransportBIP15x::rmCookieFile] unable to delete "
            "identity cookie {}", cookiePath_);
         return false;
      }
   }
   return true;
}

bool TransportBIP15x::addCookieToPeers(const std::string &id
   , const BinaryData &serverPubKey)
{
   BinaryData cookieKey(static_cast<size_t>(BTC_ECKEY_COMPRESSED_LENGTH));
   if (serverPubKey.empty()) {
      if (!getCookie(cookieKey)) {
         return false;
      }
   }
   else {
      if (serverPubKey.getSize() != BTC_ECKEY_COMPRESSED_LENGTH) {
         logger_->error("[TransportBIP15x::addCookieToPeers] invalid public key"
            " length: {}", serverPubKey.getSize());
         return false;
      }
      cookieKey = serverPubKey;
   }

   // Add the host and the key to the list of verified peers. Be sure
   // to erase any old keys first.
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   authPeers_->eraseName(id);
   authPeers_->addPeer(cookieKey, std::vector<std::string>({id}));
   return true;
}

// Get lambda functions related to authorized peers. Copied from Armory.
//
// INPUT:  None
// OUTPUT: None
// RETURN: AuthPeersLambdas object with required lambdas.
AuthPeersLambdas TransportBIP15x::getAuthPeerLambda()
{
   auto getMap = [this](void)->const std::map<std::string, bip15x::PubKey>&
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPeerNameMap();
   };

   auto getPrivKey = [this](const BinaryDataRef& pubkey)->const SecureBinaryData&
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPrivateKey(pubkey);
   };

   auto getAuthSet = [this](void)->const std::set<SecureBinaryData>&
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPublicKeySet();
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}

bool TransportBIP15x::fail()
{
   isValid_ = false;
   return false;
};

bool TransportBIP15x::processAEAD(const bip15x::Message &inMsg
   , const std::unique_ptr<BIP151Connection> &bip151Conn, const WriteDataCb &writeCb
   , bool requesterSent/*true for server*/)
{
   const auto &msgData = inMsg.getData();
   switch (inMsg.getType()) {
   case bip15x::MsgType::AEAD_EncInit:
   {
      if (bip151Conn->processEncinit(msgData.getPtr(), msgData.getSize()
         , false) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCINIT not processed");
         return fail();
      }

      //valid encinit, send client side encack
      BinaryData encackPayload(BIP151PUBKEYSIZE);
      if (bip151Conn->getEncackData(encackPayload.getPtr()
         , BIP151PUBKEYSIZE) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCACK data not obtained");
         return fail();
      }
      writeCb(bip15x::MsgType::AEAD_EncAck, encackPayload, false);
   }
   break;

   case bip15x::MsgType::AEAD_EncAck:
      if (bip151Conn->processEncack(msgData.getPtr(), msgData.getSize()
         , true) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCACK not processed");
         return fail();
      }
      break;

   case bip15x::MsgType::AEAD_Rekey:
      // Rekey requests before auth are invalid.
      if (bip151Conn->getBIP150State() != BIP150State::SUCCESS) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - Not ready to rekey");
         return fail();
      }

      // If connection is already setup, we only accept rekey enack messages.
      if (bip151Conn->processEncack(msgData.getPtr(), msgData.getSize()
         , false) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_REKEY not processed");
         return fail();
      }
      break;

   case bip15x::MsgType::AuthReply:
      if (bip151Conn->processAuthreply(msgData.getPtr(), msgData.getSize()
         , !requesterSent, bip151Conn->getProposeFlag()) != 0) { //false: haven't seen an auth challenge yet
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_REPLY not processed");
         return fail();
      }
      break;

   case bip15x::MsgType::AuthChallenge:
   {
      bool goodChallenge = true;
      int challengeResult = bip151Conn->processAuthchallenge(msgData.getPtr()
         , msgData.getSize(), requesterSent);

      if (challengeResult == -1) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_CHALLENGE not processed");
         return fail();
      } else if (challengeResult == 1) {
         goodChallenge = false;
      }

      BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
      int validReply = bip151Conn->getAuthreplyData(authreplyBuf.getPtr()
         , authreplyBuf.getSize(), requesterSent, goodChallenge);

      if (validReply != 0) {  // auth setup failure, kill connection
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_REPLY data not obtained");
         return fail();
      }
      if (!writeCb(bip15x::MsgType::AuthReply, authreplyBuf, true)) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151"
            " handshake process failed - AUTH_REPLY not sent");
         return fail();
      }
   }
   break;

   case bip15x::MsgType::AuthPropose:
   {  // Continue after common AEAD handling
      bool goodPropose = true;
      auto proposeResult = bip151Conn->processAuthpropose(msgData.getPtr(), msgData.getSize());

      if (proposeResult == -1) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151"
            " handshake process failed - AUTH_PROPOSE processing failed");
         return fail();
      } else if (proposeResult == 1) {
         goodPropose = false;
      } else {
         //keep track of the propose check state
         bip151Conn->setGoodPropose();
      }

      BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
      if (bip151Conn->getAuthchallengeData(authchallengeBuf.getPtr()
         , authchallengeBuf.getSize()
         , "" //empty string, use chosen key from processing auth propose
         , !requesterSent, goodPropose) == -1) {
         //auth setup failure, kill connection
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 handshake process "
            "failed - AUTH_CHALLENGE data not obtained");
         return fail();
      }
      if (!writeCb(bip15x::MsgType::AuthChallenge, authchallengeBuf.getRef(), true)) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151"
            " handshake process failed - AUTH_CHALLENGE not sent");
         return fail();
      }
   }
   break;

   default: break;
   }
   return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////////
TransportBIP15xClient::TransportBIP15xClient(const std::shared_ptr<spdlog::logger> &logger
   , const BIP15xParams &params)
   : TransportBIP15x(logger, params.cookiePath)
   , params_(params)
{
   assert(logger_);

   if (!params.ephemeralPeers && (params.ownKeyFileDir.empty() || params.ownKeyFileName.empty())) {
      throw std::runtime_error("Client requested static ID key but no key " \
         "wallet file is specified.");
   }

   if ((params_.cookie == BIP15xCookie::MakeClient) && params_.cookiePath.empty()) {
      throw std::runtime_error("ID cookie creation requested but no name " \
         "supplied. Connection is incomplete.");
   }
   else if ((params_.cookie == BIP15xCookie::ReadServer) && params_.cookiePath.empty()
      && params.serverPublicKey.empty()) {
      throw std::runtime_error("server cookie read requested but no name " \
         "or public key supplied. Connection is incomplete.");
   }

   outKeyTimePoint_ = std::chrono::steady_clock::now();

   // In general, load the server key from a special Armory wallet file.
   if (!params.ephemeralPeers) {
      authPeers_ = std::make_unique<AuthorizedPeers>(
         params.ownKeyFileDir, params.ownKeyFileName, [](const std::set<BinaryData> &)
      {
         return SecureBinaryData{};
      });
   }

   if (params_.cookie == BIP15xCookie::MakeClient) {
      createCookie();
   }
}

TransportBIP15xClient::~TransportBIP15xClient() noexcept
{
   // Need to close connection before socket connection is partially destroyed!
   // Otherwise it might crash in ProcessIncomingData a bit later
   closeConnection();

   // If it exists, delete the identity cookie.
   if (params_.cookie == BIP15xCookie::MakeClient) {
      rmCookieFile();
   }
}

// A function that handles any required rekeys before data is sent.
//
// ***This function must be called before any data is sent on the wire.***
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15xClient::rekeyIfNeeded(size_t dataSize)
{
   bool needsRekey = false;
   const auto rightNow = std::chrono::steady_clock::now();

   if (bip150HandshakeCompleted_) {
      // Rekey off # of bytes sent or length of time since last rekey.
      if (bip151Connection_->rekeyNeeded(dataSize)) {
         needsRekey = true;
      }
      else {
         auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
            rightNow - outKeyTimePoint_);
         if (time_sec.count() >= bip15x::AEAD_REKEY_INTERVAL_SECS) {
            needsRekey = true;
         }
      }

      if (needsRekey) {
         outKeyTimePoint_ = rightNow;
         rekey();
      }
   }
}

bool TransportBIP15xClient::sendData(const std::string &data)
{
   if (!bip151Connection_ || (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      SPDLOG_LOGGER_ERROR(logger_, "transport is not connected, sending packet failed");
      return false;
   }

   rekeyIfNeeded(data.size());

   auto packet = bip15x::MessageBuilder(BinaryData::fromString(data)
      , bip15x::MsgType::SinglePacket).encryptIfNeeded(bip151Connection_.get()).build();
   // An error message is already logged elsewhere if the send fails.
   sendPacket(packet);
   return true;
}

bool TransportBIP15xClient::sendPacket(const BinaryData &packet, bool encrypted)
{
   if (!sendCb_) {
      logger_->error("[TransportBIP15xClient::sendPacket] send callback not set");
      return false;
   }
   if (!isValid()) {
      logger_->error("[TransportBIP15xClient::sendPacket] sending packet in invalid state");
   }
   if (encrypted && (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      if ((bip151Connection_->getBIP150State() != BIP150State::CHALLENGE1) &&
         (bip151Connection_->getBIP150State() != BIP150State::PROPOSE) &&
         (bip151Connection_->getBIP150State() != BIP150State::REPLY2)) {
         logger_->error("[TransportBIP15xClient::sendPacket] attempt to send encrypted packet"
            " before encryption turned on ({})", (int)bip151Connection_->getBIP150State());
         return fail();
      }
   }

   return sendCb_(packet.toBinStr());
}

void TransportBIP15xClient::rekey()
{
   logger_->debug("[TransportBIP15xClient::rekey] rekeying");

   if (!bip150HandshakeCompleted_) {
      logger_->error("[TransportBIP15xClient::rekey] Can't rekey before BIP150 "
         "handshake is complete", __func__);
      fail();
      return;
   }

   BinaryData rekeyData(BIP151PUBKEYSIZE);
   memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

   auto packet = bip15x::MessageBuilder(rekeyData
      , bip15x::MsgType::AEAD_Rekey).encryptIfNeeded(bip151Connection_.get()).build();
   logger_->debug("[TransportBIP15xClient::rekey] rekeying session ({} {})"
      , rekeyData.toHexStr(), packet.toHexStr());
   sendPacket(packet);
   bip151Connection_->rekeyOuterSession();
   ++outerRekeyCount_;
}


// Kick off the BIP 151 handshake. This is the first function to call once the
// unencrypted connection is established.
//
// INPUT:  None
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15xClient::startBIP151Handshake()
{
   const auto &packet = bip15x::MessageBuilder(bip15x::MsgType::AEAD_Setup).build();
   sendPacket(packet, false);
   return true;
}

// The function that handles raw data coming in from the socket. The data may or
// may not be encrypted.
//
// INPUT:  The raw incoming data. (const string&)
// OUTPUT: None
// RETURN: None
void TransportBIP15xClient::onRawDataReceived(const std::string &rawData)
{
   if (!bip151Connection_ || !isValid()) {
      logger_->error("[TransportBIP15xClient::onRawDataReceived] received {} bytes of"
         " data in disconnected/invalid state", rawData.size());
      return;
   }

   auto payload = BinaryData::fromString(rawData);
   if (!bip151Connection_) {
      logger_->error("[TransportBIP15xClient::onRawDataReceived] received {}-sized"
         " packet in disconnected state", payload.getSize());
      return;
   }
   // Perform decryption if we're ready.
   if (bip151Connection_->connectionComplete()) {
      auto result = bip151Connection_->decryptPacket(
         payload.getPtr(), payload.getSize(),
         payload.getPtr(), payload.getSize());

      if (result != 0) {
         logger_->error("[TransportBIP15xClient::onRawDataReceived] Packet [{} bytes]"
            " decryption failed - error {}", payload.getSize(), result);
         if (socketErrorCb_) {
            socketErrorCb_(DataConnectionListener::ProtocolViolation);
         }
         fail();
         return;
      }
      payload.resize(payload.getSize() - POLY1305MACLEN);
   }
   processIncomingData(payload);
}

void TransportBIP15xClient::openConnection(const std::string &host
   , const std::string &port)
{
   closeConnection();

   host_ = host;
   port_ = port;

   // BIP 151 connection setup. Technically should be per-socket or something
   // similar but data connections will only connect to one machine at a time.
   auto lbds = getAuthPeerLambda();
   bip151Connection_ = std::make_unique<BIP151Connection>(lbds);
}

void TransportBIP15xClient::closeConnection()
{
   // If a future obj is still waiting, satisfy it to prevent lockup. This
   // shouldn't happen here but it's an emergency fallback.
   if (serverPubkeyProm_) {
      serverPubkeyProm_->setValue(false);
   }

   SPDLOG_LOGGER_DEBUG(logger_, "[TransportBIP15xClient::closeConnection]");

   bip151Connection_.reset();
   bip150HandshakeCompleted_ = false;
   bip151HandshakeCompleted_ = false;
}

// The function that processes raw ZMQ connection data. It processes the BIP
// 150/151 handshake (if necessary) and decrypts the raw data.
//
// INPUT:  Reformed message. (const BinaryData&) // TODO: FIX UP MSG
// OUTPUT: None
// RETURN: None
void TransportBIP15xClient::processIncomingData(const BinaryData &payload)
{
   const auto &msg = bip15x::Message::parse(payload);
   if (!msg.isValid()) {
      logger_->error("[TransportBIP15xClient::processIncomingData] deserialization failed");
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::SerializationFailed);
      }
      return;
   }

   // If we're still handshaking, take the next step. (No fragments allowed.)
   if (msg.getType() > bip15x::MsgType::AEAD_Threshold) {
      if (!processAEADHandshake(msg)) {
         logger_->error("[TransportBIP15xClient::processIncomingData] handshake failed");
         if (socketErrorCb_) {
            socketErrorCb_(DataConnectionListener::HandshakeFailed);
         }
      }
      return;
   }

   // We can now safely obtain the full message.
   BinaryDataRef inMsg = msg.getData();

   // We shouldn't get here but just in case....
   if (!bip151Connection_ || (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      logger_->error("[TransportBIP15xClient::processIncomingData] encryption handshake is incomplete");
      if (bip151Connection_ && socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::HandshakeFailed);
      }
      return;
   }

   // Pass the final data up the chain.
   if (notifyDataCb_) {
      notifyDataCb_(inMsg.toBinStr());
   }
}

// The function processing the BIP 150/151 handshake packets.
//
// INPUT:  The handshake packet. (const ZmqBIP15XMsg&)
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15xClient::processAEADHandshake(const bip15x::Message &msgObj)
{
   // Function used to send data out on the wire.
   auto writeData = [this](bip15x::MsgType type, const BinaryData &payload, bool encrypt)->bool {
      auto conn = encrypt ? bip151Connection_.get() : nullptr;
      auto packet = bip15x::MessageBuilder(payload, type).encryptIfNeeded(conn).build();
      return sendPacket(packet, (conn != nullptr));
   };

   const std::string &srvId = host_ + ":" + port_;

   // Read the message, get the type, and process as needed. Code mostly copied
   // from Armory.
   if (processAEAD(msgObj, bip151Connection_, writeData, false)) {
      switch (msgObj.getType()) {
      case bip15x::MsgType::AEAD_PresentPubkey:
      {  // Not handled by common AEAD code
         /*packet is server's pubkey, do we have it?*/
         //init server promise
         serverPubkeyProm_ = std::make_shared<FutureValue<bool>>();

         // If it's a local connection, get a cookie with the server's key.
         if (params_.cookie == BIP15xCookie::ReadServer) {
            if (!addCookieToPeers(srvId, params_.serverPublicKey)) {
               return false;
            }
         }

         // If we don't have the key already, we may ask the the user if they wish
         // to continue. (Remote signer only.)
         if (!bip151Connection_->havePublicKey(msgObj.getData(), srvId)) {
            //we don't have this key, call user prompt lambda
            if (verifyNewIDKey(msgObj.getData(), srvId)) {
               // Add the key. Old keys aren't deleted automatically. Do it to be safe.
               std::lock_guard<std::mutex> lock(authPeersMutex_);
               authPeers_->eraseName(srvId);
               authPeers_->addPeer(msgObj.getData().copy(), std::vector<std::string>{ srvId });
            }
         } else {
            //set server key promise
            if (serverPubkeyProm_) {
               serverPubkeyProm_->setValue(true);
            } else {
               logger_->warn("[TransportBIP15xClient::processAEADHandshake] server public key was already set");
            }
         }
      }
      break;

      case bip15x::MsgType::AEAD_EncInit:
      {  // Continue after common AEAD processing
         //start client side encinit
         BinaryData encinitPayload(ENCINITMSGSIZE);
         if (bip151Connection_->getEncinitData(encinitPayload.getPtr()
            , ENCINITMSGSIZE, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0) {
            logger_->error("[TransportBIP15xClient::processAEADHandshake] BIP 150/151 "
               "handshake process failed - AEAD_ENCINIT data not obtained");
            return fail();
         }
         writeData(bip15x::MsgType::AEAD_EncInit, encinitPayload, false);
      }
      break;

      case bip15x::MsgType::AEAD_EncAck:
      {  // Continue after common AEAD processing
         // Do we need to check the server's ID key?
         if (serverPubkeyProm_ != nullptr) {
            //if so, wait on the promise
            bool result = serverPubkeyProm_->waitValue();

            if (result) {
               serverPubkeyProm_.reset();
            } else {
               logger_->error("[TransportBIP15xClient::processAEADHandshake] BIP 150/151"
                  " handshake process failed - AEAD_ENCACK - Server public key not verified");
               return fail();
            }
         }

         //bip151 handshake completed, time for bip150
         BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
         if (bip151Connection_->getAuthchallengeData(
            authchallengeBuf.getPtr(), authchallengeBuf.getSize(), srvId
            , true //true: auth challenge step #1 of 6
            , false) != 0) { //false: have not processed an auth propose yet
            logger_->error("[TransportBIP15xClient::processAEADHandshake] BIP 150/151 "
               "handshake process failed - AUTH_CHALLENGE data not obtained");
            return fail();
         }
         writeData(bip15x::MsgType::AuthChallenge, authchallengeBuf, true);
         bip151HandshakeCompleted_ = true;
      }
      break;

      case bip15x::MsgType::AEAD_Rekey:
         //Continue after common AEAD processing
         ++innerRekeyCount_;
         break;

      case bip15x::MsgType::AuthReply:
      {  // Continue after common AEAD processing
         BinaryData authproposeBuf(BIP151PRVKEYSIZE);
         if (bip151Connection_->getAuthproposeData(
            authproposeBuf.getPtr(),
            authproposeBuf.getSize()) != 0) {
            logger_->error("[TransportBIP15xClient::processAEADHandshake] BIP 150/151 "
               "handshake process failed - AUTH_PROPOSE data not obtained");
            return fail();
         }
         writeData(bip15x::MsgType::AuthPropose, authproposeBuf, true);
      }
      break;

      case bip15x::MsgType::AuthChallenge:
      {  //Continue after common AEAD processing
         // Rekey.
         bip151Connection_->bip150HandshakeRekey();
         bip150HandshakeCompleted_ = true;

         auto now = std::chrono::steady_clock::now();
         outKeyTimePoint_ = now;

         logger_->debug("[TransportBIP15xClient::processAEADHandshake] BIP 150 handshake"
            " with server complete - connection to {} is ready and fully secured", srvId);
         if (socketErrorCb_) {
            socketErrorCb_(DataConnectionListener::NoError);
         }
      }
      break;

      default: break;
      }
      return true;
   }
   return false;
}

// Set the callback to be used when asking if the user wishes to accept BIP 150
// identity keys from a server. See the design notes in the header for details.
//
// INPUT:  The callback that will ask the user to confirm the new key. (std::function)
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15xClient::setKeyCb(const BIP15xNewKeyCb &cb)
{
   if (cb) {
      cbNewKey_ = cb;
   }
   else {
      cbNewKey_ = [this](const std::string &, const std::string, const std::string&
            , const std::shared_ptr<FutureValue<bool>> &prom) {
         SPDLOG_LOGGER_DEBUG(logger_, "no new key callback was set - auto-accepting connections");
         prom->setValue(true);
      };
   }
}

// If the user is presented with a new remote server ID key it doesn't already
// know about, verify the key. A promise will also be used in case any functions
// are waiting on verification results.
//
// INPUT:  The key to verify. (const BinaryDataRef)
//         The server IP address (or host name) and port. (const string)
// OUTPUT: N/A
// RETURN: N/A
bool TransportBIP15xClient::verifyNewIDKey(const BinaryDataRef &key, const std::string &srvId)
{
   if (params_.cookie == BIP15xCookie::ReadServer) {
      // If we get here, it's because the cookie add failed or the cookie was
      // incorrect. Satisfy the promise to prevent lockup.
      logger_->error("[TransportBIP15xClient::verifyNewIDKey] Server ID key cookie could not be verified", __func__);
      serverPubkeyProm_->setValue(false);
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::HandshakeFailed);
      }
      return false;
   }

   logger_->debug("[TransportBIP15xClient::verifyNewIDKey] new key ({}) for server {}"
      " has arrived", key.toHexStr(), srvId);

   if (!cbNewKey_) {
      logger_->error("[TransportBIP15xClient::verifyNewIDKey] no server key callback "
         "is set - aborting handshake");
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::HandshakeFailed);
      }
      return false;
   }

   // Ask the user if they wish to accept the new identity key.
   // There shouldn't be any old key, at least in authPeerNameSearch
   cbNewKey_({}, key.toHexStr(), srvId, serverPubkeyProm_);
   bool cbResult = false;

   //have we seen the server's pubkey?
   if (serverPubkeyProm_ != nullptr) {
      //if so, wait on the promise
      cbResult = serverPubkeyProm_->waitValue();
      serverPubkeyProm_.reset();
   }

   if (!cbResult) {
      logger_->info("[TransportBIP15xClient::verifyNewIDKey] user refused new server {}"
         " identity key {} - connection refused", srvId, key.toHexStr());
      return false;
   }

   logger_->info("[TransportBIP15xClient::verifyNewIDKey] server {} has new identity "
      "key {} - connection accepted", srvId, key.toHexStr());
   return true;
}

// Generate a cookie with the client's identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: True if success, false if failure.
bool TransportBIP15xClient::createCookie()
{
   if (params_.cookie != BIP15xCookie::MakeClient) {
      logger_->error("[TransportBIP15xClient::genBIPIDCookie] ID cookie creation "
         "requested but not allowed");
      return false;
   }
   return TransportBIP15x::createCookie();
}

bool TransportBIP15xClient::getCookie(BinaryData &cookieBuf)
{
   if (params_.cookie != BIP15xCookie::ReadServer) {
      return false;
   }
   return TransportBIP15x::getCookie(cookieBuf);
}
