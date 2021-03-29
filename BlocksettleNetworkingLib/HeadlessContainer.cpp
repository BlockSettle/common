/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "HeadlessContainer.h"

#include "BSErrorCodeStrings.h"
#include "Bip15xDataConnection.h"
#include "ConnectionManager.h"
#include "DataConnection.h"
#include "ProtobufHeadlessUtils.h"
#include "SystemFileUtils.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"
#include "WsDataConnection.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>

#include <spdlog/spdlog.h>
#include "headless.pb.h"
#include "signer.pb.h"

#include "SocketObject.h"

namespace {

   constexpr int kKillTimeout = 5000;
   constexpr int kStartTimeout = 5000;

   // When remote signer will try to reconnect
   constexpr auto kLocalReconnectPeriod = std::chrono::seconds(10);
   constexpr auto kRemoteReconnectPeriod = std::chrono::seconds(1);
   constexpr auto kSleepPeriod = std::chrono::milliseconds(20);

   const uint32_t kConnectTimeoutSec = 1;

} // namespace

using namespace Blocksettle::Communication;
using namespace bs::sync;

Q_DECLARE_METATYPE(headless::RequestPacket)
Q_DECLARE_METATYPE(std::shared_ptr<bs::sync::hd::Leaf>)

NetworkType HeadlessContainer::mapNetworkType(headless::NetworkType netType)
{
   switch (netType) {
   case headless::MainNetType:   return NetworkType::MainNet;
   case headless::TestNetType:   return NetworkType::TestNet;
   default:                      return NetworkType::Invalid;
   }
}

void HeadlessListener::processDisconnectNotification()
{
   SPDLOG_LOGGER_INFO(logger_, "remote signer has been disconnected");
   isConnected_ = false;
   isReady_ = false;
   tryEmitError(HeadlessContainer::SignerGoesOffline
      , QObject::tr("Remote signer disconnected"));
}

void HeadlessListener::tryEmitError(SignContainer::ConnectionError errorCode, const QString &msg)
{
   // Try to send error only once because only first error should be relevant.
   if (!wasErrorReported_) {
      wasErrorReported_ = true;
      parent_->onConnError(errorCode, msg);
   }
}

bs::signer::RequestId HeadlessListener::newRequestId()
{
   return ++id_;
}

void HeadlessListener::setConnected(bool flag)
{
   if (flag) {
      wasErrorReported_ = false;
      isShuttingDown_ = false;
   }
   else {
      isShuttingDown_ = true;
   }
}

void HeadlessListener::OnDataReceived(const std::string& data)
{
   headless::RequestPacket packet;
   if (!packet.ParseFromString(data)) {
      logger_->error("[HeadlessListener] failed to parse request packet");
      return;
   }

   if (packet.id() > id_) {
      logger_->error("[HeadlessListener] reply id inconsistency: {} > {}", packet.id(), id_);
      tryEmitError(HeadlessContainer::InvalidProtocol
         , QObject::tr("reply id inconsistency"));
      return;
   }

   if (packet.type() == headless::DisconnectionRequestType) {
      processDisconnectNotification();
      return;
   }

   if (packet.type() == headless::AuthenticationRequestType) {
      headless::AuthenticationReply response;
      if (!response.ParseFromString(packet.data())) {
         logger_->error("[HeadlessListener] failed to parse auth reply");

         tryEmitError(HeadlessContainer::SerializationFailed
            , QObject::tr("failed to parse auth reply"));
         return;
      }

      if (HeadlessContainer::mapNetworkType(response.nettype()) != netType_) {
         logger_->error("[HeadlessListener] network type mismatch");
         tryEmitError(HeadlessContainer::NetworkTypeMismatch
            , QObject::tr("Network type mismatch (Mainnet / Testnet)"));
         return;
      }

      // BIP 150/151 should be be complete by this point.
      isReady_ = true;
      parent_->onAuthenticated();
   } else {
      parent_->onPacketReceived(packet);
   }
}

void HeadlessListener::OnConnected()
{
   if (isConnected_) {
      logger_->error("already connected");
      return;
   }

   isConnected_ = true;
   logger_->debug("[HeadlessListener] Connected");
   parent_->onConnected();
}

void HeadlessListener::OnDisconnected()
{
   if (isShuttingDown_) {
      return;
   }
   SPDLOG_LOGGER_ERROR(logger_, "remote signer disconnected unexpectedly");
   isConnected_ = false;
   isReady_ = false;
   tryEmitError(HeadlessContainer::SocketFailed
      , QObject::tr("TCP connection was closed unexpectedly"));
}

void HeadlessListener::OnError(DataConnectionListener::DataConnectionError errorCode)
{
   logger_->debug("[HeadlessListener] error {}", errorCode);
   isConnected_ = false;
   isReady_ = false;

   switch (errorCode) {
      case NoError:
         assert(false);
         break;
      case UndefinedSocketError:
         tryEmitError(HeadlessContainer::SocketFailed, QObject::tr("Socket error"));
         break;
      case HostNotFoundError:
         tryEmitError(HeadlessContainer::HostNotFound, QObject::tr("Host not found"));
         break;
      case HandshakeFailed:
         tryEmitError(HeadlessContainer::HandshakeFailed, QObject::tr("Handshake failed"));
         break;
      case SerializationFailed:
         tryEmitError(HeadlessContainer::SerializationFailed, QObject::tr("Serialization failed"));
         break;
      case HeartbeatWaitFailed:
         tryEmitError(HeadlessContainer::HeartbeatWaitFailed, QObject::tr("Connection lost"));
         break;
      case ConnectionTimeout:
         tryEmitError(HeadlessContainer::ConnectionTimeout, QObject::tr("Connection timeout"));
         break;
   }
}

bs::signer::RequestId HeadlessListener::Send(headless::RequestPacket packet, bool updateId)
{
   if (!connection_) {
      return 0;
   }

   bs::signer::RequestId id = 0;
   if (updateId) {
      id = newRequestId();
      packet.set_id(id);
   }

   if (!connection_->send(packet.SerializeAsString())) {
      logger_->error("[HeadlessListener] Failed to send request packet");
      parent_->onDisconnected();
      return 0;
   }
   return id;
}

HeadlessContainer::HeadlessContainer(const std::shared_ptr<spdlog::logger> &logger, OpMode opMode
   , SignerCallbackTarget *sct)
   : WalletSignerContainer(logger, sct, opMode)
{
   qRegisterMetaType<headless::RequestPacket>();
   qRegisterMetaType<std::shared_ptr<bs::sync::hd::Leaf>>();
   qRegisterMetaType<ConnectionError>("ConnectionError");
}

bs::signer::RequestId HeadlessContainer::Send(const headless::RequestPacket &packet, bool incSeqNo)
{
   if (!listener_) {
      return 0;
   }
   return listener_->Send(packet, incSeqNo);
}

void HeadlessContainer::ProcessSignTXResponse(unsigned int id, const std::string &data)
{
   headless::SignTxReply response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSignTXResponse] Failed to parse SignTxReply");
      sct_->txSigned(id, {}, bs::error::ErrorCode::FailedToParse);
      return;
   }
   const auto cb = signTxMap_.take(id);
   if (cb) {
      cb(BinaryData::fromString(response.signedtx())
         , static_cast<bs::error::ErrorCode>(response.errorcode()), {});
      return;
   }
   const auto cbSettl = cbSettlementSignTxMap_.take(id);
   if (cbSettl) {
      cbSettl(static_cast<bs::error::ErrorCode>(response.errorcode())
         , BinaryData::fromString(response.signedtx()));
   }
   sct_->txSigned(id, BinaryData::fromString(response.signedtx())
      , static_cast<bs::error::ErrorCode>(response.errorcode()));
}

void HeadlessContainer::ProcessSettlementSignTXResponse(unsigned int id, const std::string &data)
{
   headless::SignTxReply response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSettlementSignTXResponse] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlementSignTxMap_.take(id);
   if (cb) {
      cb(static_cast<bs::error::ErrorCode>(response.errorcode())
         , BinaryData::fromString(response.signedtx()));
   }
   sct_->txSigned(id, BinaryData::fromString(response.signedtx())
      , static_cast<bs::error::ErrorCode>(response.errorcode()));
}

void HeadlessContainer::ProcessPubResolveResponse(unsigned int id, const std::string &data)
{
   headless::SignTxReply response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessPubResolveResponse] failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSignerStateMap_.take(id);
   if (cb) {
      Codec_SignerState::SignerState state;
      state.ParseFromString(response.signedtx());
      cb(static_cast<bs::error::ErrorCode>(response.errorcode()), state);
   }
   else {
      logger_->error("[HeadlessContainer::ProcessPubResolveResponse] failed to find reqId {}", id);
      sct_->onError(id, "failed to find original request");
   }
}

void HeadlessContainer::ProcessCreateHDLeafResponse(unsigned int id, const std::string &data)
{
   headless::CreateHDLeafResponse response;

   auto cb = cbCCreateLeafMap_.take(id);

   if (!cb) {
      logger_->debug("[HeadlessContainer::ProcessCreateHDLeafResponse] no CB for create leaf response");
   }

   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessCreateHDLeafResponse] Failed to parse CreateHDWallet reply");

      if (cb) {
         cb(bs::error::ErrorCode::FailedToParse, {});
      }

      return;
   }

   bs::error::ErrorCode result = static_cast<bs::error::ErrorCode>(response.errorcode());

   if (result == bs::error::ErrorCode::NoError) {
      const auto path = bs::hd::Path::fromString(response.leaf().path());
      logger_->debug("[HeadlessContainer::ProcessCreateHDLeafResponse] HDLeaf {} created", response.leaf().path());
   } else {
      logger_->error("[HeadlessContainer::ProcessCreateHDLeafResponse] failed to create leaf: {}"
                     , response.errorcode());
   }

   if (cb) {
      cb(result, response.leaf().walletid());
   }
}

void HeadlessContainer::ProcessEnableTradingInWalletResponse(unsigned int id, const std::string& data)
{
   headless::EnableTradingInWalletResponse response;

   auto cb = cbUpdateWalletMap_.take(id);

   if (!cb) {
      logger_->debug("[HeadlessContainer::ProcessEnableTradingInWalletResponse] no CB for promote HD Wallet response");
   }

   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessEnableTradingInWalletResponse] Failed to parse EnableXBTTradingCb reply");

      if (cb) {
         cb(bs::error::ErrorCode::FailedToParse, {});
      }

      return;
   }

   bs::error::ErrorCode result = static_cast<bs::error::ErrorCode>(response.errorcode());

   if (result == bs::error::ErrorCode::NoError) {
      logger_->debug("[HeadlessContainer::ProcessEnableTradingInWalletResponse] HDWallet {} updated", response.rootwalletid());
   } else {
      logger_->error("[HeadlessContainer::ProcessEnableTradingInWalletResponse] failed to update: {}"
                     , response.errorcode());
   }

   if (cb) {
      cb(result, response.rootwalletid());
   }
}

void HeadlessContainer::ProcessPromoteWalletResponse(unsigned int id, const std::string &data)
{
   headless::PromoteWalletToPrimaryResponse response;

   auto cb = cbUpdateWalletMap_.take(id);

   if (!cb) {
      logger_->debug("[HeadlessContainer::ProcessPromoteWalletResponse] no CB for promote HD Wallet response");
   }

   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessPromoteWalletResponse] Failed to parse EnableXBTTradingCb reply");

      if (cb) {
         cb(bs::error::ErrorCode::FailedToParse, {});
      }

      return;
   }

   bs::error::ErrorCode result = static_cast<bs::error::ErrorCode>(response.errorcode());

   if (result == bs::error::ErrorCode::NoError) {
      logger_->debug("[HeadlessContainer::ProcessPromoteWalletResponse] HDWallet {} updated", response.rootwalletid());
   } else {
      logger_->error("[HeadlessContainer::ProcessPromoteWalletResponse] failed to update: {}"
                     , response.errorcode());
   }

   if (cb) {
      cb(result, response.rootwalletid());
   }
}

void HeadlessContainer::ProcessGetHDWalletInfoResponse(unsigned int id, const std::string &data)
{
   headless::GetHDWalletInfoResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessGetHDWalletInfoResponse] Failed to parse GetHDWalletInfo reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   if (response.error().empty()) {
      sct_->walletInfo(id, response);
   }
   else {
      missingWallets_.insert(response.rootwalletid());
      sct_->onError(id, response.error());
   }
}

void HeadlessContainer::ProcessAutoSignActEvent(unsigned int id, const std::string &data)
{
   headless::AutoSignActEvent event;
   if (!event.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessAutoSignActEvent] Failed to parse SetLimits reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   sct_->autoSignStateChanged(static_cast<bs::error::ErrorCode>(event.errorcode())
      , event.rootwalletid());
}

void HeadlessContainer::ProcessSetUserId(const std::string &data)
{
   headless::SetUserIdResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSetUserId] failed to parse response");
      return;
   }
   if (!response.auth_wallet_id().empty() && (response.response() == headless::AWR_NoError)) {
      sct_->authLeafAdded(response.auth_wallet_id());
   }
   else {   // unset auth wallet
      sct_->authLeafAdded({});
   }
}

bs::signer::RequestId HeadlessContainer::signTXRequest(const bs::core::wallet::TXSignRequest &txSignReq
   , SignContainer::TXSignMode mode, bool keepDuplicatedRecipients)
{
   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainer::signTXRequest] Invalid TXSignRequest");
      return 0;
   }
   const auto &request = bs::signer::coreTxRequestToPb(txSignReq, keepDuplicatedRecipients);

   headless::RequestPacket packet;
   switch (mode) {
   case TXSignMode::Full:
      packet.set_type(headless::SignTxRequestType);
      break;

   case TXSignMode::Partial:
      packet.set_type(headless::SignPartialTXRequestType);
      break;

   case TXSignMode::AutoSign:
      packet.set_type(headless::AutoSignFullType);
      break;

   default:
      logger_->error("[{}] unknown sign mode {}", __func__, (int)mode);
      break;
   }
   packet.set_data(request.SerializeAsString());
   const auto id = Send(packet);
   signRequests_.insert(id);
   return id;
}

void HeadlessContainer::signTXRequest(const bs::core::wallet::TXSignRequest& txReq
   , const std::function<void(const BinaryData &signedTX, bs::error::ErrorCode
      , const std::string& errorReason)>& cb
   , TXSignMode mode, bool keepDuplicatedRecipients)
{
   if (!txReq.isValid()) {
      logger_->error("[HeadlessContainer::signTXRequest] Invalid TXSignRequest");
      cb({}, bs::error::ErrorCode::InternalError, "invalid request");
      return;
   }
   const auto& request = bs::signer::coreTxRequestToPb(txReq, keepDuplicatedRecipients);

   headless::RequestPacket packet;
   switch (mode) {
   case TXSignMode::Full:
      packet.set_type(headless::SignTxRequestType);
      break;

   case TXSignMode::Partial:
      packet.set_type(headless::SignPartialTXRequestType);
      break;

   case TXSignMode::AutoSign:
      packet.set_type(headless::AutoSignFullType);
      break;

   default:
      logger_->error("[{}] unknown sign mode {}", __func__, (int)mode);
      break;
   }
   packet.set_data(request.SerializeAsString());
   const auto id = Send(packet);
   if (id) {
      signTxMap_.put(id, cb);
   }
   else {
      cb({}, bs::error::ErrorCode::InternalError, "failed to send");
   }
}

bs::signer::RequestId HeadlessContainer::signSettlementTXRequest(const bs::core::wallet::TXSignRequest &txSignReq
   , const bs::sync::PasswordDialogData &dialogData, SignContainer::TXSignMode mode
   , bool keepDuplicatedRecipients
   , const SignTxCb &cb)
{
   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainer::signSettlementTXRequest] Invalid TXSignRequest");
      return 0;
   }

   headless::SignTxRequest signTxRequest = bs::signer::coreTxRequestToPb(txSignReq, keepDuplicatedRecipients);

   headless::SignSettlementTxRequest settlementRequest;
   *(settlementRequest.mutable_signtxrequest()) = signTxRequest;
   *(settlementRequest.mutable_passworddialogdata()) = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::SignSettlementTxRequestType);

   packet.set_data(settlementRequest.SerializeAsString());
   const auto reqId = Send(packet);
   cbSettlementSignTxMap_.put(reqId, cb);
   return reqId;
}

bs::signer::RequestId HeadlessContainer::signSettlementPartialTXRequest(
   const bs::core::wallet::TXSignRequest &txSignReq
   , const bs::sync::PasswordDialogData &dialogData
   , const SignTxCb &cb)
{
   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainer::signSettlementPartialTXRequest] Invalid TXSignRequest");
      return 0;
   }

   headless::SignTxRequest signTxRequest = bs::signer::coreTxRequestToPb(txSignReq);

   headless::SignSettlementTxRequest settlementRequest;
   *(settlementRequest.mutable_signtxrequest()) = signTxRequest;
   *(settlementRequest.mutable_passworddialogdata()) = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::SignSettlementPartialTxType);
   packet.set_data(settlementRequest.SerializeAsString());

   const auto reqId = Send(packet);
   cbSettlementSignTxMap_.put(reqId, cb);
   return reqId;
}

bs::signer::RequestId HeadlessContainer::resolvePublicSpenders(const bs::core::wallet::TXSignRequest &txReq
   , const SignerStateCb &cb)
{
   const auto signTxRequest = bs::signer::coreTxRequestToPb(txReq);
   headless::RequestPacket packet;
   packet.set_type(headless::ResolvePublicSpendersType);
   packet.set_data(signTxRequest.SerializeAsString());

   const auto reqId = Send(packet);
   cbSignerStateMap_.put(reqId, cb);
   return reqId;
}

static void fillSettlementData(headless::SettlementData *settlData, const bs::core::wallet::SettlementData &sd)
{
   settlData->set_settlement_id(sd.settlementId.toBinStr());
   settlData->set_counterparty_pubkey(sd.cpPublicKey.toBinStr());
   settlData->set_my_pubkey_first(sd.ownKeyFirst);
}

bs::signer::RequestId HeadlessContainer::signSettlementPayoutTXRequest(const bs::core::wallet::TXSignRequest &txSignReq
   , const bs::core::wallet::SettlementData &sd, const bs::sync::PasswordDialogData &dialogData
   , const SignTxCb &cb)
{
   if ((txSignReq.armorySigner_.getTxInCount() != 1) ||
      (txSignReq.armorySigner_.getTxOutCount() != 1) ||
      sd.settlementId.empty()) {
      logger_->error("[HeadlessContainer::signSettlementPayoutTXRequest] Invalid"
         " PayoutTXSignRequest: in:{} out:{} settlId:{}", txSignReq.armorySigner_.getTxInCount()
         , txSignReq.armorySigner_.getTxOutCount(), sd.settlementId.toHexStr());
      return 0;
   }
   headless::SignSettlementPayoutTxRequest settlementRequest;
   auto request = settlementRequest.mutable_signpayouttxrequest();
   request->set_fee(txSignReq.fee);
   request->set_tx_hash(txSignReq.txHash.toBinStr());
   request->set_signerstate(txSignReq.serializeState().SerializeAsString());

   fillSettlementData(request->mutable_settlement_data(), sd);
   *(settlementRequest.mutable_passworddialogdata()) = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::SignSettlementPayoutTxType);
   packet.set_data(settlementRequest.SerializeAsString());

   const auto reqId = Send(packet);
   cbSettlementSignTxMap_.put(reqId, cb);
   return reqId;
}

bs::signer::RequestId HeadlessContainer::signAuthRevocation(const std::string &walletId, const bs::Address &authAddr
   , const UTXO &utxo, const bs::Address &bsAddr, const SignTxCb &cb)
{
   headless::SignAuthAddrRevokeRequest request;
   request.set_wallet_id(walletId);
   request.set_auth_address(authAddr.display());
   request.set_utxo(utxo.serialize().toBinStr());
   request.set_validation_address(bsAddr.display());

   headless::RequestPacket packet;
   packet.set_type(headless::SignAuthAddrRevokeType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   cbSettlementSignTxMap_.put(reqId, cb);
   signRequests_.insert(reqId);
   return reqId;
}

bs::signer::RequestId HeadlessContainer::updateDialogData(const bs::sync::PasswordDialogData &dialogData, uint32_t dialogId)
{
   headless::UpdateDialogDataRequest updateDialogDataRequest;
   updateDialogDataRequest.set_dialogid(dialogId);
   *(updateDialogDataRequest.mutable_passworddialogdata()) = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::UpdateDialogDataType);

   packet.set_data(updateDialogDataRequest.SerializeAsString());
   const auto reqId = Send(packet);
   return reqId;
}

bs::signer::RequestId HeadlessContainer::CancelSignTx(const BinaryData &txId)
{
   headless::CancelSignTx request;
   request.set_tx_id(txId.toBinStr());

   headless::RequestPacket packet;
   packet.set_type(headless::CancelSignTxRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

bs::signer::RequestId HeadlessContainer::setUserId(const BinaryData &userId, const std::string &walletId)
{
   if (!listener_) {
      logger_->warn("[HeadlessContainer::SetUserId] listener not set yet");
      return 0;
   }

   bs::sync::PasswordDialogData info;
   info.setValue(PasswordDialogData::WalletId, QString::fromStdString(walletId));

   headless::SetUserIdRequest request;
   auto dialogData = request.mutable_passworddialogdata();
   *dialogData = info.toProtobufMessage();
   if (!userId.empty()) {
      request.set_userid(userId.toBinStr());
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SetUserIdType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

bs::signer::RequestId HeadlessContainer::syncCCNames(const std::vector<std::string> &ccNames)
{
   logger_->debug("[HeadlessContainer::syncCCNames] syncing {} CCs", ccNames.size());
   headless::SyncCCNamesData request;
   for (const auto &cc : ccNames) {
      request.add_ccnames(cc);
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SyncCCNamesType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

bool HeadlessContainer::createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &path
   , const std::vector<bs::wallet::PasswordData> &pwData, bs::sync::PasswordDialogData dialogData
   , const CreateHDLeafCb &cb)
{
   if (rootWalletId.empty() || (path.length() != 3)) {
      logger_->error("[HeadlessContainer::createHDLeaf] Invalid input data for HD wallet creation");
      return false;
   }
   headless::CreateHDLeafRequest request;
   request.set_rootwalletid(rootWalletId);
   request.set_path(path.toString());

   if (!pwData.empty()) {
      if (!pwData[0].salt.empty()) {
         request.set_salt(pwData[0].salt.toBinStr());
      }
   }
   dialogData.setValue(PasswordDialogData::WalletId, QString::fromStdString(rootWalletId));

   auto requestDialogData = request.mutable_passworddialogdata();
   *requestDialogData = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::CreateHDLeafRequestType);
   packet.set_data(request.SerializeAsString());
   auto createLeafRequestId = Send(packet);
   if (createLeafRequestId == 0) {
      logger_->error("[HeadlessContainer::createHDLeaf] failed to send request");
      return false;
   }

   if (cb) {
      cbCCreateLeafMap_.put(createLeafRequestId, cb);
   } else {
      logger_->warn("[HeadlessContainer::createHDLeaf] cb not set for leaf creation {}"
                     , path.toString());
   }
   return true;
}

bool HeadlessContainer::enableTradingInHDWallet(const std::string& rootWalletId
   , const BinaryData &userId, bs::sync::PasswordDialogData dialogData
   , const WalletSignerContainer::UpdateWalletStructureCB& cb)
{
   headless::EnableTradingInWalletRequest request;
   request.set_rootwalletid(rootWalletId);
   request.set_user_id(userId.toBinStr());

   dialogData.setValue(PasswordDialogData::WalletId, QString::fromStdString(rootWalletId));

   auto requestDialogData = request.mutable_passworddialogdata();
   *requestDialogData = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::EnableTradingInWalletType);
   packet.set_data(request.SerializeAsString());
   auto requestId = Send(packet);

   if (requestId == 0) {
      logger_->error("[HeadlessContainer::enableTradingInHDWallet] failed to send request");
      return false;
   }

   if (cb) {
      cbUpdateWalletMap_.put(requestId, cb);
   }

   return true;
}

bool HeadlessContainer::promoteWalletToPrimary(const std::string& rootWalletId
      , bs::sync::PasswordDialogData dialogData, const UpdateWalletStructureCB& cb)
{
   headless::PromoteWalletToPrimaryRequest request;
   request.set_rootwalletid(rootWalletId);

   dialogData.setValue(PasswordDialogData::WalletId, QString::fromStdString(rootWalletId));

   auto requestDialogData = request.mutable_passworddialogdata();
   *requestDialogData = dialogData.toProtobufMessage();

   headless::RequestPacket packet;
   packet.set_type(headless::PromoteWalletToPrimaryType);
   packet.set_data(request.SerializeAsString());
   auto requestId = Send(packet);

   if (requestId == 0) {
      logger_->error("[HeadlessContainer::promoteWalletToPrimary] failed to send request");
      return false;
   }

   if (cb) {
      cbUpdateWalletMap_.put(requestId, cb);
   }

   return true;
}

bs::signer::RequestId HeadlessContainer::DeleteHDRoot(const std::string &rootWalletId)
{
   SPDLOG_LOGGER_ERROR(logger_, "unimplemented");
   return 0;
}

bs::signer::RequestId HeadlessContainer::DeleteHDLeaf(const std::string &leafWalletId)
{
   SPDLOG_LOGGER_ERROR(logger_, "unimplemented");
   return 0;
}

bs::signer::RequestId HeadlessContainer::customDialogRequest(bs::signer::ui::GeneralDialogType signerDialog
   , const QVariantMap &data)
{
   // serialize variant data
   QByteArray ba;
   QDataStream stream(&ba, QIODevice::WriteOnly);
   stream << data;

   headless::CustomDialogRequest request;
   request.set_dialogname(bs::signer::ui::getGeneralDialogName(signerDialog).toStdString());
   request.set_variantdata(ba.data(), size_t(ba.size()));

   headless::RequestPacket packet;
   packet.set_type(headless::ExecCustomDialogRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

bs::signer::RequestId HeadlessContainer::GetInfo(const std::string &rootWalletId)
{
   if (rootWalletId.empty()) {
      return 0;
   }
   headless::GetHDWalletInfoRequest request;
   request.set_rootwalletid(rootWalletId);

   headless::RequestPacket packet;
   packet.set_type(headless::GetHDWalletInfoRequestType);
   packet.set_data(request.SerializeAsString());
   return Send(packet);
}

bool HeadlessContainer::isReady() const
{
   return (listener_ != nullptr) && listener_->isReady();
}

bool HeadlessContainer::isWalletOffline(const std::string &walletId) const
{
   return ((missingWallets_.find(walletId) != missingWallets_.end())
      || (woWallets_.find(walletId) != woWallets_.end()));
}

void HeadlessContainer::createSettlementWallet(const bs::Address &authAddr
   , const std::function<void(const SecureBinaryData &)> &cb)
{
   headless::CreateSettlWalletRequest request;
   request.set_auth_address(authAddr.display());

   headless::RequestPacket packet;
   packet.set_data(request.SerializeAsString());
   packet.set_type(headless::CreateSettlWalletType);
   const auto reqId = Send(packet);
   cbSettlWalletMap_.put(reqId, cb);
}

void HeadlessContainer::setSettlementID(const std::string &walletId, const SecureBinaryData &id
   , const std::function<void(bool, const SecureBinaryData&)> &cb)
{
   headless::SetSettlementIdRequest request;
   request.set_wallet_id(walletId);
   request.set_settlement_id(id.toBinStr());

   headless::RequestPacket packet;
   packet.set_data(request.SerializeAsString());
   packet.set_type(headless::SetSettlementIdType);
   const auto reqId = Send(packet);
   cbSettlPubkeyMap_.put(reqId, cb);
}

void HeadlessContainer::getSettlementPayinAddress(const std::string &walletId
   , const bs::core::wallet::SettlementData &sd
   , const std::function<void(bool, bs::Address)> &cb)
{
   headless::SettlPayinAddressRequest request;
   request.set_wallet_id(walletId);
   fillSettlementData(request.mutable_settlement_data(), sd);

   headless::RequestPacket packet;
   packet.set_data(request.SerializeAsString());
   packet.set_type(headless::GetSettlPayinAddrType);
   const auto reqId = Send(packet);
   cbPayinAddrMap_.put(reqId, cb);
}

void HeadlessContainer::getRootPubkey(const std::string &walletID
   , const std::function<void(bool, const SecureBinaryData &)> &cb)
{
   headless::SettlGetRootPubkeyRequest request;
   request.set_wallet_id(walletID);

   headless::RequestPacket packet;
   packet.set_data(request.SerializeAsString());
   packet.set_type(headless::SettlGetRootPubkeyType);
   const auto reqId = Send(packet);
   cbSettlPubkeyMap_.put(reqId, cb);
}

void HeadlessContainer::getAddressPubkey(const std::string& walletID
   , const std::string& address, const std::function<void(const SecureBinaryData&)>& cb)
{
   headless::AddressPubKeyRequest request;
   request.set_wallet_id(walletID);
   request.set_address(address);

   headless::RequestPacket packet;
   packet.set_data(request.SerializeAsString());
   packet.set_type(headless::AddressPubkeyRequestType);
   const auto reqId = Send(packet);
   cbSettlWalletMap_.put(reqId, cb);
}

void HeadlessContainer::getChatNode(const std::string &walletID
   , const std::function<void(const BIP32_Node &)> &cb)
{
   headless::ChatNodeRequest request;
   request.set_wallet_id(walletID);

   headless::RequestPacket packet;
   packet.set_data(request.SerializeAsString());
   packet.set_type(headless::ChatNodeRequestType);
   const auto reqId = Send(packet);
   cbChatNodeMap_.put(reqId, cb);
}

void HeadlessContainer::syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &cb)
{
   headless::RequestPacket packet;
   packet.set_type(headless::SyncWalletInfoType);
   const auto reqId = Send(packet);
   cbWalletInfoMap_.put(reqId, cb);
}

void HeadlessContainer::syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &cb)
{
   headless::SyncWalletRequest request;
   request.set_walletid(id);

   headless::RequestPacket packet;
   packet.set_type(headless::SyncHDWalletType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   cbHDWalletMap_.put(reqId, cb);
}

void HeadlessContainer::syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &cb)
{
   headless::SyncWalletRequest request;
   request.set_walletid(id);

   headless::RequestPacket packet;
   packet.set_type(headless::SyncWalletType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   cbWalletMap_.put(reqId, cb);
}

void HeadlessContainer::syncAddressComment(const std::string &walletId, const bs::Address &addr
   , const std::string &comment)
{
   headless::SyncCommentRequest request;
   request.set_walletid(walletId);
   request.set_address(addr.display());
   request.set_comment(comment);

   headless::RequestPacket packet;
   packet.set_type(headless::SyncCommentType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

void HeadlessContainer::syncTxComment(const std::string &walletId, const BinaryData &txHash
   , const std::string &comment)
{
   headless::SyncCommentRequest request;
   request.set_walletid(walletId);
   request.set_txhash(txHash.toBinStr());
   request.set_comment(comment);

   headless::RequestPacket packet;
   packet.set_type(headless::SyncCommentType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

void HeadlessContainer::setSettlAuthAddr(const std::string &walletId, const BinaryData &settlId
   , const bs::Address &addr)
{
   headless::SettlementAuthAddress request;
   request.set_wallet_id(walletId);
   request.set_settlement_id(settlId.toBinStr());
   request.set_auth_address(addr.display());

   headless::RequestPacket packet;
   packet.set_type(headless::SettlementAuthType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

void HeadlessContainer::getSettlAuthAddr(const std::string &walletId, const BinaryData &settlId
   , const std::function<void(const bs::Address &)> &cb)
{
   headless::SettlementAuthAddress request;
   request.set_wallet_id(walletId);
   request.set_settlement_id(settlId.toBinStr());

   headless::RequestPacket packet;
   packet.set_type(headless::SettlementAuthType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   cbSettlAuthMap_.put(reqId, cb);
}

void HeadlessContainer::setSettlCP(const std::string &walletId, const BinaryData &payinHash, const BinaryData &settlId
   , const BinaryData &cpPubKey)
{
   headless::SettlementCounterparty request;
   request.set_wallet_id(walletId);
   request.set_payin_hash(payinHash.toBinStr());
   request.set_settlement_id(settlId.toBinStr());
   request.set_cp_public_key(cpPubKey.toBinStr());

   headless::RequestPacket packet;
   packet.set_type(headless::SettlementCPType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

void HeadlessContainer::getSettlCP(const std::string &walletId, const BinaryData &payinHash
   , const std::function<void(const BinaryData &, const BinaryData &)> &cb)
{
   headless::SettlementCounterparty request;
   request.set_wallet_id(walletId);
   request.set_payin_hash(payinHash.toBinStr());

   headless::RequestPacket packet;
   packet.set_type(headless::SettlementCPType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   cbSettlCPMap_.put(reqId, cb);
}

void HeadlessContainer::extendAddressChain(
   const std::string &walletId, unsigned count, bool extInt,
   const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &cb)
{
   headless::ExtendAddressChainRequest request;
   request.set_wallet_id(walletId);
   request.set_count(count);
   request.set_ext_int(extInt);

   headless::RequestPacket packet;
   packet.set_type(headless::ExtendAddressChainType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   if (!reqId) {
      if (cb) {
         cb({});
      }
      return;
   }
   cbExtAddrsMap_.put(reqId, cb);
}

void HeadlessContainer::syncNewAddresses(const std::string &walletId
   , const std::vector<std::string> &inData
   , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &cb)
{
   headless::SyncNewAddressRequest request;
   request.set_wallet_id(walletId);
   for (const auto &in : inData) {
      auto addrData = request.add_addresses();
      addrData->set_index(in);
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SyncNewAddressType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   if (!reqId) {
      if (cb) {
         cb({});
      }
      return;
   }
   cbExtAddrsMap_.put(reqId, cb);
}

void HeadlessContainer::syncAddressBatch(
   const std::string &walletId, const std::set<BinaryData>& addrSet,
   std::function<void(bs::sync::SyncState)> cb)
{
   headless::SyncAddressesRequest request;
   request.set_wallet_id(walletId);
   for (const auto &addr : addrSet) {
      request.add_addresses(addr.toBinStr());
   }

   headless::RequestPacket packet;
   packet.set_type(headless::SyncAddressesType);
   packet.set_data(request.SerializeAsString());
   const auto reqId = Send(packet);
   if (!reqId) {
      if (cb) {
         cb(bs::sync::SyncState::Failure);
      }
      return;
   }
   cbSyncAddrsMap_.put(reqId, cb);
}

void HeadlessContainer::ProcessUpdateStatus(const std::string &data)
{
   headless::UpdateStatus evt;
   if (!evt.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessUpdateControlPasswordStatus] Failed to parse reply");
      return;
   }

   if (evt.status() == headless::UpdateStatus_WalletsStatus_NoWallets) {
      sct_->newWalletPrompt();
   }
   else if (evt.status() == headless::UpdateStatus_WalletsStatus_ReadyToSync) {
      sct_->walletsReady();
   }
}

void HeadlessContainer::ProcessSettlWalletCreate(unsigned int id, const std::string &data)
{
   headless::CreateSettlWalletResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSettlWalletCreate] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlWalletMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   cb(SecureBinaryData::fromString(response.public_key()));
}

void HeadlessContainer::ProcessSetSettlementId(unsigned int id, const std::string &data)
{
   headless::SetSettlementIdResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSetSettlementId] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlPubkeyMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   cb(response.success(), SecureBinaryData::fromString(response.public_key()));
}

void HeadlessContainer::ProcessGetPayinAddr(unsigned int id, const std::string &data)
{
   headless::SettlPayinAddressResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessGetPayinAddr] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbPayinAddrMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   auto addrObj = bs::Address::fromAddressString(response.address());
   cb(response.success(), addrObj);
}

void HeadlessContainer::ProcessSettlGetRootPubkey(unsigned int id, const std::string &data)
{
   headless::SettlGetRootPubkeyResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSettlGetRootPubkey] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlPubkeyMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   cb(response.success(), SecureBinaryData::fromString(response.public_key()));
}

void HeadlessContainer::ProcessChatNodeResponse(unsigned int id, const std::string &data)
{
   headless::ChatNodeResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessChatNodeResponse] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbChatNodeMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }

   if (response.wallet_id().empty()) {
      logger_->error("[HeadlessContainer::ProcessChatNodeResponse] wallet not found");
      sct_->onError(id, "wallet not found for chat node");
   }
   else {
      BIP32_Node chatNode;
      try {
         chatNode.initFromBase58(SecureBinaryData::fromString(response.b58_chat_node()));
      } catch (const std::exception &e) {
         logger_->error("[HeadlessContainer::ProcessChatNodeResponse] failed to deserialize BIP32 node: {}", e.what());
      }
      cb(chatNode);
   }
}

void HeadlessContainer::ProcessSettlAuthResponse(unsigned int id, const std::string &data)
{
   headless::SettlementAuthAddress response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSettlAuthResponse] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlAuthMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }

   if (response.wallet_id().empty()) {
      logger_->error("[HeadlessContainer::ProcessSettlAuthResponse] wallet not found");
      sct_->onError(id, "wallet not found for settlement");
   } else {
      cb(bs::Address::fromAddressString(response.auth_address()));
   }
}

void HeadlessContainer::ProcessSettlCPResponse(unsigned int id, const std::string &data)
{
   headless::SettlementCounterparty response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSettlCPResponse] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlCPMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }

   if (response.wallet_id().empty()) {
      logger_->error("[HeadlessContainer::ProcessSettlCPResponse] wallet not found");
      sct_->onError(id, "wallet not found for payin");
   } else {
      cb(BinaryData::fromString(response.settlement_id())
         , BinaryData::fromString(response.cp_public_key()));
   }
}

void HeadlessContainer::ProcessWindowStatus(unsigned int id, const std::string &data)
{
   headless::WindowStatus message;
   if (!message.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessExtAddrChain] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   SPDLOG_LOGGER_DEBUG(logger_, "local signer visible: {}", message.visible());
   isWindowVisible_ = message.visible();
   sct_->windowIsVisible(isWindowVisible_);
}

void HeadlessContainer::ProcessAddrPubkeyResponse(unsigned int id, const std::string& data)
{
   headless::AddressPubKeyResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSettlGetRootPubkey] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSettlWalletMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   cb(SecureBinaryData::fromString(response.public_key()));
}

void HeadlessContainer::ProcessSyncWalletInfo(unsigned int id, const std::string &data)
{
   headless::SyncWalletInfoResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSyncWalletInfo] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbWalletInfoMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   std::vector<bs::sync::WalletInfo> result = bs::sync::WalletInfo::fromPbMessage(response);

   for (size_t i = 0; i < result.size(); ++i) {
      const auto &walletInfo = result.at(i);
      if (walletInfo.watchOnly) {
         woWallets_.insert(*walletInfo.ids.cbegin());
      } else {
         woWallets_.erase(*walletInfo.ids.cbegin());
      }
   }
   cb(result);
}

void HeadlessContainer::ProcessSyncHDWallet(unsigned int id, const std::string &data)
{
   headless::SyncHDWalletResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSyncHDWallet] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbHDWalletMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   const bool isWoRoot = (woWallets_.find(response.walletid()) != woWallets_.end());
   bs::sync::HDWalletData result;
   for (const auto &groupInfo : response.groups()) {
      bs::sync::HDWalletData::Group group;
      group.type = static_cast<bs::hd::CoinType>(groupInfo.type() | bs::hd::hardFlag);
      group.extOnly = groupInfo.ext_only();
      group.salt = BinaryData::fromString(groupInfo.salt());
      for (const auto &leafInfo : groupInfo.leaves()) {
         if (isWoRoot) {
            woWallets_.insert(leafInfo.id());
         }
         group.leaves.push_back({ { leafInfo.id() }, bs::hd::Path::fromString(leafInfo.path())
            , std::string{}, std::string{}, group.extOnly
            , BinaryData::fromString(leafInfo.extra_data()) });
      }
      result.groups.push_back(group);
   }
   cb(result);
}

void HeadlessContainer::ProcessSyncWallet(unsigned int id, const std::string &data)
{
   headless::SyncWalletResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSyncWallet] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbWalletMap_.take(id);
   if (!cb) {
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }

   bs::sync::WalletData result = bs::sync::WalletData::fromPbMessage(response);

   cb(result);
}

static bs::sync::SyncState mapFrom(headless::SyncState state)
{
   switch (state) {
   case headless::SyncState_Success:      return bs::sync::SyncState::Success;
   case headless::SyncState_NothingToDo:  return bs::sync::SyncState::NothingToDo;
   case headless::SyncState_Failure:      return bs::sync::SyncState::Failure;
   }
   return bs::sync::SyncState::Failure;
}

void HeadlessContainer::ProcessSyncAddresses(unsigned int id, const std::string &data)
{
   headless::SyncAddressesResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessSyncAddresses] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbSyncAddrsMap_.take(id);
   if (!cb) {
      logger_->error("[HeadlessContainer::ProcessSyncAddresses] no callback found for id {}", id);
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }

   const auto result = mapFrom(response.state());
   cb(result);
}

void HeadlessContainer::ProcessExtAddrChain(unsigned int id, const std::string &data)
{
   headless::ExtendAddressChainResponse response;
   if (!response.ParseFromString(data)) {
      logger_->error("[HeadlessContainer::ProcessExtAddrChain] Failed to parse reply");
      sct_->onError(id, "failed to parse");
      return;
   }
   const auto cb = cbExtAddrsMap_.take(id);
   if (!cb) {
      logger_->error("[HeadlessContainer::ProcessExtAddrChain] no callback found for id {}", id);
      sct_->onError(id, "no callback found for id " + std::to_string(id));
      return;
   }
   std::vector<std::pair<bs::Address, std::string>> result;
   for (int i = 0; i < response.addresses_size(); ++i) {
      const auto &addr = response.addresses(i);
      auto addrObj = bs::Address::fromAddressString(addr.address());
      result.push_back({ addrObj, addr.index() });
   }
   cb(result);
}


RemoteSigner::RemoteSigner(const std::shared_ptr<spdlog::logger> &logger
   , const QString &host, const QString &port, NetworkType netType
   , const std::shared_ptr<ConnectionManager>& connectionManager
   , SignerCallbackTarget *hct, OpMode opMode
   , const bool ephemeralDataConnKeys
   , const std::string& ownKeyFileDir
   , const std::string& ownKeyFileName
   , const bs::network::BIP15xNewKeyCb &inNewKeyCB)
   : HeadlessContainer(logger, opMode, hct)
   , host_(host), port_(port), netType_(netType)
   , ephemeralDataConnKeys_(ephemeralDataConnKeys)
   , ownKeyFileDir_(ownKeyFileDir)
   , ownKeyFileName_(ownKeyFileName)
   , cbNewKey_{inNewKeyCB}
   , connectionManager_{connectionManager}
{}

RemoteSigner::~RemoteSigner() noexcept
{
   isRestartScheduled_ = false;
   if (restartThread_.joinable()) {
      restartThread_.join();
   }
}

// Establish the remote connection to the signer.
void RemoteSigner::Start()
{
   if (!connection_) {
      RecreateConnection();
   }

   // If we're already connected, don't do more setup.
   if (headlessConnFinished_) {
      return;
   }

   {
      std::lock_guard<std::mutex> lock(mutex_);
      listener_ = std::make_shared<HeadlessListener>(logger_, connection_, netType_, this);
   }

   RemoteSigner::Connect();
}

bool RemoteSigner::Stop()
{
   return Disconnect();
}

void RemoteSigner::Connect()
{
   if (!connection_) {
      //QString is a disaster
      sct_->connError(ConnectionError::UnknownError
         , QString::fromLocal8Bit("[RemoteSigner::Connect] connection not created"));
   }

   if (connection_->isActive()) {
      return;
   }

   auto connectCb = [this]()
   {
//      listener_->wasErrorReported_ = false;
//      listener_->isShuttingDown_ = false;

      bool result = connection_->openConnection(host_.toStdString(), port_.toStdString(), listener_.get());
      if (!result) {
         sct_->connError(ConnectionError::SocketFailed
            , QString::fromLocal8Bit("[RemoteSigner::Connect] Failed to open connection to headless container"));
         return;
      }

      headlessConnFinished_ = true;
   };

   sct_->connected(host_.toStdString());
   headlessConnFinished_ = true;
//   return true;

   auto getCookieLbd = [this, connectCb]()
   {
      auto bip15xConn = std::dynamic_pointer_cast<Bip15xDataConnection>(connection_);
      if (bip15xConn == nullptr) {
         //cookie sharing is specific to BIP15x connections
         connectCb();
         return;
      }

      const std::string& serverName = host_.toStdString() + ":" + port_.toStdString();

      //check client uses cookie
      if (!bip15xConn->usesCookie()) {
         connectCb();
         return;
      }

      auto now = std::chrono::steady_clock::now();

      /*
      Probe the signer listen port. The signer creates the cookie before it starts
      listening for incoming connections. This is to make sure the new signer has had
      the time to replace existing cookie files before we try and read it, otherwise
      we could end up reading an expired cookie before it's replaced.

      We do not want to read the cookie after establishing the WS connection. This is
      because the server initiates the AEAD handhsake and we'd have to hang the client
      while it tries to read the cookie from disk. Instead, we make sure the cookie is
      available before initiating an encrypted connecting with the signer.

      This whole procedure will timeout after 5sec without success.
      */
      {
         SimpleSocket testSocket(host_.toStdString(), port_.toStdString());
         bool serverUp = false;

         while (std::chrono::steady_clock::now() - now < std::chrono::seconds(20)) {
            if (testSocket.testConnection()) {
               serverUp = true;
               break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
         }

         if (!serverUp) {
            sct_->connError(ConnectionError::SocketFailed
               , QString::fromLocal8Bit("[RemoteSigner::Connect] could not connect to server"));
            return;
         }
      }


      //try to read cookie file
      bool haveCookie = false;
      while (std::chrono::steady_clock::now() - now < std::chrono::seconds(20)) {
         std::string cookiePath = SystemFilePaths::appDataLocation() + "/" + "signerServerID";
         if (bip15xConn->addCookieKeyToKeyStore(cookiePath, serverName)) {
            haveCookie = true;
            break;
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      if (!haveCookie) {
         sct_->connError(ConnectionError::CookieError
            , QString::fromLocal8Bit("[RemoteSigner::Connect] failed to load cookie"));
         return;
      }

      connectCb();
   };

   std::thread connectThread(getCookieLbd);
   if (connectThread.joinable()) {
      connectThread.detach();
   }
}

bool RemoteSigner::Disconnect()
{
   if (!connection_) {
      return true;
   }
   if (listener_) {
      listener_->setConnected(false);
   }

   bool result = connection_->closeConnection();
   connection_.reset();
   return result;
}

void RemoteSigner::Authenticate()
{
   logger_->debug("[RemoteSigner::Authenticate]");
   mutex_.lock();
   if (!listener_) {
      mutex_.unlock();
      sct_->connError(UnknownError, tr("listener missing on authenticate"));
      return;
   }
   mutex_.unlock();

   headless::AuthenticationRequest request;
   request.set_nettype((netType_ == NetworkType::TestNet) ? headless::TestNetType : headless::MainNetType);

   headless::RequestPacket packet;
   packet.set_type(headless::AuthenticationRequestType);
   packet.set_data(request.SerializeAsString());
   Send(packet);
}

void RemoteSigner::RecreateConnection()
{
   logger_->info("[RemoteSigner::RecreateConnection] Restart connection...");

   bs::network::BIP15xParams params;
   params.ephemeralPeers = ephemeralDataConnKeys_;
   params.ownKeyFileDir = ownKeyFileDir_;
   params.ownKeyFileName = ownKeyFileName_;
   params.authMode = bs::network::BIP15xAuthMode::TwoWay;

   // Server's cookies are not available in remote mode
   if (opMode() == OpMode::Local || opMode() == OpMode::LocalInproc) {
      params.cookie = bs::network::BIP15xCookie::ReadServer;
   }

   try {
      bip15xTransport_ = std::make_shared<bs::network::TransportBIP15xClient>(logger_, params);
      bip15xTransport_->setKeyCb(cbNewKey_);
      WsDataConnectionParams wsParams;
      wsParams.timeoutSecs = kConnectTimeoutSec;
      auto wsConn = std::make_unique<WsDataConnection>(logger_, wsParams);
      auto conn = std::make_shared<Bip15xDataConnection>(logger_, std::move(wsConn), bip15xTransport_);
      connection_ = std::move(conn);

      headlessConnFinished_ = false;
   }
   catch (const std::exception &e) {
      logger_->error("[RemoteSigner::RecreateConnection] connection creation failed: {}", e.what());
      sct_->connError(ConnectionError::SocketFailed, tr("Connection creation failed"));
   }
}

void RemoteSigner::restartConnection()
{
   ScheduleRestart();
}

void RemoteSigner::reconnect()
{
   RecreateConnection();
   Start();
}

void RemoteSigner::ScheduleRestart()
{
   if (isRestartScheduled_) {
      return;
   }
   if (restartThread_.joinable()) {
      restartThread_.join();
   }
   isRestartScheduled_ = true;

   restartThread_ = std::thread([this] {
      const auto timeout = isLocal() ? kLocalReconnectPeriod : kRemoteReconnectPeriod;
      const int nbIters = timeout / kSleepPeriod;
      for (int i = 0; i < nbIters; ++i) {
         std::this_thread::sleep_for(kSleepPeriod);
         if (!isRestartScheduled_) {
            return;
         }
      }
      isRestartScheduled_ = false;
      reconnect();
   });
}

bool RemoteSigner::isOffline() const
{
   std::lock_guard<std::mutex> lock(mutex_);
   return (listener_ == nullptr);
}

void RemoteSigner::updatePeerKeys(const bs::network::BIP15xPeers &peers)
{
   if (!connection_) {
      RecreateConnection();
   }
   bip15xTransport_->updatePeerKeys(peers);
}

void RemoteSigner::onConnected()
{
   auto lbd = [this]()
   {
      Authenticate();
   };

   std::thread connThr(lbd);
   if (connThr.joinable()) {
      connThr.detach();
   }

}

void RemoteSigner::onAuthenticated()
{
   // Once the BIP 150/151 handshake is complete, it's safe to start sending
   // app-level data to the signer.
   sct_->onAuthComplete();
   sct_->onReady();
}

void RemoteSigner::onDisconnected()
{
   missingWallets_.clear();
   woWallets_.clear();

   // signRequests_ will be empty after that
   std::set<bs::signer::RequestId> tmpReqs = std::move(signRequests_);
   for (const auto &id : tmpReqs) {
      sct_->txSigned(id, {}, bs::error::ErrorCode::TxCancelled, "Signer disconnected");
   }
   for (const auto& signTx : signTxMap_.takeAll()) {
      signTx.second({}, bs::error::ErrorCode::TxCancelled, "Signer disconnected");
   }

   sct_->connTorn();
   restartConnection();
}

void RemoteSigner::onConnError(ConnectionError error, const QString &details)
{
   sct_->connError(error, details);
   restartConnection();
}

void RemoteSigner::onPacketReceived(const headless::RequestPacket &packet)
{
   signRequests_.erase(packet.id());

   switch (packet.type()) {
   case headless::SignTxRequestType:
   case headless::AutoSignFullType:
   case headless::SignPartialTXRequestType:
   case headless::SignSettlementPayoutTxType:
   case headless::SignAuthAddrRevokeType:
      ProcessSignTXResponse(packet.id(), packet.data());
      break;

   case headless::SignSettlementTxRequestType:
   case headless::SignSettlementPartialTxType:
      ProcessSettlementSignTXResponse(packet.id(), packet.data());
      break;

   case headless::ResolvePublicSpendersType:
      ProcessPubResolveResponse(packet.id(), packet.data());
      break;

   case headless::CreateHDLeafRequestType:
      ProcessCreateHDLeafResponse(packet.id(), packet.data());
      break;

   case headless::EnableTradingInWalletType:
      ProcessEnableTradingInWalletResponse(packet.id(), packet.data());
      break;

   case headless::PromoteWalletToPrimaryType:
      ProcessPromoteWalletResponse(packet.id(), packet.data());
      break;

   case headless::GetHDWalletInfoRequestType:
      ProcessGetHDWalletInfoResponse(packet.id(), packet.data());
      break;

   case headless::SetUserIdType:
      ProcessSetUserId(packet.data());
      break;

   case headless::AutoSignActType:
      ProcessAutoSignActEvent(packet.id(), packet.data());
      break;

   case headless::CreateSettlWalletType:
      ProcessSettlWalletCreate(packet.id(), packet.data());
      break;

   case headless::SetSettlementIdType:
      ProcessSetSettlementId(packet.id(), packet.data());
      break;

   case headless::GetSettlPayinAddrType:
      ProcessGetPayinAddr(packet.id(), packet.data());
      break;

   case headless::SettlGetRootPubkeyType:
      ProcessSettlGetRootPubkey(packet.id(), packet.data());
      break;

   case headless::AddressPubkeyRequestType:
      ProcessAddrPubkeyResponse(packet.id(), packet.data());
      break;

   case headless::SyncWalletInfoType:
      ProcessSyncWalletInfo(packet.id(), packet.data());
      break;

   case headless::SyncHDWalletType:
      ProcessSyncHDWallet(packet.id(), packet.data());
      break;

   case headless::SyncWalletType:
      ProcessSyncWallet(packet.id(), packet.data());
      break;

   case headless::SyncCommentType:
      break;   // normally no data will be returned on sync of comments

   case headless::SyncAddressesType:
      ProcessSyncAddresses(packet.id(), packet.data());
      break;

   case headless::ExtendAddressChainType:
   case headless::SyncNewAddressType:
      ProcessExtAddrChain(packet.id(), packet.data());
      break;

   case headless::WalletsListUpdatedType:
      sct_->walletsChanged();
      break;

   case headless::UpdateStatusType:
      ProcessUpdateStatus(packet.data());
      break;

   case headless::ChatNodeRequestType:
      ProcessChatNodeResponse(packet.id(), packet.data());
      break;

   case headless::SettlementAuthType:
      ProcessSettlAuthResponse(packet.id(), packet.data());
      break;

   case headless::SettlementCPType:
      ProcessSettlCPResponse(packet.id(), packet.data());
      break;

   case headless::WindowStatusType:
      ProcessWindowStatus(packet.id(), packet.data());
      break;

   default:
      logger_->error("[HeadlessContainer] Unknown packet type: {}", packet.type());
      break;
   }
}

LocalSigner::LocalSigner(const std::shared_ptr<spdlog::logger> &logger
   , const QString &homeDir, NetworkType netType, const QString &port
   , const std::shared_ptr<ConnectionManager>& connectionManager
   , SignerCallbackTarget *hct, const bool startSignerProcess
   , const std::string& ownKeyFileDir
   , const std::string& ownKeyFileName
   , double asSpendLimit
   , const bs::network::BIP15xNewKeyCb &inNewKeyCB)
   : RemoteSigner(logger, QLatin1String("127.0.0.1"), port, netType
      , connectionManager, hct, OpMode::Local, true
      , ownKeyFileDir, ownKeyFileName, inNewKeyCB)
      , homeDir_(homeDir), startProcess_(startSignerProcess), asSpendLimit_(asSpendLimit)
{}

LocalSigner::~LocalSigner() noexcept
{
   Stop();
}

QStringList LocalSigner::args() const
{
   auto walletsCopyDir = homeDir_ + QLatin1String("/copy");
   if (!QDir().exists(walletsCopyDir)) {
      walletsCopyDir = homeDir_ + QLatin1String("/signer");
   }

   QStringList result;
   result << QLatin1String("--guimode") << QLatin1String("litegui");
   switch (netType_) {
   case NetworkType::TestNet:
   case NetworkType::RegTest:
      result << QString::fromStdString("--testnet");
      break;
   case NetworkType::MainNet:
      result << QString::fromStdString("--mainnet");
      break;
   default:
      break;
   }

   // Among many other things, send the signer the terminal's BIP 150 ID key.
   // Processes reading keys from the disk are subject to attack.
   result << QLatin1String("--listen") << QLatin1String("127.0.0.1");
   result << QLatin1String("--accept_from") << QLatin1String("127.0.0.1");
   result << QLatin1String("--port") << port_;
   result << QLatin1String("--dirwallets") << walletsCopyDir;
   if (asSpendLimit_ > 0) {
      result << QLatin1String("--auto_sign_spend_limit")
         << QString::number(asSpendLimit_, 'f', 8);
   }
   result << QLatin1String("--terminal_id_key")
      << QString::fromStdString(bip15xTransport_->getOwnPubKey().toHexStr());

   return result;
}

void LocalSigner::Start()
{
   Stop();
   RemoteSigner::Start();

   if (startProcess_) {
      // If there's a previous headless process, stop it.
      headlessProcess_ = std::make_shared<QProcess>();

#ifdef Q_OS_WIN
      const auto signerAppPath = QCoreApplication::applicationDirPath() + QLatin1String("/blocksettle_signer.exe");
#elif defined (Q_OS_MACOS)
      auto bundleDir = QDir(QCoreApplication::applicationDirPath());
      bundleDir.cdUp();
      bundleDir.cdUp();
      bundleDir.cdUp();
      const auto signerAppPath = bundleDir.absoluteFilePath(QLatin1String("BlockSettle Signer.app/Contents/MacOS/BlockSettle Signer"));
#else
      const auto signerAppPath = QCoreApplication::applicationDirPath() + QLatin1String("/blocksettle_signer");
#endif
      if (!QFile::exists(signerAppPath)) {
         logger_->error("[LocalSigner::Start] Signer binary {} not found"
            , signerAppPath.toStdString());
         sct_->connError(UnknownError, tr("missing signer binary"));
         return;
      }

      const auto cmdArgs = args();
      logger_->debug("[LocalSigner::Start] starting {} {}"
         , signerAppPath.toStdString(), cmdArgs.join(QLatin1Char(' ')).toStdString());

#ifndef NDEBUG
      headlessProcess_->setProcessChannelMode(QProcess::MergedChannels);
      connect(headlessProcess_.get(), &QProcess::readyReadStandardOutput, this, [this] {
         logger_->debug("[LocalSigner] process output:\n{}"
            , headlessProcess_->readAllStandardOutput().toStdString());
      });
#endif

      headlessProcess_->start(signerAppPath, cmdArgs);
      if (!headlessProcess_->waitForStarted(kStartTimeout)) {
         logger_->error("[LocalSigner::Start] Failed to start process");
         headlessProcess_.reset();
         sct_->connError(UnknownError, tr("failed to start process"));
      }
   }
}

bool LocalSigner::Stop()
{
   RemoteSigner::Stop();

   if (headlessProcess_) {
      if (!headlessProcess_->waitForFinished(kKillTimeout)) {
         headlessProcess_->terminate();
         headlessProcess_->waitForFinished(kKillTimeout);
      }
      headlessProcess_.reset();
   }
   return true;
}

bool HeadlessListener::addCookieKeyToKeyStore(
   const std::string& path, const std::string& name)
{
   auto bip15xConnection =
      std::dynamic_pointer_cast<Bip15xDataConnection>(connection_);

   if (bip15xConnection == nullptr) {
      return false;
   }

   return bip15xConnection->addCookieKeyToKeyStore(path, name);
}


Q_DECLARE_METATYPE(bs::error::ErrorCode)
Q_DECLARE_METATYPE(bs::signer::RequestId)

void QtHCT::onError(bs::signer::RequestId reqId, const std::string& errMsg)
{
   QMetaObject::invokeMethod(this, [this, reqId, errMsg] {
      emit Error(reqId, errMsg);
   });
}

void QtHCT::txSigned(bs::signer::RequestId reqId, const BinaryData& signedTX
   , bs::error::ErrorCode errCode, const std::string& errMsg)
{
   QMetaObject::invokeMethod(this, [this, reqId, signedTX, errCode, errMsg] {
      emit TXSigned(reqId, signedTX, errCode, errMsg);
   });
}

void QtHCT::walletInfo(bs::signer::RequestId reqId
   , const Blocksettle::Communication::headless::GetHDWalletInfoResponse& wi)
{
   QMetaObject::invokeMethod(this, [this, reqId, wi] {
      emit QWalletInfo(reqId, bs::hd::WalletInfo(wi));
   });
}

void QtHCT::autoSignStateChanged(bs::error::ErrorCode errCode, const std::string& walletId)
{
   QMetaObject::invokeMethod(this, [this, errCode, walletId] {
      emit AutoSignStateChanged(errCode, walletId);
   });
}

void QtHCT::authLeafAdded(const std::string& walletId)
{
   QMetaObject::invokeMethod(this, [this, walletId] {
      emit AuthLeafAdded(walletId);
   });
}
