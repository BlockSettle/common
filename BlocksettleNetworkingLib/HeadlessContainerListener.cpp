#include "HeadlessContainerListener.h"
#include <spdlog/spdlog.h>
#include "CheckRecipSigner.h"
#include "ConnectionManager.h"
#include "CoreHDWallet.h"
#include "CoreWalletsManager.h"
#include "WalletEncryption.h"
#include "ZmqSecuredServerConnection.h"


using namespace Blocksettle::Communication;

HeadlessContainerListener::HeadlessContainerListener(const std::shared_ptr<ServerConnection> &conn
   , const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::core::WalletsManager> &walletsMgr
   , const std::string &walletsPath, NetworkType netType
   , bool wo, const bool &hasUI, const bool &backupEnabled)
   : QObject(nullptr), ServerConnectionListener()
   , connection_(conn)
   , logger_(logger)
   , walletsMgr_(walletsMgr)
   , walletsPath_(walletsPath)
   , backupPath_(walletsPath + "/../backup")
   , netType_(netType)
   , watchingOnly_(wo)
   , hasUI_(hasUI)
   , backupEnabled_(backupEnabled)
{
   connect(this, &HeadlessContainerListener::xbtSpent, this, &HeadlessContainerListener::onXbtSpent);
}

HeadlessContainerListener::~HeadlessContainerListener() noexcept
{
   if (!connection_) {
      return;
   }
   disconnect();
}

bool HeadlessContainerListener::disconnect(const std::string &clientId)
{
   headless::RequestPacket packet;
   packet.set_data("");
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(headless::DisconnectionRequestType);
   const auto &serializedPkt = packet.SerializeAsString();

   bool rc = sendData(serializedPkt, clientId);
   if (rc && !clientId.empty()) {
      OnClientDisconnected(clientId);
   }
   if (clientId.empty()) {
      authTickets_.clear();
   }
   else {
      authTickets_.erase(clientId);
   }
   return rc;
}

bool HeadlessContainerListener::sendData(const std::string &data, const std::string &clientId)
{
   bool sentOk = false;
   if (clientId.empty()) {
      for (const auto &clientId : connectedClients_) {
         if (connection_->SendDataToClient(clientId, data)) {
            sentOk = true;
         }
      }
   }
   else {
      sentOk = connection_->SendDataToClient(clientId, data);
   }
   return sentOk;
}

void HeadlessContainerListener::SetLimits(const SignContainer::Limits &limits)
{
   limits_ = limits;
}

static NetworkType mapNetworkType(headless::NetworkType netType)
{
   switch (netType) {
   case headless::MainNetType:   return NetworkType::MainNet;
   case headless::TestNetType:   return NetworkType::TestNet;
   default:    return NetworkType::Invalid;
   }
}

static std::string toHex(const std::string &binData)
{
   return BinaryData(binData).toHexStr();
}

void HeadlessContainerListener::OnClientConnected(const std::string &clientId)
{
   logger_->debug("[HeadlessContainerListener] client {} connected", toHex(clientId));
}

void HeadlessContainerListener::OnClientDisconnected(const std::string &clientId)
{
   logger_->debug("[HeadlessContainerListener] client {} disconnected", toHex(clientId));
   emit clientDisconnected(clientId);
}

void HeadlessContainerListener::OnDataFromClient(const std::string &clientId, const std::string &data)
{
   headless::RequestPacket packet;
   if (!packet.ParseFromString(data)) {
      logger_->error("[{}] failed to parse request packet", __func__);
      return;
   }

   const auto authTkt = authTicket(clientId);
   if ((packet.type() != headless::AuthenticationRequestType)
      && (authTkt.isNull() || (SecureBinaryData(packet.authticket()) != authTkt))) {
      if (packet.authticket().empty() && authTkt.isNull()) {
         logger_->info("[HeadlessContainerListener] request {} ignored due to empty auth ticket", packet.type());
      }
      else {
         logger_->error("[HeadlessContainerListener] auth ticket mismatch for {}!", toHex(clientId));
      }
      disconnect(clientId);
      return;
   }

   if (packet.type() == headless::AuthenticationRequestType) {
      if (!authTkt.isNull()) {
         logger_->warn("[HeadlessContainerListener] {} already authenticated", toHex(clientId));
//         AuthResponse(clientId, packet, "already authenticated");
      }

      headless::AuthenticationRequest request;
      if (!request.ParseFromString(packet.data())) {
         logger_->error("[HeadlessContainerListener] failed to parse auth request");
         AuthResponse(clientId, packet, "Failed to parse request");
         return;
      }
      if (mapNetworkType(request.nettype()) != netType_) {
         logger_->warn("[HeadlessContainerListener] remote network type mismatch");
         AuthResponse(clientId, packet, "Wrong Bitcoin network type");
         return;
      }

      const auto authTicket = CryptoPRNG::generateRandom(8);
      logger_->debug("[HeadlessContainerListener] setting authTicket {} for {}", authTicket.toHexStr(), toHex(clientId));
      authTickets_[clientId] = authTicket;
      AuthResponse(clientId, packet);
   }
   else {
      if (!onRequestPacket(clientId, packet)) {
         packet.set_data("");
         sendData(packet.SerializeAsString(), clientId);
      }
   }
}

void HeadlessContainerListener::OnPeerConnected(const std::string &ip)
{
   emit peerConnected(QString::fromStdString(ip));
}

void HeadlessContainerListener::OnPeerDisconnected(const std::string &ip)
{
   emit peerDisconnected(QString::fromStdString(ip));
}

SecureBinaryData HeadlessContainerListener::authTicket(const std::string &clientId) const
{
   const auto itTicket = authTickets_.find(clientId);
   if (itTicket == authTickets_.end()) {
      return {};
   }
   return itTicket->second;
}

void HeadlessContainerListener::AuthResponse(const std::string &clientId, headless::RequestPacket packet
   , const std::string &errMsg)
{
   headless::AuthenticationReply response;
   if (errMsg.empty()) {
      response.set_authticket(authTicket(clientId).toBinStr());
      if (hasUI_) {
         response.set_hasui(true);
      }
   }
   else {
      response.set_error(errMsg);
   }
   response.set_nettype((netType_ == NetworkType::TestNet) ? headless::TestNetType : headless::MainNetType);
   packet.set_data(response.SerializeAsString());
   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send response auth packet");
      return;
   }
   logger_->info("[HeadlessContainerListener] client {} authenticated", toHex(clientId));
   connectedClients_.insert(clientId);
   emit clientAuthenticated(clientId, connection_->GetClientInfo(clientId));
}

bool HeadlessContainerListener::isRequestAllowed(Blocksettle::Communication::headless::RequestType reqType) const
{
   if (watchingOnly_) {
      switch (reqType) {
      case headless::CancelSignTxRequestType:
      case headless::SignTXRequestType:
      case headless::SignPartialTXRequestType:
      case headless::SignPayoutTXRequestType:
      case headless::SignTXMultiRequestType:
      case headless::PasswordRequestType:
      case headless::CreateHDWalletRequestType:
      case headless::GetRootKeyRequestType:
      case headless::SetLimitsRequestType:
      case headless::ChangePasswordRequestType:
         return false;
      default:    break;
      }
   }
   return true;
}

bool HeadlessContainerListener::onRequestPacket(const std::string &clientId, headless::RequestPacket packet)
{
   if (!isRequestAllowed(packet.type())) {
      logger_->info("[{}] request {} is not applicable at this state", __func__, (int)packet.type());
      return false;
   }

   switch (packet.type()) {
   case headless::HeartbeatType:
      packet.set_data({});
      if (!sendData(packet.SerializeAsString(), clientId)) {
         logger_->error("[HeadlessContainerListener] failed to send response hearbeat packet");
         return false;
      }
      break;

   case headless::CancelSignTxRequestType:
      return onCancelSignTx(clientId, packet);

   case headless::SignTXRequestType:
      return onSignTXRequest(clientId, packet);

   case headless::SignPartialTXRequestType:
      return onSignTXRequest(clientId, packet, true);

   case headless::SignPayoutTXRequestType:
      return onSignPayoutTXRequest(clientId, packet);

   case headless::SignTXMultiRequestType:
      return onSignMultiTXRequest(clientId, packet);

   case headless::PasswordRequestType:
      return onPasswordReceived(clientId, packet);

   case headless::SetUserIdRequestType:
      return onSetUserId(clientId, packet);

   case headless::CreateHDWalletRequestType:
      return onCreateHDWallet(clientId, packet);

   case headless::DeleteHDWalletRequestType:
      return onDeleteHDWallet(packet);

   case headless::GetRootKeyRequestType:
      return onGetRootKey(clientId, packet);

   case headless::SetLimitsRequestType:
      return onSetLimits(clientId, packet);

   case headless::GetHDWalletInfoRequestType:
      return onGetHDWalletInfo(clientId, packet);

   case headless::ChangePasswordRequestType:
      return onChangePassword(clientId, packet);

   case headless::DisconnectionRequestType:
      emit OnClientDisconnected(clientId);
      break;

   case headless::SyncWalletInfoType:
      return onSyncWalletInfo(clientId, packet);

   case headless::SyncHDWalletType:
      return onSyncHDWallet(clientId, packet);

   case headless::SyncWalletType:
      return onSyncWallet(clientId, packet);

   case headless::SyncCommentType:
      return onSyncComment(clientId, packet);

   case headless::SyncAddressesType:
      return onSyncAddresses(clientId, packet);

   default:
      logger_->error("[HeadlessContainerListener] unknown request type {}", packet.type());
      return false;
   }
   return true;
}

bool HeadlessContainerListener::onSignTXRequest(const std::string &clientId, const headless::RequestPacket &packet, bool partial)
{
   const auto reqType = partial ? headless::SignPartialTXRequestType : headless::SignTXRequestType;
   headless::SignTXRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse SignTXRequest");
      SignTXResponse(clientId, packet.id(), reqType, "failed to parse");
      return false;
   }
   uint64_t inputVal = 0;
   bs::core::wallet::TXSignRequest txSignReq;
   txSignReq.walletId = request.walletid();
   for (int i = 0; i < request.inputs_size(); i++) {
      UTXO utxo;
      utxo.unserialize(request.inputs(i));
      if (utxo.isInitialized()) {
         txSignReq.inputs.push_back(utxo);
         inputVal += utxo.getValue();
      }
   }

   uint64_t outputVal = 0;
   for (int i = 0; i < request.recipients_size(); i++) {
      BinaryData serialized = request.recipients(i);
      const auto recip = ScriptRecipient::deserialize(serialized);
      txSignReq.recipients.push_back(recip);
      const auto outAddr = bs::Address::fromRecipient(recip);
      outputVal += recip->getValue();
   }
   uint64_t value = outputVal;

   txSignReq.fee = request.fee();
   txSignReq.RBF = request.rbf();

   if (!request.unsignedstate().empty()) {
      const BinaryData prevState(request.unsignedstate());
      txSignReq.prevStates.push_back(prevState);
      if (!value) {
         bs::CheckRecipSigner signer(prevState);
         value = signer.spendValue();
         if (txSignReq.change.value) {
            value -= txSignReq.change.value;
         }
      }
   }

   if (request.has_change()) {
      txSignReq.change.address = request.change().address();
      txSignReq.change.index = request.change().index();
      txSignReq.change.value = request.change().value();
   }

   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainerListener] invalid SignTXRequest");
      SignTXResponse(clientId, packet.id(), reqType, "missing critical data");
      return false;
   }

   txSignReq.populateUTXOs = request.populateutxos();

   const auto wallet = walletsMgr_->getWalletById(txSignReq.walletId);
   if (!wallet) {
      logger_->error("[HeadlessContainerListener] failed to find wallet {}", txSignReq.walletId);
      SignTXResponse(clientId, packet.id(), reqType, "failed to find wallet " + txSignReq.walletId);
      return false;
   }
   const auto rootWalletId = walletsMgr_->getHDRootForLeaf(txSignReq.walletId)->walletId();

   if ((wallet->type() == bs::core::wallet::Type::Bitcoin)
      && !CheckSpendLimit(value, request.applyautosignrules(), rootWalletId)) {
      SignTXResponse(clientId, packet.id(), reqType, "spend limit exceeded");
      return false;
   }

   const auto onPassword = [this, wallet, txSignReq, rootWalletId, clientId, id = packet.id(), partial
      , reqType, value, autoSign = request.applyautosignrules()
      , keepDuplicatedRecipients = request.keepduplicatedrecipients()] (const SecureBinaryData &pass,
            bool cancelledByUser) {
      try {
         if (!wallet->encryptionTypes().empty() && pass.isNull()) {
            logger_->error("[HeadlessContainerListener] empty password for wallet {}", wallet->name());
            SignTXResponse(clientId, id, reqType, "missing password for encrypted wallet", {}, cancelledByUser);
            return;
         }
         const auto tx = partial ? wallet->signPartialTXRequest(txSignReq, pass)
            : wallet->signTXRequest(txSignReq, pass, keepDuplicatedRecipients);
         SignTXResponse(clientId, id, reqType, {}, tx, cancelledByUser);
         emit xbtSpent(value, autoSign);
      }
      catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener] failed to sign {} TX request: {}", partial ? "partial" : "full", e.what());
         SignTXResponse(clientId, id, reqType, std::string("failed to sign: ") + e.what());
         passwords_.erase(wallet->walletId());
         passwords_.erase(rootWalletId);
         emit autoSignDeactivated(rootWalletId);
      }
   };

   if (!request.password().empty()) {
      onPassword(BinaryData::CreateFromHex(request.password()), false);
      return true;
   }

   const QString prompt = tr("Outgoing %1Transaction").arg(partial ? tr("Partial ") : tr(""));
   return RequestPasswordIfNeeded(clientId, txSignReq, prompt, onPassword, request.applyautosignrules());
}

bool HeadlessContainerListener::onCancelSignTx(const std::string &, headless::RequestPacket packet)
{
   headless::CancelSignTx request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse CancelSignTx");
      return false;
   }

   emit cancelSignTx(request.txid());

   return true;
}

bool HeadlessContainerListener::onSignPayoutTXRequest(const std::string &clientId, const headless::RequestPacket &packet)
{
   const auto reqType = headless::SignPayoutTXRequestType;
   headless::SignPayoutTXRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse SignPayoutTXRequest");
      SignTXResponse(clientId, packet.id(), reqType, "failed to parse");
      return false;
   }

   const auto settlWallet = std::dynamic_pointer_cast<bs::core::SettlementWallet>(walletsMgr_->getSettlementWallet());
   if (!settlWallet) {
      logger_->error("[HeadlessContainerListener] Settlement wallet is missing");
      SignTXResponse(clientId, packet.id(), reqType, "no settlement wallet");
      return false;
   }

   const auto &authWallet = walletsMgr_->getAuthWallet();
   if (!authWallet) {
      logger_->error("[HeadlessContainerListener] Auth wallet is missing");
      SignTXResponse(clientId, packet.id(), reqType, "no auth wallet");
      return false;
   }

   bs::core::wallet::TXSignRequest txSignReq;
   txSignReq.walletId = authWallet->walletId();
   UTXO utxo;
   utxo.unserialize(request.input());
   if (utxo.isInitialized()) {
      txSignReq.inputs.push_back(utxo);
   }

   BinaryData serialized = request.recipient();
   const auto recip = ScriptRecipient::deserialize(serialized);
   txSignReq.recipients.push_back(recip);

   const bs::Address authAddr(request.authaddress());
   const BinaryData settlementId = request.settlementid();

   const auto rootWalletId = walletsMgr_->getHDRootForLeaf(authWallet->walletId())->walletId();

   const auto onAuthPassword = [this, clientId, id = packet.id(), txSignReq, authWallet, authAddr
      , settlWallet, settlementId, reqType, rootWalletId](const SecureBinaryData &pass,
            bool cancelledByUser) {
      if (!authWallet->encryptionTypes().empty() && pass.isNull()) {
         logger_->error("[HeadlessContainerListener] no password for encrypted auth wallet");
         SignTXResponse(clientId, id, reqType, "password required, but empty received");
      }

      const auto authKeys = authWallet->getKeyPairFor(authAddr, pass);
      if (authKeys.privKey.isNull() || authKeys.pubKey.isNull()) {
         logger_->error("[HeadlessContainerListener] failed to get priv/pub keys for {}", authAddr.display<std::string>());
         SignTXResponse(clientId, id, reqType, "no auth priv/pub keys found");
         passwords_.erase(authWallet->walletId());
         passwords_.erase(rootWalletId);
         emit autoSignDeactivated(rootWalletId);
         return;
      }

      const auto onSettlPassword = [this, clientId, id, txSignReq, authKeys, settlWallet, settlementId
         , reqType](const std::string &pass, bool cancelledByUser) {
         try {
            const auto tx = settlWallet->signPayoutTXRequest(txSignReq, authKeys, settlementId);
            SignTXResponse(clientId, id, reqType, {}, tx, cancelledByUser);
         }
         catch (const std::exception &e) {
            logger_->error("[HeadlessContainerListener] failed to sign PayoutTX request: {}", e.what());
            SignTXResponse(clientId, id, reqType, std::string("failed to sign: ") + e.what());
         }
      };
      onSettlPassword({}, cancelledByUser);
   };

   if (!request.password().empty()) {
      onAuthPassword(BinaryData::CreateFromHex(request.password()), false);
      return true;
   }

   const QString prompt = tr("Signing pay-out transaction for %1 XBT:\n"
      "  Settlement ID: %2"
   ).arg(QString::number(utxo.getValue() / BTCNumericTypes::BalanceDivider, 'f', 8))
      .arg(QString::fromStdString(settlementId.toHexStr()));

   return RequestPasswordIfNeeded(clientId, txSignReq, prompt, onAuthPassword, request.applyautosignrules());
}

bool HeadlessContainerListener::onSignMultiTXRequest(const std::string &clientId, const headless::RequestPacket &packet)
{
   const auto reqType = headless::SignTXMultiRequestType;
   headless::SignTXMultiRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse SignTXMultiRequest");
      SignTXResponse(clientId, packet.id(), reqType, "failed to parse");
      return false;
   }

   bs::core::wallet::TXMultiSignRequest txMultiReq;
   bs::core::WalletMap walletMap;
   txMultiReq.prevState = request.signerstate();
   for (int i = 0; i < request.walletids_size(); i++) {
      const auto &wallet = walletsMgr_->getWalletById(request.walletids(i));
      if (!wallet) {
         logger_->error("[HeadlessContainerListener] failed to find wallet with id {}", request.walletids(i));
         SignTXResponse(clientId, packet.id(), reqType, "failed to find wallet " + request.walletids(i));
         return false;
      }
      walletMap[wallet->walletId()] = wallet;
   }

   QString prompt = tr("Signing multi-wallet input (auth revoke) transaction");

   const auto cbOnAllPasswords = [this, txMultiReq, walletMap, reqType, clientId, id=packet.id()]
                                 (const std::unordered_map<std::string, SecureBinaryData> &walletPasswords) {
      try {
         const auto tx = bs::core::SignMultiInputTX(txMultiReq, walletPasswords, walletMap);
         SignTXResponse(clientId, id, reqType, {}, tx);
      }
      catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener] failed to sign multi TX request: {}", e.what());
         SignTXResponse(clientId, id, reqType, std::string("failed to sign: ") + e.what());
      }
   };
   return RequestPasswordsIfNeeded(++reqSeqNo_, clientId, txMultiReq, walletMap, prompt, cbOnAllPasswords);
}

void HeadlessContainerListener::SignTXResponse(const std::string &clientId, unsigned int id, headless::RequestType reqType
   , const std::string &error, const BinaryData &tx, bool cancelledByUser)
{
   headless::SignTXReply response;
   if (tx.isNull()) {
      response.set_error(error);
   }
   else {
      response.set_signedtx(tx.toBinStr());
   }
   response.set_cancelledbyuser(cancelledByUser);

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(reqType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send response signTX packet");
   }
   if (!tx.isNull()) {
      emit txSigned();
   }
}

bool HeadlessContainerListener::onPasswordReceived(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::PasswordReply response;
   if (!response.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse PasswordReply");
      return false;
   }
   if (response.walletid().empty()) {
      logger_->error("[HeadlessContainerListener] walletId is empty in PasswordReply");
      return false;
   }
   const auto password = BinaryData::CreateFromHex(response.password());
   if (!password.isNull()) {
      passwords_[response.walletid()] = password;
   }

   passwordReceived(clientId, response.walletid(), password, response.cancelledbyuser());
   return true;
}

void HeadlessContainerListener::passwordReceived(const std::string &clientId, const std::string &walletId,
   const SecureBinaryData &password, bool cancelledByUser)
{
   const auto cbsIt = passwordCallbacks_.find(walletId);
   if (cbsIt != passwordCallbacks_.end()) {
      for (const auto &cb : cbsIt->second) {
         cb(password, cancelledByUser);
      }
      passwordCallbacks_.erase(cbsIt);
   }
   if (autoSignPwdReqs_.find(walletId) != autoSignPwdReqs_.end()) {
      autoSignPwdReqs_.erase(walletId);
      if (clientId.empty()) {
         for (const auto &authTicket : authTickets_) {
            activateAutoSign(authTicket.first, walletId, password);
         }
      }
      else {
         activateAutoSign(clientId, walletId, password);
      }
   }
}

void HeadlessContainerListener::passwordReceived(const std::string &walletId,
   const SecureBinaryData &password, bool cancelledByUser)
{
   passwordReceived({}, walletId, password, cancelledByUser);
}

bool HeadlessContainerListener::RequestPasswordIfNeeded(const std::string &clientId
   , const bs::core::wallet::TXSignRequest &txReq
   , const QString &prompt, const PasswordReceivedCb &cb, bool autoSign)
{
   const auto &wallet = walletsMgr_->getWalletById(txReq.walletId);
   if (!wallet) {
      return false;
   }
   bool needPassword = !wallet->encryptionTypes().empty();
   SecureBinaryData password;
   std::string walletId = wallet->walletId();
   if (needPassword && autoSign) {
      const auto &hdRoot = walletsMgr_->getHDRootForLeaf(walletId);
      if (hdRoot) {
         walletId = hdRoot->walletId();
      }
      const auto passwordIt = passwords_.find(walletId);
      if (passwordIt != passwords_.end()) {
         needPassword = false;
         password = passwordIt->second;
      }
   }
   if (!needPassword) {
      if (cb) {
         cb(password, false);
      }
      return true;
   }

   return RequestPassword(clientId, txReq, prompt, cb);
}

bool HeadlessContainerListener::RequestPasswordsIfNeeded(int reqId, const std::string &clientId
   , const bs::core::wallet::TXMultiSignRequest &txMultiReq, const bs::core::WalletMap &walletMap
   , const QString &prompt, const PasswordsReceivedCb &cb)
{
   TempPasswords tempPasswords;
   for (const auto &wallet : walletMap) {
      const auto &walletId = wallet.first;
      const auto &rootWallet = walletsMgr_->getHDRootForLeaf(walletId);
      const auto &rootWalletId = rootWallet->walletId();

      tempPasswords.rootLeaves[rootWalletId].insert(walletId);
      tempPasswords.reqWalletIds.insert(walletId);

      if (!rootWallet->encryptionTypes().empty()) {
         const auto cbWalletPass = [this, reqId, cb, rootWalletId](const SecureBinaryData &password, bool) {
            auto &tempPasswords = tempPasswords_[reqId];
            const auto &walletsIt = tempPasswords.rootLeaves.find(rootWalletId);
            if (walletsIt == tempPasswords.rootLeaves.end()) {
               return;
            }
            for (const auto &walletId : walletsIt->second) {
               tempPasswords.passwords[walletId] = password;
            }
            if (tempPasswords.passwords.size() == tempPasswords.reqWalletIds.size()) {
               cb(tempPasswords.passwords);
               tempPasswords_.erase(reqId);
            }
         };

         bs::core::wallet::TXSignRequest txReq;
         txReq.walletId = rootWallet->walletId();
         RequestPassword(clientId, txReq, prompt, cbWalletPass);
      }
      else {
         tempPasswords.passwords[walletId] = {};
      }
   }
   if (tempPasswords.reqWalletIds.size() == tempPasswords.passwords.size()) {
      cb(tempPasswords.passwords);
   }
   else {
      tempPasswords_[reqId] = tempPasswords;
   }
   return true;
}

bool HeadlessContainerListener::RequestPassword(const std::string &clientId, const bs::core::wallet::TXSignRequest &txReq
   , const QString &prompt, const PasswordReceivedCb &cb)
{
   if (cb) {
      auto &callbacks = passwordCallbacks_[txReq.walletId];
      callbacks.push_back(cb);
      if (callbacks.size() > 1) {
         return true;
      }
   }

   if (hasUI_) {
      emit passwordRequired(txReq, prompt);
      return true;
   }
   else {
      headless::PasswordRequest request;
      if (!prompt.isEmpty()) {
         request.set_prompt(prompt.toStdString());
      }
      if (!txReq.walletId.empty()) {
         request.set_walletid(txReq.walletId);
         const auto &wallet = walletsMgr_->getWalletById(txReq.walletId);
         std::vector<bs::wallet::EncryptionType> encTypes;
         std::vector<SecureBinaryData> encKeys;
         bs::wallet::KeyRank keyRank = { 0, 0 };
         if (wallet) {
            encTypes = wallet->encryptionTypes();
            encKeys = wallet->encryptionKeys();
            keyRank = wallet->encryptionRank();
         }
         else {
            const auto &rootWallet = walletsMgr_->getHDWalletById(txReq.walletId);
            if (rootWallet) {
               encTypes = rootWallet->encryptionTypes();
               encKeys = rootWallet->encryptionKeys();
               keyRank = rootWallet->encryptionRank();
            }
         }

         for (const auto &encType : encTypes) {
            request.add_enctypes(static_cast<uint32_t>(encType));
         }
         for (const auto &encKey : encKeys) {
            request.add_enckeys(encKey.toBinStr());
         }
         request.set_rankm(keyRank.first);
      }

      headless::RequestPacket packet;
      packet.set_authticket(authTicket(clientId).toBinStr());
      packet.set_type(headless::PasswordRequestType);
      packet.set_data(request.SerializeAsString());
      return sendData(packet.SerializeAsString(), clientId);
   }
}

bool HeadlessContainerListener::onSetUserId(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::SetUserIdRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse SetUserIdRequest");
      return false;
   }

   walletsMgr_->setChainCode(request.userid());

   headless::RequestPacket response;
   response.set_authticket(authTicket(clientId).toBinStr());
   response.set_type(headless::SetUserIdRequestType);
   sendData(response.SerializeAsString(), clientId);
   return true;
}

bool HeadlessContainerListener::CreateHDLeaf(const std::string &clientId, unsigned int id, const headless::NewHDLeaf &request
   , const std::vector<bs::wallet::PasswordData> &pwdData)
{
   const auto hdWallet = walletsMgr_->getHDWalletById(request.rootwalletid());
   if (!hdWallet) {
      logger_->error("[HeadlessContainerListener] failed to find root HD wallet by id {}", request.rootwalletid());
      CreateHDWalletResponse(clientId, id, "no root HD wallet");
      return false;
   }
   const auto path = bs::hd::Path::fromString(request.path());
   if ((path.length() != 3) || !path.isAbolute()) {
      logger_->error("[HeadlessContainerListener] invalid path {} at HD wallet creation", request.path());
      CreateHDWalletResponse(clientId, id, "invalid path");
      return false;
   }

   SecureBinaryData password;
   for (const auto &pwd : pwdData) {
      password = mergeKeys(password, pwd.password);
   }

   if (backupEnabled_) {
      walletsMgr_->backupWallet(hdWallet, backupPath_);
   }

   const auto onPassword = [this, hdWallet, path, clientId, id](const SecureBinaryData &pass,
         bool cancelledByUser) {
      std::shared_ptr<bs::core::hd::Node> leafNode;
      if (!hdWallet->encryptionTypes().empty() && pass.isNull()) {
         logger_->error("[HeadlessContainerListener] no password for encrypted wallet");
         CreateHDWalletResponse(clientId, id, "password required, but empty received");
      }
      const auto &rootNode = hdWallet->getRootNode(pass);
      if (rootNode) {
         leafNode = rootNode->derive(path);
      } else {
         logger_->error("[HeadlessContainerListener] failed to decrypt root node");
         CreateHDWalletResponse(clientId, id, "root node decryption failed");
      }

      if (leafNode) {
         leafNode->clearPrivKey();

         const auto groupIndex = static_cast<bs::hd::CoinType>(path.get(1));
         auto group = hdWallet->getGroup(groupIndex);
         if (!group) {
            group = hdWallet->createGroup(groupIndex);
         }
         const auto leafIndex = path.get(2);
         auto leaf = group->createLeaf(leafIndex, leafNode);
         if (!leaf || (leaf != group->getLeaf(leafIndex))) {
            logger_->error("[HeadlessContainerListener] failed to create/get leaf {}", path.toString());
            CreateHDWalletResponse(clientId, id, "failed to create leaf");
            return;
         }

         CreateHDWalletResponse(clientId, id, leaf->walletId()
         , leafNode->pubCompressedKey(), leafNode->chainCode());
      }
      else {
         logger_->error("[HeadlessContainerListener] failed to create HD leaf");
         CreateHDWalletResponse(clientId, id, "failed to derive");
      }
   };

   if (!hdWallet->encryptionTypes().empty()) {
      if (!password.isNull()) {
         onPassword(password, false);
      }
      else {
         bs::core::wallet::TXSignRequest txReq;
         txReq.walletId = hdWallet->walletId();
         return RequestPassword(clientId, txReq, tr("Creating a wallet %1").arg(QString::fromStdString(txReq.walletId)), onPassword);
      }
   }
   else {
      onPassword({}, false);
   }
   return true;
}

bool HeadlessContainerListener::CreateHDWallet(const std::string &clientId, unsigned int id, const headless::NewHDWallet &request
   , NetworkType netType, const std::vector<bs::wallet::PasswordData> &pwdData, bs::wallet::KeyRank keyRank)
{
   if (netType != netType_) {
      CreateHDWalletResponse(clientId, id, "network type mismatch");
      return false;
   }
   std::shared_ptr<bs::core::hd::Wallet> wallet;
   try {
      auto seed = request.privatekey().empty() ? bs::core::wallet::Seed(request.seed(), netType)
         : bs::core::wallet::Seed(netType, request.privatekey(), request.chaincode());
      wallet = walletsMgr_->createWallet(request.name(), request.description()
         , seed, walletsPath_, request.primary(), pwdData, keyRank);
   }
   catch (const std::exception &e) {
      CreateHDWalletResponse(clientId, id, e.what());
      return false;
   }
   if (!wallet) {
      CreateHDWalletResponse(clientId, id, "creation failed");
      return false;
   }
   try {
      SecureBinaryData password = pwdData.empty() ? SecureBinaryData{} : pwdData[0].password;
      if (keyRank.first > 1) {
         for (int i = 1; i < keyRank.first; ++i) {
            password = mergeKeys(password, pwdData[i].password);
         }
      }
      const auto woWallet = wallet->createWatchingOnly(password);
      if (!woWallet) {
         CreateHDWalletResponse(clientId, id, "failed to create watching-only copy");
         return false;
      }
      CreateHDWalletResponse(clientId, id, woWallet->walletId(), {}, {}, woWallet);
   }
   catch (const std::exception &e) {
      CreateHDWalletResponse(clientId, id, e.what());
      return false;
   }
   return true;
}

bool HeadlessContainerListener::onCreateHDWallet(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::CreateHDWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse CreateHDWalletRequest");
      CreateHDWalletResponse(clientId, packet.id(), "failed to parse");
      return false;
   }
   std::vector<bs::wallet::PasswordData> pwdData;
   for (int i = 0; i < request.password_size(); ++i) {
      const auto pwd = request.password(i);
      pwdData.push_back({BinaryData::CreateFromHex(pwd.password())
         , static_cast<bs::wallet::EncryptionType>(pwd.enctype()), pwd.enckey()});
   }
   bs::wallet::KeyRank keyRank = { request.rankm(), request.rankn() };

   if (request.has_leaf()) {
      return CreateHDLeaf(clientId, packet.id(), request.leaf(), pwdData);
   }
   else if (request.has_wallet()) {
      return CreateHDWallet(clientId, packet.id(), request.wallet()
         , mapNetworkType(request.wallet().nettype()), pwdData, keyRank);
   }
   else {
      CreateHDWalletResponse(clientId, packet.id(), "unknown request");
   }
   return false;
}

void HeadlessContainerListener::CreateHDWalletResponse(const std::string &clientId, unsigned int id
   , const std::string &errorOrWalletId, const BinaryData &pubKey, const BinaryData &chainCode
   , const std::shared_ptr<bs::core::hd::Wallet> &wallet)
{
   logger_->debug("[HeadlessContainerListener] CreateHDWalletResponse: {}", errorOrWalletId);
   headless::CreateHDWalletResponse response;
   if (!pubKey.isNull() && !chainCode.isNull()) {
      auto leaf = response.mutable_leaf();
      leaf->set_walletid(errorOrWalletId);
   }
   else if (wallet) {
      auto wlt = response.mutable_wallet();
      wlt->set_name(wallet->name());
      wlt->set_description(wallet->description());
      wlt->set_walletid(wallet->walletId());
      wlt->set_nettype((wallet->networkType() == NetworkType::TestNet) ? headless::TestNetType : headless::MainNetType);
      for (const auto &group : wallet->getGroups()) {
         auto grp = wlt->add_groups();
         grp->set_path(group->path().toString());
         for (const auto &leaf : group->getLeaves()) {
            auto wLeaf = wlt->add_leaves();
            wLeaf->set_path(leaf->path().toString());
            wLeaf->set_walletid(leaf->walletId());
         }
      }
   }
   else {
      response.set_error(errorOrWalletId);
   }

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(headless::CreateHDWalletRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send response CreateHDWallet packet");
   }
}

bool HeadlessContainerListener::onDeleteHDWallet(headless::RequestPacket &packet)
{
   headless::DeleteHDWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse CreateHDWalletRequest");
      return false;
   }
   if (!request.leafwalletid().empty()) {
      const auto &walletId = request.leafwalletid();
      const auto &wallet = walletsMgr_->getWalletById(walletId);
      if (!wallet) {
         logger_->error("[HeadlessContainerListener] failed to find leaf by id {}", walletId);
         return false;
      }
      logger_->debug("Deleting HDLeaf {}: {}", walletId, wallet->name());
      return walletsMgr_->deleteWalletFile(wallet);
   }
   else if (!request.rootwalletid().empty()) {
      const auto &walletId = request.rootwalletid();
      const auto &wallet = walletsMgr_->getHDWalletById(walletId);
      if (!wallet) {
         logger_->error("[HeadlessContainerListener] failed to find HD Wallet by id {}", walletId);
         return false;
      }
      logger_->debug("Deleting HDWallet {}: {}", walletId, wallet->name());
      return walletsMgr_->deleteWalletFile(wallet);
   }

   logger_->error("[HeadlessContainerListener] can't delete any wallet type - no id specified");
   return false;
}

bool HeadlessContainerListener::onSetLimits(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::SetLimitsRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse SetLimitsRequest");
      AutoSignActiveResponse(clientId, {}, false, "request parse error", packet.id());
      return false;
   }
   if (request.rootwalletid().empty()) {
      logger_->error("[HeadlessContainerListener] no wallet specified in SetLimitsRequest");
      AutoSignActiveResponse(clientId, request.rootwalletid(), false, "invalid request", packet.id());
      return false;
   }
   if (!request.activateautosign()) {
      deactivateAutoSign(clientId, request.rootwalletid());
      return true;
   }

   if (!request.password().empty()) {
      activateAutoSign(clientId, request.rootwalletid(), BinaryData::CreateFromHex(request.password()));
   }
   else {
      const auto &wallet = walletsMgr_->getHDWalletById(request.rootwalletid());
      if (!wallet) {
         logger_->error("[HeadlessContainerListener] failed to find root wallet by id {} (to activate auto-sign)"
            , request.rootwalletid());
         AutoSignActiveResponse(clientId, request.rootwalletid(), false, "missing wallet", packet.id());
         return false;
      }
      if (!wallet->encryptionTypes().empty() && !isAutoSignActive(request.rootwalletid())) {
         addPendingAutoSignReq(request.rootwalletid());
         emit autoSignRequiresPwd(request.rootwalletid());
      }
      else {
         emit autoSignActivated(request.rootwalletid());
         AutoSignActiveResponse(clientId, request.rootwalletid(), true, {}, packet.id());
      }
   }
   return true;
}

bool HeadlessContainerListener::onGetRootKey(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::GetRootKeyRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse GetRootKeyRequest");
      GetRootKeyResponse(clientId, packet.id(), nullptr, "failed to parse request");
      return false;
   }
   const auto &wallet = walletsMgr_->getHDWalletById(request.rootwalletid());
   if (!wallet) {
      logger_->error("[HeadlessContainerListener] failed to find wallet for id {}", request.rootwalletid());
      GetRootKeyResponse(clientId, packet.id(), nullptr, "failed to find wallet");
      return false;
   }
   if (!wallet->encryptionTypes().empty() && request.password().empty()) {
      logger_->error("[HeadlessContainerListener] password is missing for encrypted wallet {}", request.rootwalletid());
      GetRootKeyResponse(clientId, packet.id(), nullptr, "password missing");
      return false;
   }

   logger_->info("Requested private key for wallet {}", request.rootwalletid());
   const auto decrypted = wallet->getRootNode(BinaryData::CreateFromHex(request.password()));
   if (!decrypted) {
      logger_->error("[HeadlessContainerListener] failed to get/decrypt root node for {}", request.rootwalletid());
      GetRootKeyResponse(clientId, packet.id(), nullptr, "failed to get node");
      return false;
   }
   GetRootKeyResponse(clientId, packet.id(), decrypted, wallet->walletId());
   return true;
}

void HeadlessContainerListener::GetRootKeyResponse(const std::string &clientId, unsigned int id
   , const std::shared_ptr<bs::core::hd::Node> &decrypted, const std::string &errorOrId)
{
   headless::GetRootKeyResponse response;
   if (decrypted) {
      response.set_decryptedprivkey(decrypted->privateKey().toBinStr());
      response.set_chaincode(decrypted->chainCode().toBinStr());
   }
   response.set_walletid(errorOrId);

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(headless::GetRootKeyRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send response GetRootKey packet");
   }
}

bool HeadlessContainerListener::onGetHDWalletInfo(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::GetHDWalletInfoRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse GetHDWalletInfoRequest");
      GetHDWalletInfoResponse(clientId, packet.id(), {}, nullptr, "failed to parse request");
      return false;
   }
   const auto &wallet = walletsMgr_->getHDWalletById(request.rootwalletid());
   if (!wallet) {
      logger_->error("[HeadlessContainerListener] failed to find wallet for id {}", request.rootwalletid());
      GetHDWalletInfoResponse(clientId, packet.id(), request.rootwalletid(), nullptr, "failed to find wallet");
      return false;
   }
   GetHDWalletInfoResponse(clientId, packet.id(), request.rootwalletid(), wallet);
   return true;
}

void HeadlessContainerListener::GetHDWalletInfoResponse(const std::string &clientId, unsigned int id
   , const std::string &walletId, const std::shared_ptr<bs::core::hd::Wallet> &wallet, const std::string &error)
{
   headless::GetHDWalletInfoResponse response;
   if (!error.empty()) {
      response.set_error(error);
   }
   if (wallet) {
      for (const auto &encType : wallet->encryptionTypes()) {
         response.add_enctypes(static_cast<uint32_t>(encType));
      }
      for (const auto &encKey : wallet->encryptionKeys()) {
         response.add_enckeys(encKey.toBinStr());
      }
      response.set_rankm(wallet->encryptionRank().first);
      response.set_rankn(wallet->encryptionRank().second);
   }
   if (!walletId.empty()) {
      response.set_rootwalletid(walletId);
   }

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(headless::GetHDWalletInfoRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send response GetHDWalletInfo packet");
   }
}

bool HeadlessContainerListener::onChangePassword(const std::string &clientId
   , headless::RequestPacket &packet)
{
   headless::ChangePasswordRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse ChangePasswordRequest");
      ChangePasswordResponse(clientId, packet.id(), {}, false);
      return false;
   }
   const auto &wallet = walletsMgr_->getHDWalletById(request.rootwalletid());
   if (!wallet) {
      logger_->error("[HeadlessContainerListener] failed to find wallet for id {}", request.rootwalletid());
      ChangePasswordResponse(clientId, packet.id(), request.rootwalletid(), false);
      return false;
   }
   std::vector<bs::wallet::PasswordData> pwdData;
   for (int i = 0; i < request.newpassword_size(); ++i) {
      const auto &pwd = request.newpassword(i);
      pwdData.push_back({ BinaryData::CreateFromHex(pwd.password())
         , static_cast<bs::wallet::EncryptionType>(pwd.enctype()), pwd.enckey()});
   }
   bs::wallet::KeyRank keyRank = {request.rankm(), request.rankn()};

   bool result = wallet->changePassword(pwdData, keyRank
      , BinaryData::CreateFromHex(request.oldpassword())
      , request.addnew(), request.removeold(), request.dryrun());

   if (!result) {
      logger_->error("[HeadlessContainerListener] failed to change password for wallet {}", request.rootwalletid());
      ChangePasswordResponse(clientId, packet.id(), request.rootwalletid(), false);
      return false;
   }
   logger_->info("Changed password for wallet {} (id: {})", wallet->name(), wallet->walletId());
   ChangePasswordResponse(clientId, packet.id(), request.rootwalletid(), true);
   return true;
}

void HeadlessContainerListener::ChangePasswordResponse(const std::string &clientId, unsigned int id
   , const std::string &walletId, bool ok)
{
   headless::ChangePasswordResponse response;
   response.set_rootwalletid(walletId);
   response.set_success(ok);

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(headless::ChangePasswordRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send ChangePassword response");
   }
}

void HeadlessContainerListener::AutoSignActiveResponse(const std::string &clientId, const std::string &walletId
   , bool active, const std::string &error, unsigned int id)
{
   headless::SetLimitsResponse response;
   response.set_rootwalletid(walletId);
   response.set_autosignactive(active);
   if (!error.empty()) {
      response.set_error(error);
   }

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_authticket(authTicket(clientId).toBinStr());
   packet.set_type(headless::SetLimitsRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      if (clientId.empty()) {
         logger_->warn("[HeadlessContainerListener] failed to multicast SetLimits response");
      }
      else {
         logger_->error("[HeadlessContainerListener] failed to send SetLimits response");
      }
   }
}

bool HeadlessContainerListener::CheckSpendLimit(uint64_t value, bool autoSign, const std::string &walletId)
{
   if (autoSign) {
      if (value > limits_.autoSignSpendXBT) {
         logger_->warn("[HeadlessContainerListener] requested auto-sign spend {} exceeds limit {}", value
            , limits_.autoSignSpendXBT);
         deactivateAutoSign(walletId, "spend limit reached");
         return false;
      }
   }
   else {
      if (value > limits_.manualSpendXBT) {
         logger_->warn("[HeadlessContainerListener] requested manual spend {} exceeds limit {}", value
            , limits_.manualSpendXBT);
         return false;
      }
   }
   return true;
}

void HeadlessContainerListener::onXbtSpent(qint64 value, bool autoSign)
{
   if (autoSign) {
      limits_.autoSignSpendXBT -= value;
      logger_->debug("[HeadlessContainerListener] new auto-sign spend limit =  {}", limits_.autoSignSpendXBT);
   }
   else {
      limits_.manualSpendXBT -= value;
      logger_->debug("[HeadlessContainerListener] new manual spend limit =  {}", limits_.manualSpendXBT);
   }
}

void HeadlessContainerListener::activateAutoSign(const std::string &clientId, const std::string &walletId
   , const SecureBinaryData &password)
{
   const auto &wallet = walletId.empty() ? walletsMgr_->getPrimaryWallet() : walletsMgr_->getHDWalletById(walletId);
   if (!wallet) {
      deactivateAutoSign(walletId, "wallet missing");
      return;
   }
   if (!wallet->encryptionTypes().empty()) {
      if (password.isNull()) {
         deactivateAutoSign(walletId, "empty password");
         return;
      }
      const auto decrypted = wallet->getRootNode(password);
      if (!decrypted) {
         deactivateAutoSign(walletId, "failed to decrypt root node");
         return;
      }
   }
   passwords_[wallet->walletId()] = password;
   emit autoSignActivated(wallet->walletId());
   if (clientId.empty()) {
      for (const auto &authTicket : authTickets_) {
         AutoSignActiveResponse(authTicket.first, wallet->walletId(), true);
      }
   }
   else {
      AutoSignActiveResponse(clientId, wallet->walletId(), true);
   }
}

void HeadlessContainerListener::deactivateAutoSign(const std::string &clientId, const std::string &walletId
   , const std::string &reason)
{
   if (walletId.empty()) {
      passwords_.clear();
   }
   else {
      passwords_.erase(walletId);
   }
   emit autoSignDeactivated(walletId);
   if (clientId.empty()) {
      for (const auto &authTicket : authTickets_) {
         AutoSignActiveResponse(authTicket.first, walletId, false, reason);
      }
   }
   else {
      AutoSignActiveResponse(clientId, walletId, false, reason);
   }
}

bool HeadlessContainerListener::isAutoSignActive(const std::string &walletId) const
{
   if (walletId.empty()) {
      return !passwords_.empty();
   }
   return (passwords_.find(walletId) != passwords_.end());
}

void HeadlessContainerListener::addPendingAutoSignReq(const std::string &walletId)
{
   if (walletId.empty()) {
      autoSignPwdReqs_.insert(walletsMgr_->getPrimaryWallet()->walletId());
   }
   else {
      autoSignPwdReqs_.insert(walletId);
   }
}

static headless::NetworkType mapFrom(NetworkType netType)
{
   switch (netType) {
   case NetworkType::MainNet: return headless::MainNetType;
   case NetworkType::TestNet:
   default:    return headless::TestNetType;
   }
}

bool HeadlessContainerListener::onSyncWalletInfo(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SyncWalletInfoResponse response;

   for (size_t i = 0; i < walletsMgr_->getHDWalletsCount(); ++i) {
      const auto hdWallet = walletsMgr_->getHDWallet(i);
      auto walletData = response.add_wallets();
      walletData->set_format(headless::WalletFormatHD);
      walletData->set_id(hdWallet->walletId());
      walletData->set_name(hdWallet->name());
      walletData->set_description(hdWallet->description());
      walletData->set_nettype(mapFrom(hdWallet->networkType()));
   }
   const auto settlWallet = walletsMgr_->getSettlementWallet();
   if (settlWallet) {
      auto walletData = response.add_wallets();
      walletData->set_format(headless::WalletFormatSettlement);
      walletData->set_id(settlWallet->walletId());
      walletData->set_name(settlWallet->name());
      walletData->set_nettype(mapFrom(settlWallet->networkType()));
   }

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onSyncHDWallet(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SyncWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }

   headless::SyncHDWalletResponse response;
   const auto hdWallet = walletsMgr_->getHDWalletById(request.walletid());
   if (hdWallet) {
      for (const auto &group : hdWallet->getGroups()) {
         auto groupData = response.add_groups();
         groupData->set_type(group->index());

         for (const auto &leaf : group->getLeaves()) {
            auto leafData = groupData->add_leaves();
            leafData->set_id(leaf->walletId());
            leafData->set_index(leaf->index());
         }
      }
   } else {
      logger_->error("[{}] failed to find HD wallet with id {}", __func__, request.walletid());
      return false;
   }

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

static headless::EncryptionType mapFrom(bs::wallet::EncryptionType encType)
{
   switch (encType) {
   case bs::wallet::EncryptionType::Password:   return headless::EncryptionTypePassword;
   case bs::wallet::EncryptionType::Auth:       return headless::EncryptionTypeAutheID;
   case bs::wallet::EncryptionType::Unencrypted:
   default:       return headless::EncryptionTypeUnencrypted;
   }
}

bool HeadlessContainerListener::onSyncWallet(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SyncWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }

   headless::SyncWalletResponse response;

   const auto wallet = walletsMgr_->getWalletById(request.walletid());
   if (wallet) {
      response.set_walletid(wallet->walletId());
      for (const auto &encType : wallet->encryptionTypes()) {
         response.add_encryptiontypes(mapFrom(encType));
      }
      for (const auto &encKey : wallet->encryptionKeys()) {
         response.add_encryptionkeys(encKey.toBinStr());
      }
      auto keyrank = response.mutable_keyrank();
      keyrank->set_m(wallet->encryptionRank().first);
      keyrank->set_n(wallet->encryptionRank().second);

      response.set_nettype(mapFrom(wallet->networkType()));

      for (const auto &addr : wallet->getUsedAddressList()) {
         const auto index = wallet->getAddressIndex(addr);
         const auto comment = wallet->getAddressComment(addr);
         auto addrData = response.add_addresses();
         addrData->set_address(addr.display<std::string>());
         addrData->set_index(index);
         if (!comment.empty()) {
            addrData->set_comment(comment);
         }
      }
      for (const auto &addr : wallet->getPooledAddressList()) {
         const auto index = wallet->getAddressIndex(addr);
         auto addrData = response.add_addrpool();
         addrData->set_address(addr.display<std::string>());
         addrData->set_index(index);
      }
      for (const auto &txComment : wallet->getAllTxComments()) {
         auto txCommData = response.add_txcomments();
         txCommData->set_txhash(txComment.first.toBinStr());
         txCommData->set_comment(txComment.second);
      }
   }
   else {
      logger_->error("[{}] failed to find wallet with id {}", __func__, request.walletid());
      return false;
   }

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onSyncComment(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SyncCommentRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto wallet = walletsMgr_->getWalletById(request.walletid());
   if (!wallet) {
      logger_->error("[{}] failed to find wallet with id {}", __func__, request.walletid());
      return false;
   }
   bool rc = false;
   if (!request.address().empty()) {
      rc = wallet->setAddressComment(request.address(), request.comment());
   }
   else {
      rc = wallet->setTransactionComment(request.txhash(), request.comment());
   }
   return rc;
}

static AddressEntryType mapFrom(headless::AddressType at)
{
   switch (at) {
   case headless::AddressType_P2PKH:      return AddressEntryType_P2PKH;
   case headless::AddressType_P2PK:       return AddressEntryType_P2PK;
   case headless::AddressType_P2WPKH:     return AddressEntryType_P2WPKH;
   case headless::AddressType_Multisig:   return AddressEntryType_Multisig;
   case headless::AddressType_P2SH:       return AddressEntryType_P2SH;
   case headless::AddressType_P2WSH:      return AddressEntryType_P2WSH;
   case headless::AddressType_Default:
   default:    return AddressEntryType_Default;
   }
}

bool HeadlessContainerListener::onSyncAddresses(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SyncAddressesRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto wallet = walletsMgr_->getWalletById(request.walletid());
   if (!wallet) {
      logger_->error("[{}] failed to find wallet with id {}", __func__, request.walletid());
      return false;
   }

   headless::SyncAddressesResponse response;
   response.set_walletid(wallet->walletId());
   for (int i = 0; i < request.indices_size(); ++i) {
      const auto indexData = request.indices(i);
      std::string index;
      bs::Address address;
      try {
         address = bs::Address(indexData.index());
         if (address.isValid()) {
            index = wallet->getAddressIndex(address);
         }
      }
      catch (const std::exception &) {}
      if (index.empty()) {
         index = indexData.index();
      }
      if (address.isValid() && index.empty()) {
//         wallet->addAddress(address);
         logger_->info("[{}] can't add address {} to wallet {}", __func__
            , address.display<std::string>(), wallet->walletId());
         continue;
      }
      else {
         address = wallet->createAddressWithIndex(index
            , request.persistent(), mapFrom(indexData.addrtype()));
         if (!address.isValid()) {
            logger_->error("[{}] failed to create address for index {}", __func__, index);
            continue;
         }
      }
      auto addrData = response.add_addresses();
      addrData->set_address(address.display<std::string>());
      addrData->set_index(indexData.index());
   }

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}
