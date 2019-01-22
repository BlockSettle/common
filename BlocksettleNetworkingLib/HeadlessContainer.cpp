#include "HeadlessContainer.h"

#include "ApplicationSettings.h"
#include "ConnectionManager.h"
#include "DataConnectionListener.h"
#include "HDWallet.h"
#include "SettlementWallet.h"
#include "WalletsManager.h"
#include "ZmqSecuredDataConnection.h"
#include "ZMQHelperFunctions.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <spdlog/spdlog.h>

using namespace Blocksettle::Communication;
Q_DECLARE_METATYPE(headless::RequestPacket)

static NetworkType mapNetworkType(headless::NetworkType netType)
{
   switch (netType) {
   case headless::MainNetType:   return NetworkType::MainNet;
   case headless::TestNetType:   return NetworkType::TestNet;
   default:    return NetworkType::Invalid;
   }
}

class HeadlessListener : public QObject, public DataConnectionListener
{
   Q_OBJECT
public:
   HeadlessListener(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<DataConnection> &conn, NetworkType netType)
      : logger_(logger), connection_(conn), netType_(netType) {
   }

   void OnDataReceived(const std::string& data) override {
      headless::RequestPacket packet;
      if (!packet.ParseFromString(data)) {
         logger_->error("[HeadlessListener] failed to parse request packet");
         return;
      }
      if (packet.id() > id_) {
         logger_->error("[HeadlessListener] reply id inconsistency: {} > {}", packet.id(), id_);
         emit error(tr("reply id inconsistency"));
         return;
      }
      if ((packet.type() != headless::AuthenticationRequestType)
         && (authTicket_.isNull() || (SecureBinaryData(packet.authticket()) != authTicket_))) {
         if (packet.type() == headless::DisconnectionRequestType) {
            if (packet.authticket().empty()) {
               emit authFailed();
            }
            return;
         }
         logger_->error("[HeadlessListener] {} auth ticket mismatch ({} vs {})!", packet.type()
            , authTicket_.toHexStr(), BinaryData(packet.authticket()).toHexStr());
         emit error(tr("auth ticket mismatch"));
         return;
      }

      if (packet.type() == headless::DisconnectionRequestType) {
         OnDisconnected();
         return;
      }

      if (packet.type() == headless::AuthenticationRequestType) {
         if (!authTicket_.isNull()) {
            logger_->error("[HeadlessListener] already authenticated");
            emit error(tr("already authenticated"));
            return;
         }
         headless::AuthenticationReply response;
         if (!response.ParseFromString(packet.data())) {
            logger_->error("[HeadlessListener] failed to parse auth reply");
            emit error(tr("failed to parse auth reply"));
            return;
         }
         if (mapNetworkType(response.nettype()) != netType_) {
            logger_->error("[HeadlessListener] network type mismatch");
            emit error(tr("network type mismatch"));
            return;
         }

         if (!response.authticket().empty()) {
            authTicket_ = response.authticket();
            hasUI_ = response.hasui();
            logger_->debug("[HeadlessListener] successfully authenticated");
            emit authenticated();
         }
         else {
            logger_->error("[HeadlessListener] authentication failure: {}", response.error());
            emit error(QString::fromStdString(response.error()));
            return;
         }
      }
      else {
         emit PacketReceived(packet);
      }
   }

   void OnConnected() override {
      logger_->debug("[HeadlessListener] Connected");
      emit connected();
   }

   void OnDisconnected() override {
      logger_->debug("[HeadlessListener] Disconnected");
      emit disconnected();
   }

   void OnError(DataConnectionError errorCode) override {
      logger_->debug("[HeadlessListener] error {}", errorCode);
      emit error(tr("error #%1").arg(QString::number(errorCode)));
   }

   HeadlessContainer::RequestId Send(headless::RequestPacket packet, bool updateId = true) {
      HeadlessContainer::RequestId id = 0;
      if (updateId) {
         id = ++id_;
         packet.set_id(id);
      }
      packet.set_authticket(authTicket_.toBinStr());
      if (!connection_->send(packet.SerializeAsString())) {
         logger_->error("[HeadlessListener] Failed to send request packet");
         emit disconnected();
         return 0;
      }
      return id;
   }

   void resetAuthTicket() { authTicket_.clear(); }
   bool isAuthenticated() const { return !authTicket_.isNull(); }
   bool hasUI() const { return hasUI_; }

signals:
   void authenticated();
   void authFailed();
   void connected();
   void disconnected();
   void error(const QString &err);
   void PacketReceived(headless::RequestPacket);

private:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<DataConnection>  connection_;
   const NetworkType                netType_;
   HeadlessContainer::RequestId     id_ = 0;
   SecureBinaryData  authTicket_;
   bool     hasUI_ = false;
};


HeadlessContainer::HeadlessContainer(const std::shared_ptr<spdlog::logger> &logger, OpMode opMode)
   : SignContainer(logger, opMode)
{
   qRegisterMetaType<headless::RequestPacket>();
}

static void killProcess(int pid)
{
#ifdef Q_OS_WIN
   HANDLE hProc;
   hProc = ::OpenProcess(PROCESS_ALL_ACCESS, false, pid);
   ::TerminateProcess(hProc, 0);
   ::CloseHandle(hProc);
#else    // !Q_OS_WIN
   QProcess::execute(QLatin1String("kill"), { QString::number(pid) });
#endif   // Q_OS_WIN
}

static const QString pidFileName = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QLatin1String("/bs_headless.pid");

bool KillHeadlessProcess()
{
   QFile pidFile(pidFileName);
   if (pidFile.exists()) {
      if (pidFile.open(QIODevice::ReadOnly)) {
         const auto pidData = pidFile.readAll();
         pidFile.close();
         const auto pid = atoi(pidData.toStdString().c_str());
         if (pid <= 0) {
            qDebug() << "[HeadlessContainer] invalid PID" << pid <<"in" << pidFileName;
         }
         else {
            killProcess(pid);
            qDebug() << "[HeadlessContainer] killed previous headless process with PID" << pid;
            return true;
         }
      }
      else {
         qDebug() << "[HeadlessContainer] Failed to open PID file" << pidFileName;
      }
      pidFile.remove();
   }
   return false;
}

HeadlessContainer::RequestId HeadlessContainer::Send(headless::RequestPacket packet, bool incSeqNo)
{
   if (!listener_) {
      return 0;
   }
   return listener_->Send(packet, incSeqNo);
}

void HeadlessContainer::ProcessSignTXResponse(unsigned int id, const std::string &data)
{
   headless::SignTXReply response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse SignTXReply");
      emit TXSigned(id, {}, "failed to parse", false);
      return;
   }
   emit TXSigned(id, response.signedtx(), response.error(), response.cancelledbyuser());
}

void HeadlessContainer::ProcessPasswordRequest(const std::string &data)
{
   headless::PasswordRequest request;
   if (!request.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse PasswordRequest");
      return;
   }
   std::vector<bs::wallet::EncryptionType> encTypes;
   for (int i = 0; i < request.enctypes_size(); ++i) {
      encTypes.push_back(static_cast<bs::wallet::EncryptionType>(request.enctypes(i)));
   }
   std::vector<SecureBinaryData> encKeys;
   for (int i = 0; i < request.enckeys_size(); ++i) {
      encKeys.push_back(request.enckeys(i));
   }
   emit PasswordRequested(request.walletid(), request.prompt(), encTypes, encKeys, { request.rankm(), 0 });
}

void HeadlessContainer::ProcessCreateHDWalletResponse(unsigned int id, const std::string &data)
{
   headless::CreateHDWalletResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse CreateHDWallet reply");
      emit Error(id, "failed to parse");
      return;
   }
   if (response.has_leaf()) {
      logger_->debug("[HeadlessContainer] HDLeaf {} created", response.leaf().walletid());
      emit HDLeafCreated(id, response.leaf().pubkey(), response.leaf().chaincode(), response.leaf().walletid());
   }
   else if (response.has_wallet()) {
      const auto netType = (response.wallet().nettype() == headless::TestNetType) ? NetworkType::TestNet : NetworkType::MainNet;
      auto wallet = std::make_shared<bs::hd::Wallet>(response.wallet().walletid()
                                                     , netType
                                                     , false
                                                     , response.wallet().name()
                                                     , logger_
                                                     , response.wallet().description());

      for (int i = 0; i < response.wallet().groups_size(); i++) {
         const auto grpPath = bs::hd::Path::fromString(response.wallet().groups(i).path());
         if (grpPath.length() != 2) {
            logger_->warn("[HeadlessContainer] invalid path[{}]: {}", i, response.wallet().groups(i).path());
            continue;
         }
         const auto grpType = static_cast<bs::hd::CoinType>(grpPath.get((int)grpPath.length() - 1));
         auto group = wallet->createGroup(grpType);

         for (int j = 0; j < response.wallet().leaves_size(); j++) {
            const auto leafPath = bs::hd::Path::fromString(response.wallet().leaves(j).path());
            if (leafPath.length() != 3) {
               logger_->warn("[HeadlessContainer] invalid path[{}]: {}", j, response.wallet().leaves(j).path());
               continue;
            }
            if (leafPath.get((int)leafPath.length() - 2) != static_cast<bs::hd::Path::Elem>(grpType)) {
               continue;
            }
            auto leaf = group->newLeaf();
            const auto node = std::make_shared<bs::hd::Node>(response.wallet().leaves(j).pubkey()
               , response.wallet().leaves(j).chaincode(), netType);
            leaf->init(node, leafPath, {});
            group->addLeaf(leaf);
         }
      }
      logger_->debug("[HeadlessContainer] HDWallet {} created", wallet->getWalletId());
      emit HDWalletCreated(id, wallet);
   }
   else {
      emit Error(id, response.error());
   }
}

void HeadlessContainer::ProcessGetRootKeyResponse(unsigned int id, const std::string &data)
{
   headless::GetRootKeyResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse GetRootKey reply");
      emit Error(id, "failed to parse");
      return;
   }
   if (response.decryptedprivkey().empty()) {
      emit Error(id, response.walletid());
   }
   else {
      emit DecryptedRootKey(id, response.decryptedprivkey(), response.chaincode(), response.walletid());
   }
}

void HeadlessContainer::ProcessGetHDWalletInfoResponse(unsigned int id, const std::string &data)
{
   headless::GetHDWalletInfoResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse GetHDWalletInfo reply");
      emit Error(id, "failed to parse");
      return;
   }
   if (response.error().empty()) {
      std::vector<bs::wallet::EncryptionType> encTypes;
      for (int i = 0; i < response.enctypes_size(); ++i) {
         encTypes.push_back(static_cast<bs::wallet::EncryptionType>(response.enctypes(i)));
      }
      std::vector<SecureBinaryData> encKeys;
      for (int i = 0; i < response.enckeys_size(); ++i) {
         encKeys.push_back(response.enckeys(i));
      }
      bs::wallet::KeyRank keyRank = { response.rankm(), response.rankn() };
      emit HDWalletInfo(id, encTypes, encKeys, keyRank);
   }
   else {
      missingWallets_.insert(response.rootwalletid());
      emit Error(id, response.error());
   }
}

void HeadlessContainer::ProcessSyncAddrResponse(const std::string &data)
{
   headless::SyncAddressResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse SyncAddress reply");
      emit Error(0, "invalid address sync reply");
      return;
   }

   if (response.missingwalletid_size() > 0) {
      std::vector<std::string> missingWallets;
      for (int i = 0; i < response.missingwalletid_size(); i++) {
         missingWallets.push_back(response.missingwalletid(i));
         missingWallets_.insert(response.missingwalletid(i));
      }
      emit MissingWallets(missingWallets);
   }
   if (response.failedaddress_size() > 0) {
      std::vector<std::pair<std::string, std::string>> failedAddresses;
      for (int i = 0; i < response.failedaddress_size(); i++) {
         const auto failedAddr = response.failedaddress(i);
         failedAddresses.push_back({ failedAddr.walletid(), failedAddr.index() });
      }
      emit AddressSyncFailed(failedAddresses);
   }
   emit AddressSyncComplete();
}

void HeadlessContainer::ProcessChangePasswordResponse(unsigned int id, const std::string &data)
{
   headless::ChangePasswordResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse ChangePassword reply");
      emit Error(id, "failed to parse");
      return;
   }
   emit PasswordChanged(response.rootwalletid(), response.success());
}

void HeadlessContainer::ProcessSetLimitsResponse(unsigned int id, const std::string &data)
{
   headless::SetLimitsResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer] Failed to parse SetLimits reply");
      emit Error(id, "failed to parse");
      return;
   }
   emit AutoSignStateChanged(response.rootwalletid(), response.autosignactive(), response.error());
}

HeadlessContainer::RequestId HeadlessContainer::SignTXRequest(const bs::wallet::TXSignRequest &txSignReq
   , bool autoSign, SignContainer::TXSignMode mode, const PasswordType& password
   , bool keepDuplicatedRecipients)
{
   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainer] Invalid TXSignRequest");
      return 0;
   }
   headless::SignTXRequest request;
   request.set_walletid(txSignReq.walletId);
   request.set_keepduplicatedrecipients(keepDuplicatedRecipients);
   if (autoSign) {
      request.set_applyautosignrules(true);
   }
   if (txSignReq.populateUTXOs) {
      request.set_populateutxos(true);
   }

   for (const auto &utxo : txSignReq.inputs) {
      request.add_inputs(utxo.serialize().toBinStr());
   }

   for (const auto &recip : txSignReq.recipients) {
      request.add_recipients(recip->getSerializedScript().toBinStr());
   }
   if (txSignReq.fee) {
      request.set_fee(txSignReq.fee);
   }

   if (txSignReq.RBF) {
      request.set_rbf(true);
   }

   if (!password.isNull()) {
      request.set_password(password.toHexStr());
   }

   if (!txSignReq.prevStates.empty()) {
      request.set_unsignedstate(txSignReq.serializeState().toBinStr());
   }

   if (txSignReq.change.value) {
      auto change = request.mutable_change();
      change->set_address(txSignReq.change.address.display<std::string>());
      change->set_index(txSignReq.change.index);
      change->set_value(txSignReq.change.value);
   }

   headless::RequestPacket packet;
   switch (mode) {
   case TXSignMode::Full:
      packet.set_type(headless::SignTXRequestType);
      break;

   case TXSignMode::Partial:
      packet.set_type(headless::SignPartialTXRequestType);
      break;

   default:    break;
   }
   packet.set_data(request.SerializeAsString());
   RequestId id = Send(packet);
   signRequests_.insert(id);
   return id;
}

unsigned int HeadlessContainer::SignPartialTXRequest(const bs::wallet::TXSignRequest &req
   , bool autoSign, const PasswordType& password)
{
   return SignTXRequest(req, autoSign, TXSignMode::Partial, password);
}

HeadlessContainer::RequestId HeadlessContainer::SignPayoutTXRequest(const bs::wallet::TXSignRequest &txSignReq, const bs::Address &authAddr
   , const std::shared_ptr<bs::SettlementAddressEntry> &settlAddr
   , bool autoSign, const PasswordType& password)
{
   if ((txSignReq.inputs.size() != 1) || (txSignReq.recipients.size() != 1) || !settlAddr) {
      logger_->error("[HeadlessContainer] Invalid PayoutTXSignRequest");
      return 0;
   }
   headless::SignPayoutTXRequest request;
   request.set_input(txSignReq.inputs[0].serialize().toBinStr());
   request.set_recipient(txSignReq.recipients[0]->getSerializedScript().toBinStr());
   request.set_authaddress(authAddr.display<std::string>());
   request.set_settlementid(settlAddr->getAsset()->settlementId().toBinStr());
   request.set_buyauthkey(settlAddr->getAsset()->buyAuthPubKey().toBinStr());
   request.set_sellauthkey(settlAddr->getAsset()->sellAuthPubKey().toBinStr());
   if (autoSign) {
      request.set_applyautosignrules(autoSign);
   }

   if (!password.isNull()) {
      request.set_password(password.toHexStr());
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SignPayoutTXRequestType);
   packet.set_data(request.SerializeAsString());
   RequestId id = Send(packet);
   signRequests_.insert(id);
   return id;
}

HeadlessContainer::RequestId HeadlessContainer::SignMultiTXRequest(const bs::wallet::TXMultiSignRequest &txMultiReq)
{
   if (!txMultiReq.isValid()) {
      logger_->error("[HeadlessContainer] Invalid TXMultiSignRequest");
      return 0;
   }

   Signer signer;
   signer.setFlags(SCRIPT_VERIFY_SEGWIT);

   headless::SignTXMultiRequest request;
   for (const auto &input : txMultiReq.inputs) {
      request.add_walletids(input.second->GetWalletId());
      signer.addSpender(std::make_shared<ScriptSpender>(input.first));
   }
   for (const auto &recip : txMultiReq.recipients) {
      signer.addRecipient(recip);
   }
   request.set_signerstate(signer.serializeState().toBinStr());

   headless::RequestPacket packet;
   packet.set_type(headless::SignTXMultiRequestType);
   packet.set_data(request.SerializeAsString());
   RequestId id = Send(packet);
   signRequests_.insert(id);
   return id;
}

HeadlessContainer::RequestId HeadlessContainer::CancelSignTx(const BinaryData &txId)
{
   headless::CancelSignTx request;
   request.set_txid(txId.toBinStr());

   headless::RequestPacket packet;
   packet.set_type(headless::CancelSignTxRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

void HeadlessContainer::SendPassword(const std::string &walletId, const PasswordType &password,
   bool cancelledByUser)
{
   headless::RequestPacket packet;
   packet.set_type(headless::PasswordRequestType);

   headless::PasswordReply response;
   if (!walletId.empty()) {
      response.set_walletid(walletId);
   }
   if (!password.isNull()) {
      response.set_password(password.toHexStr());
   }
   response.set_cancelledbyuser(cancelledByUser);
   packet.set_data(response.SerializeAsString());
   Send(packet, false);
}

HeadlessContainer::RequestId HeadlessContainer::SetUserId(const BinaryData &userId)
{
   if (!listener_) {
      logger_->warn("[HeadlessContainer::SetUserId] listener not set yet");
      return 0;
   }

   if (!listener_->isAuthenticated()) {
      logger_->warn("[HeadlessContainer] setting userid without being authenticated is not allowed");
      return 0;
   }
   headless::SetUserIdRequest request;
   if (!userId.isNull()) {
      request.set_userid(userId.toBinStr());
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SetUserIdRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

static headless::AddressType getAddressType(AddressEntryType aet)
{
   switch (aet) {
   case AddressEntryType_P2PKH:           return headless::LegacyAddressType;
   case AddressEntryType_P2SH:            return headless::NestedSWAddressType;
   case AddressEntryType_P2WPKH:
   default:                               return headless::NativeSWAddressType;
   }
}

HeadlessContainer::RequestId HeadlessContainer::SyncAddresses(
   const std::vector<std::pair<std::shared_ptr<bs::Wallet>, bs::Address>> &addresses)
{
   if (addresses.empty()) {
      return 0;
   }
   if (!listener_->isAuthenticated()) {
      logger_->warn("[HeadlessContainer] syncing addresses without being authenticated is not allowed");
      return 0;
   }
   headless::SyncAddressRequest request;
   for (const auto &addr : addresses) {
      if (!addr.first) {
         logger_->warn("[HeadlessContainer] Wrong input data - skipping");
         continue;
      }
      auto address = request.add_address();
      if (!addr.second.isNull()) {
         const auto index = addr.first->GetAddressIndex(addr.second);
         if (index.empty()) {
            logger_->error("[HeadlessContainer] Failed to get index for address {}"
               , addr.second.display<std::string>());
            continue;
         }
         address->set_index(index);
         address->set_addrtype(getAddressType(addr.second.getType()));
      }
      address->set_walletid(addr.first->GetWalletId());
   }
   if (!request.address_size()) {
      logger_->error("[HeadlessContainer] SyncAddressRequest wasn't sent due to previous error[s]");
      return 0;
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SyncAddressRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

HeadlessContainer::RequestId HeadlessContainer::CreateHDLeaf(const std::shared_ptr<bs::hd::Wallet> &root
   , const bs::hd::Path &path, const std::vector<bs::wallet::PasswordData> &pwdData)
{
   if (!root || (path.length() != 3)) {
      logger_->error("[HeadlessContainer] Invalid input data for HD wallet creation");
      return 0;
   }
   headless::CreateHDWalletRequest request;
   auto leaf = request.mutable_leaf();
   leaf->set_rootwalletid(root->getWalletId());
   leaf->set_path(path.toString());
   for (const auto &pwd : pwdData) {
      auto reqPwd = request.add_password();
      reqPwd->set_password(pwd.password.toHexStr());
      reqPwd->set_enctype(static_cast<uint32_t>(pwd.encType));
      reqPwd->set_enckey(pwd.encKey.toBinStr());
   }

   headless::RequestPacket packet;
   packet.set_type(headless::CreateHDWalletRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

HeadlessContainer::RequestId HeadlessContainer::CreateHDWallet(const std::string &name
   , const std::string &desc, bool primary, const bs::wallet::Seed &seed
   , const std::vector<bs::wallet::PasswordData> &pwdData, bs::wallet::KeyRank keyRank)
{
   headless::CreateHDWalletRequest request;
   if (!pwdData.empty()) {
      request.set_rankm(keyRank.first);
      request.set_rankn(keyRank.second);
   }
   for (const auto &pwd : pwdData) {
      auto reqPwd = request.add_password();
      reqPwd->set_password(pwd.password.toHexStr());
      reqPwd->set_enctype(static_cast<uint32_t>(pwd.encType));
      reqPwd->set_enckey(pwd.encKey.toBinStr());
   }
   auto wallet = request.mutable_wallet();
   wallet->set_name(name);
   wallet->set_description(desc);
   wallet->set_nettype((seed.networkType() == NetworkType::TestNet) ? headless::TestNetType : headless::MainNetType);
   if (primary) {
      wallet->set_primary(true);
   }
   if (!seed.empty()) {
      if (seed.hasPrivateKey()) {
         wallet->set_privatekey(seed.privateKey().toBinStr());
         wallet->set_chaincode(seed.chainCode().toBinStr());
      }
      else if (!seed.seed().isNull()) {
         wallet->set_seed(seed.seed().toBinStr());
      }
   }

   headless::RequestPacket packet;
   packet.set_type(headless::CreateHDWalletRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

HeadlessContainer::RequestId HeadlessContainer::DeleteHD(const std::shared_ptr<bs::hd::Wallet> &wlt)
{
   if (!wlt) {
      return 0;
   }
   return SendDeleteHDRequest(wlt->getWalletId(), {});
}
HeadlessContainer::RequestId HeadlessContainer::DeleteHD(const std::shared_ptr<bs::Wallet> &leaf)
{
   if (!leaf) {
      return 0;
   }
   return SendDeleteHDRequest({}, leaf->GetWalletId());
}

HeadlessContainer::RequestId HeadlessContainer::SendDeleteHDRequest(const std::string &rootWalletId, const std::string &leafId)
{
   headless::DeleteHDWalletRequest request;
   if (!rootWalletId.empty()) {
      request.set_rootwalletid(rootWalletId);
   }
   else if (!leafId.empty()) {
      request.set_leafwalletid(leafId);
   }
   else {
      logger_->error("[HeadlessContainer] can't send delete request - both IDs are empty");
      return 0;
   }

   headless::RequestPacket packet;
   packet.set_type(headless::DeleteHDWalletRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

void HeadlessContainer::SetLimits(const std::shared_ptr<bs::hd::Wallet> &wallet, const SecureBinaryData &pass
   , bool autoSign)
{
   if (!wallet) {
      logger_->error("[HeadlessContainer] no root wallet for SetLimits");
      return;
   }
   if (!listener_->isAuthenticated()) {
      logger_->warn("[HeadlessContainer] setting limits without being authenticated is not allowed");
      return;
   }
   headless::SetLimitsRequest request;
   request.set_rootwalletid(wallet->getWalletId());
   if (!pass.isNull()) {
      request.set_password(pass.toHexStr());
   }
   request.set_activateautosign(autoSign);

   headless::RequestPacket packet;
   packet.set_type(headless::SetLimitsRequestType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

HeadlessContainer::RequestId HeadlessContainer::ChangePassword(const std::shared_ptr<bs::hd::Wallet> &wallet
   , const std::vector<bs::wallet::PasswordData> &newPass, bs::wallet::KeyRank keyRank
   , const SecureBinaryData &oldPass, bool addNew, bool removeOld, bool dryRun)
{
   if (!wallet) {
      logger_->error("[HeadlessContainer] no root wallet for ChangePassword");
      return 0;
   }
   headless::ChangePasswordRequest request;
   request.set_rootwalletid(wallet->getWalletId());
   if (!oldPass.isNull()) {
      request.set_oldpassword(oldPass.toHexStr());
   }
   for (const auto &pwd : newPass) {
      auto reqNewPass = request.add_newpassword();
      reqNewPass->set_password(pwd.password.toHexStr());
      reqNewPass->set_enctype(static_cast<uint32_t>(pwd.encType));
      reqNewPass->set_enckey(pwd.encKey.toBinStr());
   }
   request.set_rankm(keyRank.first);
   request.set_rankn(keyRank.second);
   request.set_addnew(addNew);
   request.set_removeold(removeOld);
   request.set_dryrun(dryRun);

   headless::RequestPacket packet;
   packet.set_type(headless::ChangePasswordRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

HeadlessContainer::RequestId HeadlessContainer::GetDecryptedRootKey(const std::shared_ptr<bs::hd::Wallet> &wallet
   , const SecureBinaryData &password)
{
   headless::GetRootKeyRequest request;
   request.set_rootwalletid(wallet->getWalletId());
   if (!password.isNull()) {
      request.set_password(password.toHexStr());
   }

   headless::RequestPacket packet;
   packet.set_type(headless::GetRootKeyRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

HeadlessContainer::RequestId HeadlessContainer::GetInfo(const std::shared_ptr<bs::hd::Wallet> &wallet)
{
   if (!wallet) {
      return 0;
   }
   headless::GetHDWalletInfoRequest request;
   request.set_rootwalletid(wallet->getWalletId());

   headless::RequestPacket packet;
   packet.set_type(headless::GetHDWalletInfoRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

bool HeadlessContainer::isReady() const
{
   return (listener_ && listener_->isAuthenticated());
}

bool HeadlessContainer::isWalletOffline(const std::string &walletId) const
{
   return (missingWallets_.find(walletId) != missingWallets_.end());
}


RemoteSigner::RemoteSigner(const std::shared_ptr<spdlog::logger> &logger
                           , const QString &host, const QString &port
                           , NetworkType netType
                           , const std::shared_ptr<ConnectionManager>& connectionManager
                           , const std::shared_ptr<ApplicationSettings>& appSettings
                           , OpMode opMode)
   : HeadlessContainer(logger, opMode)
   , host_(host), port_(port), netType_(netType)
   , connectionManager_{connectionManager}
   , appSettings_(appSettings)
{}

// Establish the remote connection to the signer.
bool RemoteSigner::Start()
{
   if (connection_) {
      return true;
   }

   // Load remote singer zmq pub key.
   // If the server pub key exists, proceed (it was initialized in LocalSigner::Start()).
   if (!zmqSignerPubKey_.getSize()){
      const QString &zmqRemoteSignerPubKey = appSettings_->get<QString>(ApplicationSettings::zmqRemoteSignerPubKey);

      if (!bs::network::readZmqKeyString(zmqRemoteSignerPubKey.toLatin1(), zmqSignerPubKey_, true
         , logger_)) {
         logger_->error("[RemoteSigner::{}] failed to read ZMQ server public "
            "key.", __func__);
         return false;
      }
   }

   connection_ = connectionManager_->CreateSecuredDataConnection(true);
   if (!connection_->SetServerPublicKey(zmqSignerPubKey_)) {
      logger_->error("[RemoteSigner::{}] Failed to set ZMQ server public key"
         , __func__);
      connection_ = nullptr;
      return false;
   }

   if (opMode() == OpMode::RemoteInproc) {
      connection_->SetZMQTransport(ZMQTransport::InprocTransport);
   }

   {
      std::lock_guard<std::mutex> lock(mutex_);
      listener_ = std::make_shared<HeadlessListener>(logger_, connection_, netType_);
      connect(listener_.get(), &HeadlessListener::connected, this, &RemoteSigner::onConnected, Qt::QueuedConnection);
      connect(listener_.get(), &HeadlessListener::authenticated, this, &RemoteSigner::onAuthenticated, Qt::QueuedConnection);
      connect(listener_.get(), &HeadlessListener::authFailed, [this] { authPending_ = false; });
      connect(listener_.get(), &HeadlessListener::disconnected, this, &RemoteSigner::onDisconnected, Qt::QueuedConnection);
      connect(listener_.get(), &HeadlessListener::error, this, &RemoteSigner::onConnError, Qt::QueuedConnection);
      connect(listener_.get(), &HeadlessListener::PacketReceived, this, &RemoteSigner::onPacketReceived, Qt::QueuedConnection);
   }

   return Connect();
}

bool RemoteSigner::Stop()
{
   return Disconnect();
}

bool RemoteSigner::Connect()
{
   QtConcurrent::run(this, &RemoteSigner::ConnectHelper);
   return true;
}

void RemoteSigner::ConnectHelper()
{
   if (!connection_->isActive()) {
      if (connection_->openConnection(host_.toStdString(), port_.toStdString(), listener_.get())) {
         emit connected();
      }
      else {
         logger_->error("[HeadlessContainer] Failed to open connection to "
            "headless container");
         return;
      }
   }
   Authenticate();
}

bool RemoteSigner::Disconnect()
{
   if (!connection_) {
      return true;
   }
   headless::RequestPacket packet;
   packet.set_type(headless::DisconnectionRequestType);
   packet.set_data("");
   Send(packet);

   return connection_->closeConnection();
}

void RemoteSigner::Authenticate()
{
   mutex_.lock();

   if (!listener_) {
      mutex_.unlock();
      emit connectionError(tr("listener missing on authenticate"));
      return;
   }
   if (listener_->isAuthenticated() || authPending_) {
      mutex_.unlock();
      return;
   }

   mutex_.unlock();

   authPending_ = true;
   headless::AuthenticationRequest request;
   request.set_nettype((netType_ == NetworkType::TestNet) ? headless::TestNetType : headless::MainNetType);

   headless::RequestPacket packet;
   packet.set_type(headless::AuthenticationRequestType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

bool RemoteSigner::isOffline() const
{
   std::lock_guard<std::mutex> lock(mutex_);

   if (!listener_) {
      return true;
   }
   return !listener_->isAuthenticated();
}

bool RemoteSigner::hasUI() const
{
   std::lock_guard<std::mutex> lock(mutex_);

   return listener_ ? listener_->hasUI() : false;
}

void RemoteSigner::onConnected()
{
   Connect();
}

void RemoteSigner::onAuthenticated()
{
   authPending_ = false;
   emit authenticated();
   emit ready();
}

void RemoteSigner::onDisconnected()
{
   missingWallets_.clear();

   {
      std::lock_guard<std::mutex> lock(mutex_);

      if (listener_) {
         listener_->resetAuthTicket();
      }
   }

   std::set<RequestId> tmpReqs = signRequests_;
   signRequests_.clear();

   for (const auto &id : tmpReqs) {
      emit TXSigned(id, {}, "signer disconnected", false);
   }

   emit disconnected();
}

void RemoteSigner::onConnError(const QString &err)
{
   emit connectionError(err);
}

void RemoteSigner::onPacketReceived(headless::RequestPacket packet)
{
   signRequests_.erase(packet.id());

   switch (packet.type()) {
   case headless::HeartbeatType:
      break;

   case headless::SignTXRequestType:
   case headless::SignPartialTXRequestType:
   case headless::SignPayoutTXRequestType:
   case headless::SignTXMultiRequestType:
      ProcessSignTXResponse(packet.id(), packet.data());
      break;

   case headless::PasswordRequestType:
      ProcessPasswordRequest(packet.data());
      break;

   case headless::CreateHDWalletRequestType:
      ProcessCreateHDWalletResponse(packet.id(), packet.data());
      break;

   case headless::GetRootKeyRequestType:
      ProcessGetRootKeyResponse(packet.id(), packet.data());
      break;

   case headless::GetHDWalletInfoRequestType:
      ProcessGetHDWalletInfoResponse(packet.id(), packet.data());
      break;

   case headless::SyncAddressRequestType:
      ProcessSyncAddrResponse(packet.data());
      break;

   case headless::SetUserIdRequestType:
      emit UserIdSet();
      break;

   case headless::ChangePasswordRequestType:
      ProcessChangePasswordResponse(packet.id(), packet.data());
      break;

   case headless::SetLimitsRequestType:
      ProcessSetLimitsResponse(packet.id(), packet.data());
      break;

   default:
      logger_->warn("[HeadlessContainer] Unknown packet type: {}", packet.type());
      break;
   }
}


LocalSigner::LocalSigner(const std::shared_ptr<spdlog::logger> &logger
                         , const QString &homeDir, NetworkType netType, const QString &port
                         , const std::shared_ptr<ConnectionManager>& connectionManager
                         , const std::shared_ptr<ApplicationSettings> &appSettings
                         , double asSpendLimit)
   : RemoteSigner(logger, QLatin1String("127.0.0.1"), port, netType
                  , connectionManager, appSettings, OpMode::Local)

{
   auto walletsCopyDir = homeDir + QLatin1String("/copy");
   if (!QDir().exists(walletsCopyDir)) {
      walletsCopyDir = homeDir + QLatin1String("/signer");
   }
   QDir dirWalletsCopy(walletsCopyDir);
   if (!dirWalletsCopy.exists()) {
      dirWalletsCopy.mkpath(walletsCopyDir);

      QDir dirWallets(homeDir);
      const auto walletFiles = dirWallets.entryList({ QLatin1String("*.lmdb") }, QDir::Files);
      logger_->debug("{} files in {}", walletFiles.size(), dirWallets.dirName().toStdString());
      for (const auto &file : walletFiles) {
         if (file.startsWith(QString::fromStdString(bs::hd::Wallet::fileNamePrefix(true)))) {
            continue;
         }
         const auto srcPathName = homeDir + QLatin1String("/") + file;
         const auto dstPathName = walletsCopyDir + QLatin1String("/") + file;
         QFile::copy(srcPathName, dstPathName);
      }
   }

   args_ << QLatin1String("--headless");
   switch (netType) {
   case NetworkType::TestNet:
   case NetworkType::RegTest:
      args_ << QString::fromStdString("--testnet");
      break;
   case NetworkType::MainNet:
      args_ << QString::fromStdString("--mainnet");
      break;
   default: break;
   }

   args_ << QLatin1String("--listen") << QLatin1String("127.0.0.1");
   args_ << QLatin1String("--port") << port_;
   args_ << QLatin1String("--dirwallets") << walletsCopyDir;
   if (asSpendLimit > 0) {
      args_ << QLatin1String("--auto_sign_spend_limit") << QString::number(asSpendLimit, 'f', 8);
   }
}

bool LocalSigner::Start()
{
   // If there's a previous headless process, stop it.
   KillHeadlessProcess();
   headlessProcess_ = std::make_shared<QProcess>();
   connect(headlessProcess_.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished)
      , [](int exitCode, QProcess::ExitStatus exitStatus) {
      QFile::remove(pidFileName);
   });

#ifdef Q_OS_WIN
   const auto signerAppPath = QCoreApplication::applicationDirPath() + QLatin1String("/blocksettle_signer.exe");
#elif defined (Q_OS_MACOS)
   auto bundleDir = QDir(QCoreApplication::applicationDirPath());
   bundleDir.cdUp();
   bundleDir.cdUp();
   bundleDir.cdUp();
   const auto signerAppPath = bundleDir.absoluteFilePath(QLatin1String("Blocksettle Signer.app/Contents/MacOS/Blocksettle Signer"));
#else
   const auto signerAppPath = QCoreApplication::applicationDirPath() + QLatin1String("/blocksettle_signer");
#endif
   if (!QFile::exists(signerAppPath)) {
      logger_->error("[HeadlessContainer] Signer binary {} not found"
         , signerAppPath.toStdString());
      emit connectionError(tr("missing signer binary"));
      emit ready();
      return false;
   }

   logger_->debug("[HeadlessContainer] starting {} {}"
      , signerAppPath.toStdString(), args_.join(QLatin1Char(' ')).toStdString());
   headlessProcess_->start(signerAppPath, args_);
   if (!headlessProcess_->waitForStarted(5000)) {
      logger_->error("[HeadlessContainer] Failed to start child");
      headlessProcess_.reset();
      emit ready();
      return false;
   }

   QFile pidFile(pidFileName);
   if (pidFile.open(QIODevice::WriteOnly)) {
      const auto pidStr = \
         QString::number(headlessProcess_->processId()).toStdString();
      pidFile.write(pidStr.data(), pidStr.size());
      pidFile.close();
   }
   else {
      logger_->warn("[LocalSigner::{}] Failed to open PID file {} for writing"
         , __func__, pidFileName.toStdString());
   }
   logger_->debug("[LocalSigner::{}] child process started", __func__);


   // Load local ZMQ server public key.
   if (zmqSignerPubKey_.getSize() == 0) {
      // If the server pub key exists, proceed. If not, give the signer a little time to create the key.
      // 50 ms seems reasonable on a VM but we'll add some padding to be safe.
      const auto zmqLocalSignerPubKeyPath = appSettings_->get<QString>(ApplicationSettings::zmqLocalSignerPubKeyFilePath);

      QFile zmqLocalSignerPubKeyFile(zmqLocalSignerPubKeyPath);
      if (!zmqLocalSignerPubKeyFile.exists()) {
         QThread::msleep(250);
      }

      if (!bs::network::readZmqKeyFile(zmqLocalSignerPubKeyPath, zmqSignerPubKey_, true
         , logger_)) {
         logger_->error("[LocalSigner::{}] failed to read ZMQ server public "
            "key ({})", __func__, zmqLocalSignerPubKeyPath.toStdString());
      }
   }


   // SPECIAL CASE: Unlike Windows and Linux, the Signer and Terminal have
   // different data directories on Macs. Check the Signer for a file. There is
   // an issue here if the Signer has moved its keys away from the standard
   // location. We really should check the Signer's config file instead.
#ifdef Q_OS_MACOS
   QString zmqSignerPubKeyPath = \
      appSettings_->get<QString>(ApplicationSettings::zmqLocalSignerPubKeyFilePath);
   QFile zmqSignerPubKeyFile(zmqSignerPubKeyPath);
   if (!zmqSignerPubKeyFile.exists()) {
      QThread::msleep(250); // Give Signer time to create files if needed.
      QDir signZMQFileDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
      signZMQFileDir.cdUp();
      QString signZMQSrvPubKeyPath = signZMQFileDir.path() + \
         QString::fromStdString("/Blocksettle/zmq_conn_srv.pub");
      if (!QFile::copy(signZMQSrvPubKeyPath, zmqSignerPubKeyPath)) {
         logger_->error("[LocalSigner::{}] Failed to copy ZMQ public key file "
            "{} to the terminal. Connection will not start.", __func__
            , signZMQSrvPubKeyPath.toStdString());
         return false;
      }
      else {
         logger_->info("[LocalSigner::{}] Copied ZMQ public key file ({}) to "
            "the terminal.", __func__, zmqSignerPubKeyPath.toStdString());
      }
   }
#endif

   return RemoteSigner::Start();
}

bool LocalSigner::Stop()
{
   RemoteSigner::Stop();

   if (headlessProcess_) {
      headlessProcess_->terminate();
      if (!headlessProcess_->waitForFinished(500)) {
         headlessProcess_->close();
      }
   }
   return true;
}


HeadlessAddressSyncer::HeadlessAddressSyncer(const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<WalletsManager> &walletsMgr)
   : QObject(nullptr), signingContainer_(container), walletsMgr_(walletsMgr)
{
   connect(walletsMgr_.get(), &WalletsManager::walletsReady, this, &HeadlessAddressSyncer::onWalletsUpdated);
   connect(signingContainer_.get(), &SignContainer::ready, this, &HeadlessAddressSyncer::onSignerReady);
}

void HeadlessAddressSyncer::onWalletsUpdated()
{
   if (!signingContainer_->isReady()) {
      wasOffline_ = true;
      return;
   }
   signingContainer_->SyncAddresses(walletsMgr_->GetAddressesInAllWallets());
}

void HeadlessAddressSyncer::onSignerReady()
{
   if (signingContainer_->isOffline()) {
      wasOffline_ = true;
      return;
   }
   if (wasOffline_) {
      wasOffline_ = false;
      signingContainer_->SyncAddresses(walletsMgr_->GetAddressesInAllWallets());
   }
}

void HeadlessAddressSyncer::SyncWallet(const std::shared_ptr<bs::Wallet> &wallet)
{
   if (!wallet || !signingContainer_) {
      return;
   }
   std::vector<std::pair<std::shared_ptr<bs::Wallet>, bs::Address>> addresses;
   for (const auto &addr : wallet->GetUsedAddressList()) {
      addresses.push_back({wallet, addr});
   }
   signingContainer_->SyncAddresses(addresses);
}

#include "HeadlessContainer.moc"
