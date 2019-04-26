#include <chrono>

#include "ZMQ_BIP15X_ServerConnection.h"
#include "MessageHolder.h"
#include "SystemFileUtils.h"

using namespace std;

static const std::string localAddrV4k = "127.0.0.1";
static const std::string clientCookieNamek = "clientID";
static const std::string idCookieNamek = "serverID";

// A call resetting the encryption-related data for individual connections.
//
// INPUT:  None
// OUTPUT: None
// RETURN: None
void ZmqBIP15XPerConnData::reset()
{
   encData_.reset();
   bip150HandshakeCompleted_ = false;
   bip151HandshakeCompleted_ = false;
   currentReadMessage_.reset();
   outKeyTimePoint_ = chrono::steady_clock::now();
}

// The constructor to use.
//
// INPUT:  Logger object. (const shared_ptr<spdlog::logger>&)
//         ZMQ context. (const std::shared_ptr<ZmqContext>&)
//         List of trusted clients. (const QStringList&)
//         Per-connection ID. (const uint64_t&)
//         Ephemeral peer usage. Not recommended. (const bool&)
// OUTPUT: None
ZmqBIP15XServerConnection::ZmqBIP15XServerConnection(
   const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<ZmqContext>& context
   , const std::vector<std::string>& trustedClients, const uint64_t& id
   , const bool& ephemeralPeers)
   : ZmqServerConnection(logger, context), id_(id)
{
   string datadir = SystemFilePaths::appDataLocation();
   string filename(SERVER_AUTH_PEER_FILENAME);

   // In general, load the client key from a special Armory wallet file.
   if (!ephemeralPeers) {
       authPeers_ = make_shared<AuthorizedPeers>(datadir, filename);
   }
   else {
      authPeers_ = make_shared<AuthorizedPeers>();
   }

   cbTrustedClients_ = [trustedClients]() -> std::vector<std::string> {
      return trustedClients;
   };

   genBIPIDCookie();
   heartbeatThread();
}

ZmqBIP15XServerConnection::ZmqBIP15XServerConnection(
   const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<ZmqContext>& context
   , const std::function<std::vector<std::string>()> &cbTrustedClients)
   : ZmqServerConnection(logger, context)
   , cbTrustedClients_(cbTrustedClients)
{
   authPeers_ = make_shared<AuthorizedPeers>();
   BinaryData bdID = CryptoPRNG::generateRandom(8);
   id_ = READ_UINT64_LE(bdID.getPtr());

   genBIPIDCookie();
   heartbeatThread();
}

ZmqBIP15XServerConnection::~ZmqBIP15XServerConnection()
{
   hbThreadRunning_ = false;
   hbCondVar_.notify_one();
   hbThread_.join();

   // If it exists, delete the identity cookie.
   if (bipIDCookieExists_) {
      const string absCookiePath =
         SystemFilePaths::appDataLocation() + "/" + idCookieNamek;
      if (SystemFileUtils::fileExist(absCookiePath)) {
         if (!SystemFileUtils::rmFile(absCookiePath)) {
            logger_->error("[{}] Unable to delete server identity cookie ({})."
               , __func__, absCookiePath);
         }
      }
   }
}

void ZmqBIP15XServerConnection::heartbeatThread()
{
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
         std::vector<std::string> timedOutClients;
         for (const auto &hbTime : lastHeartbeats_) {
            const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - hbTime.second);
            if (diff.count() > heartbeatInterval_) {
               timedOutClients.push_back(hbTime.first);
            }
         }
         {
            std::unique_lock<std::mutex> lock(clientsMtx_);
            for (const auto &client : timedOutClients) {
               lastHeartbeats_.erase(client);
               resetBIP151Connection(client);
            }
         }
         for (const auto &client : timedOutClients) {    // invoke callbacks outside the lock
            logger_->debug("[ZmqBIP15XServerConnection] client {} timed out"
               , BinaryData(client).toHexStr());
            notifyListenerOnDisconnectedClient(client);
         }
      }
   };
   hbThreadRunning_ = true;
   hbThread_ = std::thread(heartbeatProc);
}

// Create the data socket.
//
// INPUT:  None
// OUTPUT: None
// RETURN: The data socket. (ZmqContext::sock_ptr)
ZmqContext::sock_ptr ZmqBIP15XServerConnection::CreateDataSocket()
{
   return context_->CreateServerSocket();
}

// Get the incoming data.
//
// INPUT:  None
// OUTPUT: None
// RETURN: True if success, false if failure.
bool ZmqBIP15XServerConnection::ReadFromDataSocket()
{
   MessageHolder clientId;
   MessageHolder data;

   // The client ID will be sent before the actual data.
   int result = zmq_msg_recv(&clientId, dataSocket_.get(), ZMQ_DONTWAIT);
   if ((result == -1) || !clientId.GetSize())
   {
      logger_->error("[{}] {} failed to recv header: {}", __func__
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   // Now, we can grab the incoming data, whih includes the message ID once the
   // handshake is complete.
   result = zmq_msg_recv(&data, dataSocket_.get(), ZMQ_DONTWAIT);
   if (result == -1)
   {
      logger_->error("[{}] {} failed to recv message data: {}", __func__
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   // ZMQ should send one chunk of data, and not further fragment the data.
   if (!data.IsLast())
   {
      logger_->error("[{}] {} broken protocol", __func__, connectionName_);
      return false;
   }

   // Process the incoming data.
   ProcessIncomingData(data.ToString(), clientId.ToString());

   return true;
}

// The send function for the data connection. Ideally, this should not be used
// before the handshake is completed, but it is possible to use at any time.
// Whether or not the raw data is used, it will be placed in a
// ZmqBIP15XSerializedMessage object.
//
// INPUT:  The ZMQ client ID. (const string&)
//         The data to send. (const string&)
//         A post-send callback. Optional. (const SendResultCb&)
// OUTPUT: None
// RETURN: True if success, false if failure.
bool ZmqBIP15XServerConnection::SendDataToClient(const string& clientId
   , const string& data, const SendResultCb& cb)
{
   bool retVal = false;
   BIP151Connection* connPtr = nullptr;
   if (socketConnMap_[clientId]->bip151HandshakeCompleted_)
   {
      connPtr = socketConnMap_[clientId]->encData_.get();
   }

   // Check if we need to do a rekey before sending the data.
   if (socketConnMap_[clientId]->bip150HandshakeCompleted_)
   {
      bool needsRekey = false;
      auto rightNow = chrono::steady_clock::now();

      // Rekey off # of bytes sent or length of time since last rekey.
      if (connPtr->rekeyNeeded(data.size()))
      {
         needsRekey = true;
      }
      else
      {
         auto time_sec = chrono::duration_cast<chrono::seconds>(
            rightNow - socketConnMap_[clientId]->outKeyTimePoint_);
         if (time_sec.count() >= ZMQ_AEAD_REKEY_INVERVAL_SECS)
         {
            needsRekey = true;
         }
      }

      if (needsRekey)
      {
         socketConnMap_[clientId]->outKeyTimePoint_ = rightNow;
         BinaryData rekeyData(BIP151PUBKEYSIZE);
         memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

         ZmqBIP15XSerializedMessage rekeyPacket;
         rekeyPacket.construct(rekeyData.getRef(), connPtr, ZMQ_MSGTYPE_AEAD_REKEY);

         auto& packet = rekeyPacket.getNextPacket();
         if (!SendDataToClient(clientId, packet.toBinStr(), cb))
         {
            logger_->error("[ZmqBIP15XServerConnection::{}] {} failed to send "
               "rekey: {} (result={})", __func__, connectionName_
               , zmq_strerror(zmq_errno()));
         }
         connPtr->rekeyOuterSession();
         socketConnMap_[clientId]->outerRekeyCount_++;
      }
   }

   // Encrypt data here if the BIP 150 handshake is complete.
   if (socketConnMap_[clientId] && socketConnMap_[clientId]->encData_){
      if (socketConnMap_[clientId]->encData_->getBIP150State() ==
         BIP150State::SUCCESS)
      {
         string sendStr = data;
         const BinaryData payload(data);
         ZmqBIP15XSerializedMessage msg;
         msg.construct(payload.getDataVector(), connPtr
            , ZMQ_MSGTYPE_FRAGMENTEDPACKET_HEADER, socketConnMap_[clientId]->msgID_);

         // Cycle through all packets.
         while (!msg.isDone())
         {
            auto& packet = msg.getNextPacket();
            if (packet.getSize() == 0) {
               logger_->error("[ZmqBIP15XServerConnection::{}] failed to "
                  "serialize data (size {})", __func__, data.size());
               return retVal;
            }

            retVal = QueueDataToSend(clientId, packet.toBinStr(), cb, false);
            if (!retVal)
            {
               logger_->error("[ZmqBIP15XServerConnection::{}] fragment send failed"
                  , __func__, data.size());
               return retVal;
            }
         }
      }
      else
      {
         // Queue up the untouched data for straight transmission.
         retVal = QueueDataToSend(clientId, data, cb, false);
      }
   }


   return retVal;
}

bool ZmqBIP15XServerConnection::SendDataToAllClients(const std::string& data, const SendResultCb &cb)
{
   unsigned int successCount = 0;
   std::unique_lock<std::mutex> lock(clientsMtx_);

   for (const auto &it : socketConnMap_)
   {
      if (SendDataToClient(it.first, data, cb))
      {
         successCount++;
      }
   }
   return (successCount == socketConnMap_.size());
}

// The function that processes raw ZMQ connection data. It processes the BIP
// 150/151 handshake (if necessary) and decrypts the raw data.
//
// INPUT:  None
// OUTPUT: None
// RETURN: None
void ZmqBIP15XServerConnection::ProcessIncomingData(const string& encData
   , const string& clientID)
{
   // Backstop in case the callbacks haven't been used.
   if (socketConnMap_[clientID] == nullptr) {
      setBIP151Connection(clientID);
   }

   BinaryData payload(encData);

   // If decryption "failed" due to fragmentation, put the pieces together.
   // (Unlikely but we need to plan for it.)
   if (leftOverData_.getSize() != 0) {
      leftOverData_.append(payload);
      payload = move(leftOverData_);
      leftOverData_.clear();
   }

   const auto &connData = socketConnMap_[clientID];
   if (!connData) {
      logger_->error("[{}] failed to find connection data for client {}"
         , __func__, BinaryData(clientID).toHexStr());
      return;
   }

   // Decrypt only if the BIP 151 handshake is complete.
   if (connData->bip151HandshakeCompleted_) {
      //decrypt packet
      auto result = socketConnMap_[clientID]->encData_->decryptPacket(
         payload.getPtr(), payload.getSize(),
         payload.getPtr(), payload.getSize());

      // Failure isn't necessarily a problem if we're dealing with fragments.
      if (result != 0) {
         // If decryption "fails" but the result indicates fragmentation, save
         // the fragment and wait before doing anything, otherwise treat it as a
         // legit error.
         if (result <= ZMQ_MESSAGE_PACKET_SIZE && result > -1)
         {
            leftOverData_ = move(payload);
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

   // Deserialize packet.
   auto payloadRef = connData->currentReadMessage_.insertDataAndGetRef(payload);
   auto result = connData->currentReadMessage_.message_.parsePacket(payloadRef);
   if (!result) {
      if (logger_) {
         logger_->error("[ZmqBIP15XServerConnection::{}] Deserialization failed "
            "(connection {})", __func__, connectionName_);
      }
      connData->currentReadMessage_.reset();
      return;
   }

   // Fragmented messages may not be marked as fragmented when decrypted but may
   // still be a fragment. That's fine. Just wait for the other fragments.
   if (!connData->currentReadMessage_.message_.isReady()) {
      return;
   }

   connData->msgID_ = connData->currentReadMessage_.message_.getId();

   // If we're still handshaking, take the next step. (No fragments allowed.)
   if (connData->currentReadMessage_.message_.getType() == ZMQ_MSGTYPE_HEARTBEAT) {
      lastHeartbeats_[clientID] = std::chrono::steady_clock::now();
      connData->currentReadMessage_.reset();
      return;
   }
   else if (connData->currentReadMessage_.message_.getType() >
      ZMQ_MSGTYPE_AEAD_THRESHOLD) {
      if (!processAEADHandshake(connData->currentReadMessage_.message_, clientID)) {
         if (logger_) {
            logger_->error("[ZmqBIP15XServerConnection::{}] Handshake failed "
               "(connection {})", __func__, connectionName_);
         }
         return;
      }

      connData->currentReadMessage_.reset();
      return;
   }

   // We can now safely obtain the full message.
   BinaryData outMsg;
   connData->currentReadMessage_.message_.getMessage(&outMsg);

   // We shouldn't get here but just in case....
   if (connData->encData_->getBIP150State() !=
      BIP150State::SUCCESS) {
      if (logger_) {
         logger_->error("[ZmqBIP15XServerConnection::{}] Encryption handshake "
            "is incomplete (connection {})", __func__, connectionName_);
      }
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
   notifyListenerOnData(clientID, outMsg.toBinStr());
   lastHeartbeats_[clientID] = std::chrono::steady_clock::now();
   connData->currentReadMessage_.reset();
}

// The function processing the BIP 150/151 handshake packets.
//
// INPUT:  The raw handshake packet data. (const BinaryData&)
//         The client ID. (const string&)
// OUTPUT: None
// RETURN: True if success, false if failure.
bool ZmqBIP15XServerConnection::processAEADHandshake(
   const ZmqBIP15XMsgPartial& msgObj, const string& clientID)
{
   // Function used to actually send data to the client.
   auto writeToClient = [this, clientID](uint8_t type, const BinaryDataRef& msg
      , bool encrypt)->bool
   {
      ZmqBIP15XSerializedMessage outMsg;
      BIP151Connection* connPtr = nullptr;
      if (encrypt)
      {
         connPtr = socketConnMap_[clientID]->encData_.get();
      }

      // Construct the message and fire it down the pipe.
      outMsg.construct(msg, connPtr, type, 0);
      auto& packet = outMsg.getNextPacket();
      return SendDataToClient(clientID, packet.toBinStr());
   };

   // Handshake function. Code mostly copied from Armory.
   auto processHandshake = [this, &writeToClient, clientID, msgObj]()->bool
   {
      // Parse the packet.
      auto dataBdr = msgObj.getSingleBinaryMessage();
      switch (msgObj.getType())
      {
      case ZMQ_MSGTYPE_AEAD_SETUP:
      {
         // If it's a local connection, get a cookie with the server's key.
         if (useClientIDCookie_) {
            // Read the cookie with the key to check.
            BinaryData cookieKey(static_cast<size_t>(BTC_ECKEY_COMPRESSED_LENGTH));
            if (!getClientIDCookie(cookieKey, clientCookieNamek)) {
               return false;
            }
            else {
               // Add the host and the key to the list of verified peers. Be sure
               // to erase any old keys first.
               vector<string> keyName;
               string localAddrV4 = "127.0.0.1:23457";
               keyName.push_back(localAddrV4);
               authPeers_->eraseName(localAddrV4);
               authPeers_->addPeer(cookieKey, keyName);
            }
         }

         //send pubkey message
         if (!writeToClient(ZMQ_MSGTYPE_AEAD_PRESENT_PUBKEY,
            socketConnMap_[clientID]->encData_->getOwnPubKey(), false))
         {
            logger_->error("[processHandshake] ZMG_MSGTYPE_AEAD_SETUP: "
               "Response 1 not sent");
         }

         //init bip151 handshake
         BinaryData encinitData(ENCINITMSGSIZE);
         if (socketConnMap_[clientID]->encData_->getEncinitData(
            encinitData.getPtr(), ENCINITMSGSIZE
            , BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0)
         {
            //failed to init handshake, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCINIT data not obtained");
            return false;
         }

         if (!writeToClient(ZMQ_MSGTYPE_AEAD_ENCINIT, encinitData.getRef()
            , false))
         {
            logger_->error("[processHandshake] ZMG_MSGTYPE_AEAD_SETUP: "
               "Response 2 not sent");
         }
         break;
      }

      case ZMQ_MSGTYPE_AEAD_ENCACK:
      {
         //process client encack
         if (socketConnMap_[clientID]->encData_->processEncack(dataBdr.getPtr()
            , dataBdr.getSize(), true) != 0)
         {
            //failed to init handshake, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCACK not processed");
            return false;
         }

         break;
      }

      case ZMQ_MSGTYPE_AEAD_REKEY:
      {
         // Rekey requests before auth are invalid
         if (socketConnMap_[clientID]->encData_->getBIP150State() !=
            BIP150State::SUCCESS)
         {
            //can't rekey before auth, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - Not yet able to process a rekey");
            return false;
         }

         // If connection is already set up, we only accept rekey encack messages.
         if (socketConnMap_[clientID]->encData_->processEncack(dataBdr.getPtr()
            , dataBdr.getSize(), false) != 0)
         {
            //failed to init handshake, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCACK not processed");
            return false;
         }

         socketConnMap_[clientID]->innerRekeyCount_++;
         break;
      }

      case ZMQ_MSGTYPE_AEAD_ENCINIT:
      {
         //process client encinit
         if (socketConnMap_[clientID]->encData_->processEncinit(dataBdr.getPtr()
            , dataBdr.getSize(), false) != 0)
         {
            //failed to init handshake, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCINIT processing failed");
            return false;
         }

         //return encack
         BinaryData encackData(BIP151PUBKEYSIZE);
         if (socketConnMap_[clientID]->encData_->getEncackData(encackData.getPtr()
            , BIP151PUBKEYSIZE) != 0)
         {
            //failed to init handshake, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_ENCACK data not obtained");
            return false;
         }

         if (!writeToClient(ZMQ_MSGTYPE_AEAD_ENCACK, encackData.getRef()
            , false))
         {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AEAD_ENCACK not sent");
         }

         socketConnMap_[clientID]->bip151HandshakeCompleted_ = true;
         break;
      }

      case ZMQ_MSGTYPE_AUTH_CHALLENGE:
      {
         bool goodChallenge = true;
         auto challengeResult =
            socketConnMap_[clientID]->encData_->processAuthchallenge(
            dataBdr.getPtr(), dataBdr.getSize(), true); //true: step #1 of 6

         if (challengeResult == -1)
         {
            //auth fail, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_CHALLENGE processing failed");
            return false;
         }
         else if (challengeResult == 1)
         {
            goodChallenge = false;
         }

         BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
         if (socketConnMap_[clientID]->encData_->getAuthreplyData(
            authreplyBuf.getPtr(), authreplyBuf.getSize()
            , true //true: step #2 of 6
            , goodChallenge) == -1)
         {
            //auth setup failure, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_REPLY data not obtained");
            return false;
         }

         if (!writeToClient(ZMQ_MSGTYPE_AUTH_REPLY, authreplyBuf.getRef()
            , true))
         {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_REPLY not sent");
            return false;
         }

         break;
      }

      case ZMQ_MSGTYPE_AUTH_PROPOSE:
      {
         bool goodPropose = true;
         auto proposeResult =
            socketConnMap_[clientID]->encData_->processAuthpropose(
            dataBdr.getPtr(), dataBdr.getSize());

         if (proposeResult == -1)
         {
            //auth setup failure, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_PROPOSE processing failed");
            return false;
         }
         else if (proposeResult == 1)
         {
            goodPropose = false;
         }
         else
         {
            //keep track of the propose check state
            socketConnMap_[clientID]->encData_->setGoodPropose();
         }

         BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
         if (socketConnMap_[clientID]->encData_->getAuthchallengeData(
            authchallengeBuf.getPtr(), authchallengeBuf.getSize()
            , "" //empty string, use chosen key from processing auth propose
            , false //false: step #4 of 6
            , goodPropose) == -1)
         {
            //auth setup failure, kill connection
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_CHALLENGE data not obtained");
            return false;
         }

         if (!writeToClient(ZMQ_MSGTYPE_AUTH_CHALLENGE
            , authchallengeBuf.getRef(), true))
         {
            logger_->error("[processHandshake] BIP 150/151 handshake process "
               "failed - AUTH_CHALLENGE not sent");
         }

         break;
      }

      case ZMQ_MSGTYPE_AUTH_REPLY:
      {
         if (socketConnMap_[clientID]->encData_->processAuthreply(dataBdr.getPtr()
            , dataBdr.getSize(), false
            , socketConnMap_[clientID]->encData_->getProposeFlag()) != 0)
         {
            //invalid auth setup, kill connection
            return false;
         }

         //rekey after succesful BIP150 handshake
         socketConnMap_[clientID]->encData_->bip150HandshakeRekey();
         socketConnMap_[clientID]->bip150HandshakeCompleted_ = true;
         break;
      }

      default:
         logger_->error("[processHandshake] Unknown message type.");
         return false;
      }

      return true;
   };

   bool retVal = processHandshake();
   if (!retVal)
   {
      logger_->error("[{}] BIP 150/151 handshake process failed.", __func__);
   }
   return retVal;
}

// Function used to reset the BIP 150/151 handshake data. Called when a
// connection is shut down.
//
// INPUT:  The client ID. (const string&)
// OUTPUT: None
// RETURN: None
void ZmqBIP15XServerConnection::resetBIP151Connection(const string& clientID)
{
   if (socketConnMap_[clientID] != nullptr)
   {
      socketConnMap_[clientID]->reset();
   }
   else
   {
      BinaryData hexID(clientID);
      logger_->error("[{}] Client ID {} does not exist.", __func__
         , hexID.toHexStr());
   }
}

// Function used to set the BIP 150/151 handshake data. Called when a connection
// is created.
//
// INPUT:  The client ID. (const string&)
// OUTPUT: None
// RETURN: None
void ZmqBIP15XServerConnection::setBIP151Connection(const string& clientID)
{
   if (socketConnMap_[clientID] == nullptr) {
      assert(cbTrustedClients_);
      auto lbds = getAuthPeerLambda();
      for (auto b : cbTrustedClients_()) {
         const auto colonIndex = b.find(':');
         if (colonIndex == std::string::npos) {
            logger_->error("[{}] Trusted client list is malformed (for {})."
               , __func__, b);
            return;
         }
         const auto keyName = b.substr(0, colonIndex);
         SecureBinaryData inKey = READHEX(b.substr(colonIndex + 1));
         if (inKey.isNull()) {
            logger_->error("[{}] Trusted client key for {} is malformed."
               , __func__, keyName);
            return;
         }

         try {
            authPeers_->addPeer(inKey, vector<string>{ keyName });
         }
         catch (const std::exception &e) {
            logger_->error("[{}] Trusted client key {} [{}] for {} is malformed: {}"
               , __func__, inKey.toHexStr(), inKey.getSize(), keyName, e.what());
            return;
         }
      }

      {
         std::unique_lock<std::mutex> lock(clientsMtx_);
         socketConnMap_[clientID] = make_unique<ZmqBIP15XPerConnData>();
         socketConnMap_[clientID]->encData_ = make_unique<BIP151Connection>(lbds);
         socketConnMap_[clientID]->outKeyTimePoint_ = chrono::steady_clock::now();
      }
      notifyListenerOnNewConnection(clientID);
   }
   else {
      BinaryData hexID(clientID);
      logger_->error("[{}] Client ID {} already exists.", __func__
         , hexID.toHexStr());
   }
}

// Get lambda functions related to authorized peers. Copied from Armory.
//
// INPUT:  None
// OUTPUT: None
// RETURN: AuthPeersLambdas object with required lambdas.
AuthPeersLambdas ZmqBIP15XServerConnection::getAuthPeerLambda()
{
   auto authPeerPtr = authPeers_;

   auto getMap = [authPeerPtr](void)->const map<string, btc_pubkey>&
   {
      return authPeerPtr->getPeerNameMap();
   };

   auto getPrivKey = [authPeerPtr](
      const BinaryDataRef& pubkey)->const SecureBinaryData&
   {
      return authPeerPtr->getPrivateKey(pubkey);
   };

   auto getAuthSet = [authPeerPtr](void)->const set<SecureBinaryData>&
   {
      return authPeerPtr->getPublicKeySet();
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}

// Generate a cookie with the signer's identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: True if success, false if failure.
bool ZmqBIP15XServerConnection::genBIPIDCookie()
{
   const string absCookiePath = SystemFilePaths::appDataLocation() + "/"
      + idCookieNamek;
   if (SystemFileUtils::fileExist(absCookiePath)) {
      if (!SystemFileUtils::rmFile(absCookiePath)) {
         logger_->error("[{}] Unable to delete server identity cookie ({}). "
            "Will not write a new cookie.", __func__, absCookiePath);
         return false;
      }
   }

   // Ensure that we only write the compressed key.
   ofstream cookieFile(absCookiePath, ios::out | ios::binary);
   const BinaryData ourIDKey = getOwnPubKey();
   if (ourIDKey.getSize() != BTC_ECKEY_COMPRESSED_LENGTH) {
      logger_->error("[{}] Server identity key ({}) is uncompressed. Will not "
         "write the identity cookie.", __func__, absCookiePath);
      return false;
   }

   logger_->debug("[{}] Writing a new server identity cookie ({}).", __func__
      ,  absCookiePath);
   cookieFile.write(getOwnPubKey().getCharPtr(), BTC_ECKEY_COMPRESSED_LENGTH);
   cookieFile.close();
   bipIDCookieExists_ = true;

   return true;
}

void ZmqBIP15XServerConnection::addAuthPeer(const BinaryData& inKey
   , const std::string& keyName)
{
   if (!(CryptoECDSA().VerifyPublicKeyValid(inKey))) {
      logger_->error("[{}] BIP 150 authorized key ({}) for user {} is invalid."
         , __func__,  inKey.toHexStr(), keyName);
      return;
   }
   authPeers_->addPeer(inKey, vector<string>{ keyName });
}

// Get the client's identity public key. Intended for use with local clients.
//
// INPUT:  The accompanying key IP:Port or name. (const string)
// OUTPUT: The buffer that will hold the compressed ID key. (BinaryData)
// RETURN: True if success, false if failure.
bool ZmqBIP15XServerConnection::getClientIDCookie(BinaryData& cookieBuf
   , const string& cookieName)
{
   if (!useClientIDCookie_) {
      logger_->error("[{}] Server identity cookie requested despite not being "
         "available.", __func__);
      return false;
   }

   const string absCookiePath = SystemFilePaths::appDataLocation() + "/"
      + cookieName;
   if (!SystemFileUtils::fileExist(absCookiePath)) {
      logger_->error("[{}] Server identity cookie ({}) doesn't exist. Unable "
         "to verify server identity.", __func__, absCookiePath);
      return false;
   }

   // Ensure that we only read a compressed key.
   ifstream cookieFile(absCookiePath, ios::in | ios::binary);
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

// Get the server's compressed BIP 150 identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: A buffer with the compressed ECDSA ID pub key. (BinaryData)
BinaryData ZmqBIP15XServerConnection::getOwnPubKey() const
{
   const auto pubKey = authPeers_->getOwnPublicKey();
   return SecureBinaryData(pubKey.pubkey, pubKey.compressed
      ? BTC_ECKEY_COMPRESSED_LENGTH : BTC_ECKEY_UNCOMPRESSED_LENGTH);
}
