/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BlockchainAdapter.h"
#include <spdlog/spdlog.h>
#include "ArmoryErrors.h"
#include "ArmoryObject.h"
#include "BitcoinFeeCache.h"
#include "StringUtils.h"
#include "Wallets/SyncPlainWallet.h"

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;

static const auto kReconnectInterval = std::chrono::seconds{ 30 };
static const auto kBroadcastTimeout = std::chrono::seconds{ 30 };


BinaryData TxWithHeight::serialize() const
{
   BinaryData result;
   result.append(WRITE_UINT32_LE(txHeight_));
   result.append(Tx::serialize());
   return result;
}

void TxWithHeight::deserialize(const BinaryData &data)
{
   txHeight_ = READ_UINT32_LE(data.getSliceRef(0, sizeof(uint32_t)));
   Tx::unserialize(data.getSliceRef(sizeof(uint32_t), data.getSize() - sizeof(uint32_t)));
}


BlockchainAdapter::BlockchainAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &user
   , const std::shared_ptr<ArmoryConnection> &armory) //pre-inited armory connection [optional]
   : logger_(logger), user_(user), armoryPtr_(armory)
   , stopped_(std::make_shared<std::atomic_bool>(false))
{}

BlockchainAdapter::~BlockchainAdapter()
{
   *stopped_ = true;
   cleanup();
}

bool BlockchainAdapter::process(const bs::message::Envelope &env)
{
   if (env.receiver->value() == user_->value()) {
      ArmoryMessage msg;
      if (!msg.ParseFromString(env.message)) {
         logger_->error("[{}] failed to parse own request #{}", __func__, env.id());
         return true;
      }
      switch (msg.data_case()) {
      case ArmoryMessage::kReconnect:
         armoryPtr_.reset();
         start();
         break;
      case ArmoryMessage::kSettingsResponse:
         return processSettings(msg.settings_response());
      case ArmoryMessage::kKeyCompared:
         if (connKeyProm_) {
            connKeyProm_->set_value(msg.key_compared());
         }
         break;
      case ArmoryMessage::kRegisterWallet:
         return processRegisterWallet(env, msg.register_wallet());
      case ArmoryMessage::kUnregisterWallets:
         return processUnregisterWallets(env, msg.unregister_wallets());
      case ArmoryMessage::kTxPush:
         return processPushTxRequest(env, msg.tx_push());
      case ArmoryMessage::kTxPushTimeout:
         onBroadcastTimeout(msg.tx_push_timeout());
         break;
      case ArmoryMessage::kSetUnconfTarget:
         return processUnconfTarget(env, msg.set_unconf_target());
      case ArmoryMessage::kAddrTxCountRequest:
         return processGetTxCount(env, msg.addr_tx_count_request());
      case ArmoryMessage::kWalletBalanceRequest:
         return processBalance(env, msg.wallet_balance_request());
      case ArmoryMessage::kGetTxsByHash:
         return processGetTXsByHash(env, msg.get_txs_by_hash());
      case ArmoryMessage::kGetLedgerEntries:
         return processLedgerEntries(env, msg.get_ledger_entries());
      case ArmoryMessage::kLedgerUnsubscribe:
         return processLedgerUnsubscribe(env, msg.ledger_unsubscribe());
      case ArmoryMessage::kGetAddressHistory:
         return processAddressHist(env, msg.get_address_history());
      case ArmoryMessage::kFeeLevelsRequest:
         return processFeeLevels(env, msg.fee_levels_request());
      case ArmoryMessage::kGetSpendableUtxos:
         return processGetUTXOs(env, msg.get_spendable_utxos());
      case ArmoryMessage::kGetZcUtxos:
         return processGetUTXOs(env, msg.get_zc_utxos(), true);
      case ArmoryMessage::kGetRbfUtxos:
         return processGetUTXOs(env, msg.get_rbf_utxos(), false, true);
      case ArmoryMessage::kGetUtxosForAddr:
         return processUTXOsForAddr(env, msg.get_utxos_for_addr());
      case ArmoryMessage::kGetOutPoints:
         return processGetOutpoints(env, msg.get_out_points());
      case ArmoryMessage::kGetSpentness:
         return processSpentnessRequest(env, msg.get_spentness());
      case ArmoryMessage::kGetOutputsForOps:
         return processGetOutputsForOPs(env, msg.get_outputs_for_ops());
      case ArmoryMessage::kSubscribeTxForAddress:
         return processSubscribeAddressTX(env, msg.subscribe_tx_for_address());
      default:
         logger_->warn("[{}] unknown message to blockchain #{}: {}", __func__
            , env.id(), msg.data_case());
         break;
      }
   }
   return true;
}

void BlockchainAdapter::setQueue(const std::shared_ptr<bs::message::QueueInterface> &queue)
{
   Adapter::setQueue(queue);
   start();
}

void BlockchainAdapter::start()
{
   if (armoryPtr_) {
      sendLoadingBC();
      init(armoryPtr_.get());
      feeEstimationsCache_ = std::make_shared<BitcoinFeeCache>(logger_, armoryPtr_);
      onStateChanged(armoryPtr_->state());
   }
   else {
      ArmoryMessage msg;
      msg.mutable_settings_request();  // broadcast - ask for someone to provide settings
      pushBroadcast(user_, msg.SerializeAsString(), true);
   }
}

void BlockchainAdapter::sendReady()
{
   ArmoryMessage msg;
   msg.mutable_ready();
   pushBroadcast(user_, msg.SerializeAsString(), true);
}

void BlockchainAdapter::sendLoadingBC()
{
   ArmoryMessage msg;
   msg.mutable_loading();
   pushBroadcast(user_, msg.SerializeAsString());
}

void BlockchainAdapter::sendState(ArmoryState st)
{
   ArmoryMessage msg;
   auto msgState = msg.mutable_state_changed();
   msgState->set_state(static_cast<int32_t>(st));
   msgState->set_top_block(armory_->topBlock());
   pushBroadcast(user_, msg.SerializeAsString(), true);
}

bool BlockchainAdapter::processSettings(const ArmoryMessage_Settings &settings)
{
   Settings curSet{ settings.host(), settings.port(), settings.bip15x_key() };
   if (armoryPtr_) {
      if (curSet == currentSettings_) {
         logger_->warn("[BlockchainAdapter::processSettings] got the same settings"
            " and connection exists - aborting reconnect");
         return true;
      }
      currentSettings_ = curSet;
   }
   else {
      currentSettings_ = curSet;
   }
   if (settings.cache_file_name().empty()) {
      armoryPtr_ = std::make_shared<ArmoryConnection>(logger_);
      init(armoryPtr_.get());

      BinaryData serverBIP15xKey;
      if (!curSet.key.empty()) {
         try {
            serverBIP15xKey = READHEX(curSet.key);
         } catch (const std::exception &e) {
            logger_->error("[BlockchainAdapter::processSettings] invalid armory key detected: {}: {}"
               , settings.bip15x_key(), e.what());
            return true;
         }
      }
      armoryPtr_->setupConnection(static_cast<NetworkType>(settings.network_type())
         , settings.host(), settings.port(), settings.data_dir(), true
         , [serverBIP15xKey](const BinaryData&, const std::string&) { return true; });
   }
   else {
      const auto &armory = std::make_shared<ArmoryObject>(logger_
         , settings.cache_file_name(), false);
      init(armory.get());

      ArmorySettings armorySettings;
      armorySettings.socketType = static_cast<SocketType>(settings.socket_type());
      armorySettings.netType = static_cast<NetworkType>(settings.network_type());
      armorySettings.armoryDBIp = QString::fromStdString(curSet.host);
      armorySettings.armoryDBPort = std::stoi(curSet.port);
      armorySettings.armoryDBKey = QString::fromStdString(curSet.key);
      armorySettings.runLocally = settings.run_locally();
      armorySettings.dataDir = QString::fromStdString(settings.data_dir());
      armorySettings.armoryExecutablePath = QString::fromStdString(settings.executable_path());
      armorySettings.bitcoinBlocksDir = QString::fromStdString(settings.bitcoin_dir());
      armorySettings.dbDir = QString::fromStdString(settings.db_dir());

      connKeyProm_ = std::make_shared<std::promise<bool>>();
      armory->setupConnection(armorySettings, [this, armory]
         (const BinaryData& srvPubKey, const std::string& srvIPPort) -> bool {
         ArmoryMessage msg;
         auto msgReq = msg.mutable_compare_key();
         msgReq->set_new_key(srvPubKey.toBinStr());
         msgReq->set_server_id(srvIPPort);
         pushBroadcast(user_, msg.SerializeAsString(), true);

/*         auto futureObj = connKeyProm_->get_future();
         const bool result = futureObj.get();
         if (!result) { // stop armory connection loop if server key was rejected
            armory->needsBreakConnectionLoop_ = true;
            armory->setState(ArmoryState::Cancelled);
         }
         connKeyProm_.reset();
         return result;*/
         return true;
      });
      armoryPtr_ = armory;
   }
   sendLoadingBC();
   return true;
}

void BlockchainAdapter::reconnect()
{
   logger_->debug("[BlockchainAdapter::reconnect]");
   ArmoryMessage msg;
   msg.mutable_reconnect();
   pushRequest(user_, user_, msg.SerializeAsString()
      , bs::message::bus_clock::now() + kReconnectInterval);
}

void BlockchainAdapter::resumeRegistrations()
{
   for (const auto &wallet : wallets_) {
      registerWallet(wallet.first, wallet.second);
   }
}

void BlockchainAdapter::onStateChanged(ArmoryState st)
{
   switch (st) {
   case ArmoryState::Ready:
      suspended_ = false;
      resumeRegistrations();
      sendReady();
      break;

   case ArmoryState::Connected:
      logger_->debug("[BlockchainAdapter::onStateChanged] Armory connected - going online");
      armoryPtr_->goOnline();
      break;

   case ArmoryState::Error:
      logger_->error("[BlockchainAdapter::onStateChanged] armory connection encountered some errors");
      [[clang::fallthrough]];
   case ArmoryState::Offline:
      logger_->info("[BlockchainAdapter::onStateChanged] Armory is offline - "
         "suspended and reconnecting");
      suspend();
      reconnect();
      [[clang::fallthrough]];
   default:    break;
   }
   sendState(st);
}

void BlockchainAdapter::onRefresh(const std::vector<BinaryData> &ids, bool online)
{
   ArmoryMessage msg;
   auto msgRefresh = msg.mutable_refresh();
   for (const auto &id : ids) {
      const auto &idStr = id.toBinStr();
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      const auto &itReg = regMap_.find(idStr);
      if (itReg != regMap_.end()) {
         wallets_[itReg->second].registered = true;
         ArmoryMessage msgReg;
         auto msgWalletRegged = msgReg.mutable_wallet_registered();
         msgWalletRegged->set_wallet_id(itReg->second);
         msgWalletRegged->set_success(true);
         const auto &itRegReq = reqByRegId_.find(idStr);
         Envelope env;
         if (itRegReq == reqByRegId_.end()) {
            env = Envelope::makeBroadcast(user_, msgReg.SerializeAsString());
         }
         else {
            env = Envelope::makeResponse(user_, itRegReq->second.sender
               , msgReg.SerializeAsString(), itRegReq->second.foreignId());
            reqByRegId_.erase(itRegReq);
         }
         pushFill(env);
         regMap_.erase(itReg);
         logger_->debug("[{}] found reg request for {}", __func__, idStr);
         continue;
      }

      const auto &itUnconfTgt = unconfTgtMap_.find(idStr);
      if (itUnconfTgt != unconfTgtMap_.end()) {
         ArmoryMessage msgUnconfTgt;
         msgUnconfTgt.set_unconf_target_set(itUnconfTgt->second.first);
         pushResponse(user_, itUnconfTgt->second.second
            , msgUnconfTgt.SerializeAsString());
         unconfTgtMap_.erase(itUnconfTgt);
         logger_->debug("[{}] unconf tgt reg {}", __func__, idStr);
         continue;
      }

      const auto& itAddrHist = addressSubscriptions_.find(idStr);
      if (itAddrHist != addressSubscriptions_.end()) {
         singleAddrWalletRegistered(itAddrHist->second);
         addressSubscriptions_.erase(itAddrHist);
         logger_->debug("[{}] addressSubscription {}", __func__, idStr);
         continue;
      }
      msgRefresh->add_ids(id.toBinStr());
   }
   if (registrationComplete_ && !walletsReady_ && regMap_.empty()) {
      bool allWalletsRegged = true;
      for (const auto &wallet : wallets_) {
         if (!wallet.second.registered) {
            allWalletsRegged = false;
            break;
         }
      }
      if (allWalletsRegged) {
         logger_->debug("[{}] all wallets regged", __func__);
         walletsReady_ = true;
         //TODO: resume all pending requests if all wallets registered
         ArmoryMessage msgReg;
         auto msgWalletRegged = msgReg.mutable_wallet_registered();
         msgWalletRegged->set_wallet_id("");
         msgWalletRegged->set_success(true);
         pushBroadcast(user_, msgReg.SerializeAsString());
      }
   }
   if (msgRefresh->ids_size() > 0) {
      msgRefresh->set_online(online);
      pushBroadcast(user_, msg.SerializeAsString(), true);
   }
}

void BlockchainAdapter::onNewBlock(unsigned int height, unsigned int branchHeight)
{
   ArmoryMessage msg;
   auto msgBlock = msg.mutable_new_block();
   msgBlock->set_top_block(height);
   msgBlock->set_branch_height(branchHeight);
   pushBroadcast(user_, msg.SerializeAsString(), true);
}

void BlockchainAdapter::onZCInvalidated(const std::set<BinaryData> &ids)
{
   ArmoryMessage msg;
   auto zcInv = msg.mutable_zc_invalidated();
   for (const auto& id : ids) {
      zcInv->add_tx_hashes(id.toBinStr());
      const auto &itPending = pushedZCs_.find(id);
      if (itPending != pushedZCs_.end()) {
         pushedZCs_.erase(itPending);
      }  // If the TX was invalidated without being received in mempool, this could
   }     // be a sign of some rare and severe issue. Otherwise it will be removed

   pushBroadcast(user_, msg.SerializeAsString(), true);
}

static std::vector<bs::TXEntry> mergeTXEntries(const std::vector<bs::TXEntry>& entries)
{
   const auto& cbFindDupEntry = [](const std::vector<bs::TXEntry>& entries
      , const std::vector<bs::TXEntry>::iterator& itEntry)
   {
      for (auto it = entries.begin(); it != entries.end(); ++it) {
         if (it == itEntry) {
            continue;
         }
      }
      return entries.end();
   };
   auto result = entries;
   auto itEntry = result.begin();
   while (itEntry != result.end()) {
      std::vector<std::vector<bs::TXEntry>::const_iterator> dupIts;
      for (auto it = itEntry + 1; it != result.end(); ++it) {
         if (it->txHash == itEntry->txHash) {
            dupIts.push_back(it);
         }
      }
      if (!dupIts.empty()) {
         std::reverse(dupIts.begin(), dupIts.end());
         for (const auto& itDup : dupIts) {
            itEntry->merge(*itDup);
            result.erase(itDup);
         }
      }
      itEntry++;
   }
   return result;
}

void BlockchainAdapter::onZCReceived(const std::string &requestId, const std::vector<bs::TXEntry> &entries)
{
   for (const auto& entry : entries) {
      processZcForAddrSubscriptions(entry);
   }
   receivedZCs_.insert(requestId);
   const auto& mergedEntries = mergeTXEntries(entries);
   ArmoryMessage msg;
   auto msgZC = msg.mutable_zc_received();
   msgZC->set_request_id(requestId);

   ArmoryMessage msgPushTxResult;
   auto msgResult = msgPushTxResult.mutable_tx_push_result();
   msgResult->set_result(ArmoryMessage_PushTxResult_PushTxSuccess);

   const auto& sendNotOurResult = [this, &msgPushTxResult, msgResult, mergedEntries]
   {
      msgResult->set_pushed_by_us(false);
      msgResult->set_result(ArmoryMessage_PushTxResult_PushTxSuccess);
      for (const auto& entry : mergedEntries) {
         msgResult->add_tx_hashes(entry.txHash.toBinStr());
         if (msgResult->push_id().empty() && !entry.walletIds.empty()) {
            msgResult->set_push_id(*entry.walletIds.cbegin());
         }
      }
      pushBroadcast(user_, msgPushTxResult.SerializeAsString(), true);
   };
   if (requestId.empty()) {
      sendNotOurResult();
   }
   else {
      const auto& itPending = pendingTxMap_.find(requestId);
      if (itPending == pendingTxMap_.end()) {
         sendNotOurResult();
      }
      else {
         if (itPending->second.resultReported) {
            logger_->debug("[BlockchainAdapter::onZCReceived] TX push result already reported on {}"
               , requestId);
            return;
         }
         itPending->second.resultReported = true;

         msgResult->set_push_id(itPending->second.pushId);
         msgResult->set_request_id(requestId);
         msgResult->set_pushed_by_us(true);
         for (const auto& entry : mergedEntries) {
            msgResult->add_tx_hashes(entry.txHash.toBinStr());
         }
         pushResponse(user_, itPending->second.env
            , msgPushTxResult.SerializeAsString());

         if (!itPending->second.monitored) {
            pendingTxMap_.erase(itPending);
         }
      }
   }

   for (const auto &entry : mergedEntries) {
      pushedZCs_.erase(entry.txHash);

      auto msgTX = msgZC->add_tx_entries();
      msgTX->set_tx_hash(entry.txHash.toBinStr());
      for (const auto& walletId : entry.walletIds) {
         msgTX->add_wallet_ids(walletId);
      }
      for (const auto& addr : entry.addresses) {
         msgTX->add_addresses(addr.display());
      }
      msgTX->set_value(entry.value);
      msgTX->set_block_num(entry.blockNum);
      msgTX->set_chained_zc(entry.isChainedZC);
      msgTX->set_rbf(entry.isRBF);
      msgTX->set_recv_time(entry.recvTime.time_since_epoch().count());
      msgTX->set_nb_conf(entry.nbConf);
   }
   pushBroadcast(user_, msg.SerializeAsString(), true);

   const auto &itLedgerSub = ledgerSubscriptions_.find({});
   if (itLedgerSub != ledgerSubscriptions_.end()) {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_ledger_entries();
      msgResp->set_filter("");
      msgResp->set_total_pages(0);
      msgResp->set_cur_block(armory_->topBlock());
      for (const auto &entry : mergedEntries) {
         auto msgEntry = msgResp->add_entries();
         msgEntry->set_tx_hash(entry.txHash.toBinStr());
         msgEntry->set_value(entry.value);
         msgEntry->set_block_num(entry.blockNum);
         msgEntry->set_tx_time(entry.txTime);
         msgEntry->set_rbf(entry.isRBF);
         msgEntry->set_chained_zc(entry.isChainedZC);
         msgEntry->set_recv_time(entry.recvTime.time_since_epoch().count());
         msgEntry->set_nb_conf(entry.nbConf);
         for (const auto &walletId : entry.walletIds) {
            msgEntry->add_wallet_ids(walletId);
         }
         for (const auto &addr : entry.addresses) {
            msgEntry->add_addresses(addr.display());
         }
      }
      for (const auto &recv : itLedgerSub->second) {
         pushResponse(user_, recv, msg.SerializeAsString(), (SeqId)EnvelopeType::Publish);
      }
   }
}

// Unused code to save from PB's ArmoryWalletAdapter
/*constexpr auto kTxRebroadcastInterval = std::chrono::seconds(30);
namespace KnownBitcoinCoreErrors
{
   //                  Successfully broadcasted error codes
   // AlreadyInMemPool - means that TX is already in mempool
   // treat as broadcast was successfull
   const std::string TxAlreadyInMemPool = "txn-already-in-mempool";

   //                   TX conflicts error codes
   // TxAlreadyKnown - looks like there is already TX for those inputs
   // treat as broadcast was successfull. If it is a double spend - will be detected
   // by settlement monitor.
   const std::string TxAlreadyKnown = "txn-already-known";

   // TxMempoolConflict - there is TX in a mmepool that try to spent payin inputs
   // should be monitored before release. In case if this is pay-out - it
   // might be a revoke TX. should be monitored as well
   const std::string TxMempoolConflict = "txn-mempool-conflict";

   //                rebroadcast error codes
   // MempoolFool - TX was not accepted, but it might be accepted in a future
   // reason - there is just not enough space in a mempool right now
   const std::string MempoolFull = "mempool full";

   // no need to add this errors. Any other should be a good enough reason to cancel reservation
   //A transaction that spends outputs that would be replaced by it is invalid
   //TxValidationResult::TX_CONSENSUS, "bad-txns-spends-conflicting-tx"
}*/

void BlockchainAdapter::onTxBroadcastError(const std::string& requestId
   , const BinaryData &txHash, int errCode, const std::string &errMsg)
{
   pushedZCs_.erase(txHash);
   PushTxData pushData;
   const auto itPending = pendingTxMap_.find(requestId);
   if (itPending == pendingTxMap_.end()) {
      logger_->warn("[BlockchainAdapter::onTxBroadcastError] get unexpected TX error {} : {}. {} : {}"
         , errCode, errMsg
         , requestId, txHash.toHexStr(true));
   }
   else {
      pushData = itPending->second; 

      if (pushData.resultReported) {
         logger_->error("[BlockchainAdapter::onTxBroadcastError] result already reported on {} : {}"
            , requestId, txHash.toHexStr(true));
         return;
      }

      if (!itPending->second.monitored) {
         pendingTxMap_.erase(itPending);
      } else {
         itPending->second.resultReported = true;
      }
   }

   receivedZCs_.insert(requestId);
   ArmoryMessage msg;
   auto msgResp = msg.mutable_tx_push_result();
   msgResp->set_request_id(requestId);
   msgResp->add_tx_hashes(txHash.toBinStr());
   msgResp->set_error_message(errMsg);

   const auto &txHashString = txHash.toHexStr(true);
   auto broadcastErrCode = (ArmoryErrorCodes)errCode;

   switch (broadcastErrCode)
   {
   case ArmoryErrorCodes::ZcBroadcast_AlreadyInChain:
      //tx is already mined
      logger_->debug("[BlockchainAdapter::onTxBroadcastError] {} {} already in chain."
         , requestId, txHashString);
      msgResp->set_result(ArmoryMessage::PushTxAlreadyInChain);
      break;

   case ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool:
      //tx was broadcasted succesfully by another party
      logger_->debug("[BlockchainAdapter::onTxBroadcastError] {} {} already in "
         "mempool - processing as broadcasted", requestId, txHashString);
      msgResp->set_result(ArmoryMessage::PushTxAlreadyInMempool);
      break;

   case ArmoryErrorCodes::P2PReject_Duplicate:
      //mempool double spend
      logger_->error("[BlockchainAdapter::onTxBroadcastError] {} {} - {}. "
         "Double spend", requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxMempoolConflict);
      break;

   case ArmoryErrorCodes::ZcBatch_Timeout:
      onBroadcastTimeout(txHash.toBinStr());
      return;

   case ArmoryErrorCodes::ZcBroadcast_Error:
      //failed consensus rules, this tx cannot be mined
      logger_->error("[BlockchainAdapter::onTxBroadcastError] {} {} - {}. "
         "Breaks consensus rules", requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
      break;

   case ArmoryErrorCodes::ZcBroadcast_VerifyRejected:
      /*
      Failed verification: bad sig / malformed tx / utxo spent by other zc
      Since we check for signature and tx structure validity, this error is always treated as
      a mempool conflict
      */
      logger_->error("[ArmoryWalletAdapter::onTxBroadcastError] {} {} - {}. Possible"
         " double spend", requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxMempoolConflict);
      break;

   case ArmoryErrorCodes::P2PReject_InsufficientFee:
      //breaks propagation rules (typically RBF fee failures)
      logger_->error("[ArmoryWalletAdapter::onTxBroadcastError] {} {} - {}"
         , requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
      break;

   default:
      //report and fail on errors that aren't specifically handled
      logger_->error("[ArmoryWalletAdapter::onTxBroadcastError] {} {} - {} - "
         "errCode: {}. Unhandled error", requestId, txHashString, errMsg, errCode);
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
   }
   pushResponse(user_, pushData.env, msg.SerializeAsString());
}

void BlockchainAdapter::suspend()
{
   suspended_ = true;
   walletsReady_ = false;

   for (auto &wallet : wallets_) {
      wallet.second.registered = false;
      wallet.second.wallet.reset();
   }
}

void BlockchainAdapter::onBroadcastTimeout(const std::string &timeoutId)
{
   const auto& itReceived = receivedZCs_.find(timeoutId);
   if (itReceived != receivedZCs_.end()) {
      receivedZCs_.erase(itReceived);
      return;
   }

   {
      std::unique_lock<std::mutex> lock(mtxReqPool_);
      for (const auto& txSingle : pendingTxMap_) {
         if (!txSingle.second.resultReported) {
            const auto& env = txSingle.second.env;
            requestsPool_.emplace(env.foreignId(), env);
         }
      }
   }

   logger_->info("[BlockchainAdapter::onBroadcastTimeout] {}",timeoutId);
   ArmoryMessage msg;
   msg.set_tx_push_timeout(timeoutId);
   pushBroadcast(user_, msg.SerializeAsString(), true);

   suspend();
   reconnect();
}

bool BlockchainAdapter::processRegisterWallet(const bs::message::Envelope &env
   , const ArmoryMessage_RegisterWallet &request)
{
   if (suspended_) {
      return false;  // postpone until Armory will become ready
   }
   if (request.wallet_id().empty()) {
      registrationComplete_ = true;
      return true;
   }
   const auto &sendError = [this, env, request]
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_wallet_registered();
      msgResp->set_wallet_id(request.wallet_id());
      msgResp->set_success(false);
      pushResponse(user_, env, msg.SerializeAsString());
   };
   Wallet wallet;
   wallet.asNew = request.as_new();
   for (const auto &addrStr : request.addresses()) {
      wallet.addresses.push_back(BinaryData::fromString(addrStr));
   }
   const auto &regId = registerWallet(request.wallet_id(), wallet);
   if (regId.empty()) {
      sendError();
   }
   else {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      reqByRegId_[regId] = env;
   }
   return true;
}

bool BlockchainAdapter::processUnregisterWallets(const bs::message::Envelope& env
   , const ArmoryMessage_WalletIDs& request)
{
   ArmoryMessage msg;
   auto msgResp = msg.mutable_unregister_wallets();
   for (const auto& walletId : request.wallet_ids()) {
      if (unregisterWallet(walletId)) {
         msgResp->add_wallet_ids(walletId);
      }
   }
   return (pushResponse(user_, env, msg.SerializeAsString()) != 0);
}

bool BlockchainAdapter::unregisterWallet(const std::string& walletId)
{
   bool result = false;
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   const auto& itWallet = wallets_.find(walletId);
   if (itWallet == wallets_.end()) {
      logger_->warn("[{}] unknown wallet {}", __func__, walletId);
      return false;
   }
   if (itWallet->second.wallet) {
      itWallet->second.wallet->unregister();
      result = true;
   }
   else {
      logger_->warn("[{}] wallet for {} not set", __func__, walletId);
   }
   wallets_.erase(itWallet);
   regMap_.erase(walletId);
   return result;
}

std::string BlockchainAdapter::registerWallet(const std::string &walletId
   , const Wallet &wallet)
{
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   if (regMap_.empty()) {
      registrationComplete_ = false;
   }
   auto &newWallet = wallets_[walletId];
   if (!newWallet.wallet) {
      newWallet.wallet = armoryPtr_->instantiateWallet(walletId);
   }
   newWallet.asNew = wallet.asNew;
   newWallet.addresses = wallet.addresses;

   const auto &regId = newWallet.wallet->registerAddresses(newWallet.addresses
      , wallet.asNew);
   regMap_[regId] = walletId;
   return regId;
}

bool BlockchainAdapter::processUnconfTarget(const bs::message::Envelope &env
   , const ArmoryMessage_WalletUnconfirmedTarget &request)
{
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   const auto &itWallet = wallets_.find(request.wallet_id());
   if (itWallet == wallets_.end()) {
      logger_->error("[{}] unknown wallet {}", __func__, request.wallet_id());
      return true;
   }
   if (!itWallet->second.registered) {
      logger_->warn("[{}] wallet {} is not registered, yet", __func__
         , request.wallet_id());
      return false;
   }
   std::string regId;
   if (itWallet->second.wallet) {
      regId = itWallet->second.wallet->setUnconfirmedTarget(request.conf_count());
   }
   if (regId.empty()) {
      logger_->error("[{}] invalid wallet {}", __func__, request.wallet_id());
      return true;
   }
   unconfTgtMap_[regId] = { request.wallet_id(), env };
   return true;
}

bool BlockchainAdapter::processGetTxCount(const bs::message::Envelope &env
   , const ArmoryMessage_WalletIDs &request)
{
   const auto &cbTxNs = [this, env, stopped = stopped_]
      (const std::map<std::string, CombinedCounts> &txns)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_addr_tx_count_response();
      for (const auto &txn : txns) {
         auto msgByWallet = msgResp->add_wallet_tx_counts();
         msgByWallet->set_wallet_id(txn.first);
         for (const auto &byAddr : txn.second.addressTxnCounts_) {
            auto msgByAddr = msgByWallet->add_txns();
            msgByAddr->set_address(byAddr.first.toBinStr());
            msgByAddr->set_tx_count(byAddr.second);
         }
      }
      if (*stopped) {
         return;
      }
      pushResponse(user_, env, msg.SerializeAsString());
   };
   std::vector<std::string> walletIDs;
   walletIDs.reserve(request.wallet_ids_size());
   for (const auto &walletId : request.wallet_ids()) {
      walletIDs.push_back(walletId);
   }
   return armoryPtr_->getCombinedTxNs(walletIDs, cbTxNs);
}

bool BlockchainAdapter::processBalance(const bs::message::Envelope &env
   , const ArmoryMessage_WalletIDs &request)
{
   const auto &cbBal = [this, env, stopped = stopped_]
      (const std::map<std::string, CombinedBalances> &bals)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_wallet_balance_response();
      for (const auto &bal : bals) {
         if (*stopped) {
            return;
         }
         auto msgByWallet = msgResp->add_balances();
         msgByWallet->set_wallet_id(bal.first);
         if (bal.second.walletBalanceAndCount_.size() == 4) {
            msgByWallet->set_full_balance(bal.second.walletBalanceAndCount_[0]);
            msgByWallet->set_spendable_balance(bal.second.walletBalanceAndCount_[1]);
            msgByWallet->set_unconfirmed_balance(bal.second.walletBalanceAndCount_[2]);
            msgByWallet->set_address_count(bal.second.walletBalanceAndCount_[3]);
         }
         else {
            logger_->warn("[BlockchainAdapter::processBalance] empty wallet "
               "balance received for {}", bal.first);
         }
         for (const auto &byAddr : bal.second.addressBalances_) {
            if (*stopped_) {
               return;
            }
            auto msgByAddr = msgByWallet->add_addr_balances();
            msgByAddr->set_address(byAddr.first.toBinStr());
            if (byAddr.second.size() == 3) {
               msgByAddr->set_full_balance(byAddr.second[0]);
               msgByAddr->set_spendable_balance(byAddr.second[1]);
               msgByAddr->set_unconfirmed_balance(byAddr.second[2]);
            }
            else {
               logger_->warn("[BlockchainAdapter::processBalance] empty address"
                  " balance received for {}/{}", bal.first, byAddr.first.toHexStr());
            }
         }
      }
      pushResponse(user_, env, msg.SerializeAsString());
   };
   std::vector<std::string> walletIDs;
   walletIDs.reserve(request.wallet_ids_size());
   for (const auto &walletId : request.wallet_ids()) {
      walletIDs.push_back(walletId);
   }
   return armoryPtr_->getCombinedBalances(walletIDs, cbBal);
}

bool BlockchainAdapter::processPushTxRequest(const bs::message::Envelope &env
   , const ArmoryMessage_TXPushRequest &request)
{
   if (suspended_) {
      logger_->debug("[BlockchainAdapter::processPushTxRequest] suspended");
      std::unique_lock<std::mutex> lock(mtxReqPool_);
      requestsPool_[env.foreignId()] = env;
      return true;
   }

   const auto &sendError = [this, request, env](const std::string &errMsg) -> bool
   {
      logger_->error("[BlockchainAdapter::processPushTxRequest] push TX error: {}", errMsg);
      ArmoryMessage msg;
      auto msgResp = msg.mutable_tx_push_result();
      msgResp->set_push_id(request.push_id());
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
      msgResp->set_error_message(errMsg);
      return (pushResponse(user_, env, msg.SerializeAsString()) != 0);
   };

   const bool monitored = !request.push_id().empty();
   PushTxData pushTxData{ env, request.push_id(), monitored };
   std::vector<BinaryData> txToPush;
   txToPush.reserve(request.txs_to_push_size());
   for (const auto &tx : request.txs_to_push()) {
      const auto &binTX = BinaryData::fromString(tx.tx());
      const Tx txObj(binTX);
      if (binTX.empty() || !txObj.isInitialized()) {
         logger_->error("[BlockchainAdapter::processPushTxRequest] invalid TX "
            "data to push");
         return sendError("invalid TX data");
      }

      auto txHash = BinaryData::fromString(tx.expected_tx_hash());
      if (txHash.empty()) {
         txHash = txObj.getThisHash();
      }
      else {
         if (txHash != txObj.getThisHash()) {
            return sendError("TX hash mismatch");
         }
      }
      if (pushedZCs_.find(txHash) != pushedZCs_.end()) {
         logger_->error("[BlockchainAdapter::processPushTxRequest] TX already"
            " pushed - {} ignored", txHash.toHexStr(true));
         continue;
      }

      txToPush.push_back(binTX);
      pushTxData.txs.push_back(binTX);
      pushedZCs_.insert(txHash);

/*      if (monitored) {
         const auto &itWallet = wallets_.find(request.push_id());

         if (itWallet != wallets_.end()) {
            logger_->error("[BlockchainAdapter::processPushTxRequest] TX already"
               " pushed with id {} - {} ignored", request.push_id()
               , txHash.toHexStr(true));
            continue;
         }
      }*/
   }

   if (txToPush.empty()) {
      logger_->error("[BlockchainAdapter::processPushTxRequest] nothing to push");
      return sendError("nothing to push");
   }
   const auto &pushRequestId = (txToPush.size() == 1) ?
      armory_->pushZC(txToPush.at(0)) : armory_->pushZCs(txToPush);
   if (pushRequestId.empty()) {
      logger_->error("[BlockchainAdapter::processPushTxRequest] failed to push TX");
      return sendError("failed to push");
   }
   pendingTxMap_[pushRequestId] = pushTxData;
   sendBroadcastTimeout(pushRequestId);

   logger_->debug("[BlockchainAdapter::processPushTxRequest] pushed id {} for"
      " request {} ({} TX[s])", pushRequestId, request.push_id(), txToPush.size());
   return true;
}

void BlockchainAdapter::sendBroadcastTimeout(const std::string &timeoutId)
{
   ArmoryMessage msg;
   msg.set_tx_push_timeout(timeoutId);
   pushRequest(user_, user_, msg.SerializeAsString()
      , bs::message::bus_clock::now() + kBroadcastTimeout);
}

bool BlockchainAdapter::processGetTXsByHash(const bs::message::Envelope &env
   , const ArmoryMessage_TXHashes &request)
{
   const auto &cb = [this, env, stopped = stopped_]
      (const AsyncClient::TxBatchResult &txBatch, std::exception_ptr)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_transactions();
      for (const auto &tx : txBatch) {
         if (!tx.second) {
            continue;
         }
         auto msgTX = msgResp->add_transactions();
         msgTX->set_tx(tx.second->serialize().toBinStr());
         msgTX->set_height(tx.second->getTxHeight());
      }
      if (*stopped) {
         return;
      }
      const auto& message = msg.SerializeAsString();
      pushResponse(user_, env, message);
      pushBroadcast(user_, message);
   };

   std::set<BinaryData> hashes;
   for (const auto &hash : request.tx_hashes()) {
      hashes.insert(BinaryData::fromString(hash));
   }
   return armoryPtr_->getTXsByHash(hashes, cb, !request.disable_cache());
}

bool BlockchainAdapter::processLedgerEntries(const bs::message::Envelope &env
   , const std::string &filter)
{
   ledgerSubscriptions_[filter].push_back(env.sender);
   std::string walletId, addrStr;
   const auto posDot = filter.find('.');
   if (posDot != std::string::npos) {
      walletId = filter.substr(0, posDot);
      addrStr = filter.substr(posDot + 1);
   }
   const auto &cbLedger = [this, env, filter, walletId, stopped = stopped_]
      (const std::shared_ptr<AsyncClient::LedgerDelegate> &delegate)
   {
      const auto &cbPage = [this, delegate, env, filter, walletId, stopped]
         (ReturnMessage<uint64_t> pageCntReturn)
      {
         uint32_t pageCnt = 0;
         try {
            pageCnt = (uint32_t)pageCntReturn.get();
         }
         catch (const std::exception &) {
            return;
         }
         for (uint32_t page = 0; page < pageCnt; ++page) {
            if (*stopped || suspended_) {
               return;
            }
            const auto &cbEntries = [this, env, filter, page, pageCnt, walletId, stopped]
               (ReturnMessage<std::vector<ClientClasses::LedgerEntry>> entriesRet)
            {
               try {
                  auto le = entriesRet.get();
                  auto entries = mergeTXEntries(bs::TXEntry::fromLedgerEntries(le));
                  for (auto &entry : entries) {
                     entry.nbConf = armory_->getConfirmationsNumber(entry.blockNum);
                  }
                  ArmoryMessage msg;
                  auto msgResp = msg.mutable_ledger_entries();
                  msgResp->set_filter(filter);
                  msgResp->set_total_pages(pageCnt);
                  msgResp->set_cur_page(page);
                  msgResp->set_cur_block(armory_->topBlock());
                  for (const auto &entry : entries) {
                     auto msgEntry = msgResp->add_entries();
                     msgEntry->set_tx_hash(entry.txHash.toBinStr());
                     msgEntry->set_value(entry.value);
                     msgEntry->set_block_num(entry.blockNum);
                     msgEntry->set_tx_time(entry.txTime);
                     msgEntry->set_rbf(entry.isRBF);
                     msgEntry->set_chained_zc(entry.isChainedZC);
                     msgEntry->set_recv_time(entry.recvTime.time_since_epoch().count());
                     msgEntry->set_nb_conf(entry.nbConf);
                     if ((entry.walletIds.size() == 1) && entry.walletIds.cbegin()->empty()) {
                        msgEntry->add_wallet_ids(walletId);
                     }
                     else {
                        for (const auto &walletId : entry.walletIds) {
                           msgEntry->add_wallet_ids(walletId);
                        }
                     }
                     for (const auto &addr : entry.addresses) {
                        msgEntry->add_addresses(addr.display());
                     }
                  }
                  if (*stopped) {
                     return;
                  }
                  pushResponse(user_, env, msg.SerializeAsString());
               }
               catch (const std::exception &) {}
            };
            delegate->getHistoryPage(uint32_t(page), cbEntries);
         }
      };
      if (!delegate) {
         logger_->error("[BlockchainAdapter::processLedgerEntries] invalid ledger for {}", filter);
         return;
      }
      delegate->getPageCount(cbPage);
   };
   if (filter.empty()) {
      return armory_->getWalletsLedgerDelegate(cbLedger);
   }
   else {
      if (!addrStr.empty()) {
         try {
            const auto &addr = bs::Address::fromAddressString(addrStr);
            armory_->getLedgerDelegateForAddress(walletId, addr, cbLedger);
         }
         catch (const std::exception &e) {
            logger_->error("[{}] invalid address {} in filter: {}", __func__
               , addrStr, e.what());
            return true;
         }
      }
   }
   return true;
}

bool BlockchainAdapter::processLedgerUnsubscribe(const bs::message::Envelope &env
   , const std::string &filter)
{
   const auto &itSub = ledgerSubscriptions_.find(filter);
   if (itSub == ledgerSubscriptions_.end()) {
      return true;
   }
   const auto &itUser = std::find_if(itSub->second.cbegin(), itSub->second.cend()
      , [u=env.sender](const std::shared_ptr<bs::message::User> &user) {
      return (user->value() == u->value()); });
   if (itUser != itSub->second.end()) {
      itSub->second.erase(itUser);
      if (itSub->second.empty()) {
         ledgerSubscriptions_.erase(itSub);
      }
   }
}

bool BlockchainAdapter::processAddressHist(const bs::message::Envelope& env
   , const std::string& addrStr)
{
   bs::Address address;
   try {
      address = std::move(bs::Address::fromAddressString(addrStr));
   }
   catch (const std::exception& e) {
      logger_->error("[{}] invalid address string: {}", __func__, e.what());
      return true;
   }
   const auto& walletId = fortuna_.generateRandom(8).toHexStr();
   auto& newWallet = wallets_[walletId];
   newWallet.wallet = armoryPtr_->instantiateWallet(walletId);
   newWallet.asNew = true;
   newWallet.addresses = { address.id() };

   const auto& regId = newWallet.wallet->registerAddresses(newWallet.addresses
      , newWallet.asNew);
   addressSubscriptions_[regId] = { env, address, walletId };
   return true;
}

bool BlockchainAdapter::processFeeLevels(const bs::message::Envelope& env
   , const ArmoryMessage_FeeLevelsRequest& request)
{
   if (suspended_ || !armory_) {
      return false;
   }
   auto result = std::make_shared<std::map<unsigned int, float>>();
   for (auto level : request.levels()) {
      if (level < 2) {
         level = 2;
      }
      else if (level > 1008) {
         level = 1008;
      }
      const auto& cbFee = [this, env, level, result, size=request.levels_size(), stopped = stopped_]
         (float fee)
      {
         if (fee == std::numeric_limits<float>::infinity()) {
            (*result)[level] = fee;
         }
         else {
            fee = ArmoryConnection::toFeePerByte(fee);
            if (fee == 0) {
               SPDLOG_LOGGER_WARN(logger_, "Fees estimation for {} is not available, use hardcoded values!", level);
               if (level > 3) {
                  fee = 50;
               } else if (level >= 2) {
                  fee = 100;
               }
            }
            (*result)[level] = fee;
         }

         if (result->size() >= size) {
            ArmoryMessage msg;
            auto msgResp = msg.mutable_fee_levels_response();
            for (const auto& pair : *result) {
               auto respData = msgResp->add_fee_levels();
               respData->set_level(pair.first);
               respData->set_fee(pair.second);
            }
            if (*stopped) {
               return;
            }
            pushResponse(user_, env, msg.SerializeAsString());
         }
      };
      if (!armory_->estimateFee(level, cbFee)) {
         return false;
      }
   }
   return true;
}

bool BlockchainAdapter::processGetUTXOs(const bs::message::Envelope& env
   , const ArmoryMessage_WalletIDs& request, bool zc, bool rbf)
{
   std::vector<std::string> walletIDs;
   walletIDs.reserve(request.wallet_ids_size());
   for (const auto& walletId : request.wallet_ids()) {
      walletIDs.push_back(walletId);
   }
   const auto& cbTxOutList = [this, env, walletIDs, stopped = stopped_]
      (const std::vector<UTXO>& txOutList)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_utxos();
      if (!walletIDs.empty()) {
         msgResp->set_wallet_id(walletIDs.at(0));
      }
      for (const auto& utxo : txOutList) {
         msgResp->add_utxos(utxo.serialize().toBinStr());
      }
      if (*stopped) {
         return;
      }
      pushResponse(user_, env, msg.SerializeAsString());
   };
   if (walletIDs.empty()) {
      logger_->error("[{}] no wallet IDs in request", __func__);
      cbTxOutList({});
      return true;
   }
   if (zc) {
      return armory_->getSpendableZCoutputs(walletIDs, cbTxOutList);
   }
   else if (rbf) {
      return armory_->getRBFoutputs(walletIDs, cbTxOutList);
   }
   else {
      return armory_->getSpendableTxOutListForValue(walletIDs
         , std::numeric_limits<uint64_t>::max(), cbTxOutList);
   }
}

bool BlockchainAdapter::processUTXOsForAddr(const bs::message::Envelope& env
   , const ArmoryMessage_UTXOsForAddr& request)
{
   if (suspended_) {
      return false;
   }
   const auto& cbUTXOs = [this, env, stopped = stopped_]
      (const std::vector<UTXO>& utxos)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_utxos();
      for (const auto& utxo : utxos) {
         msgResp->add_utxos(utxo.serialize().toBinStr());
      }
      if (*stopped) {
         return;
      }
      pushResponse(user_, env, msg.SerializeAsString());
   };

   try {
      const auto& address = bs::Address::fromAddressString(request.address());
      return armory_->getUTXOsForAddress(address.prefixed(), cbUTXOs, request.with_zc());
   }
   catch (const std::exception& e) {
      logger_->error("[{}] invalid address {}: {}", __func__, request.address(), e.what());
      cbUTXOs({});
   }
   return true;
}

bool BlockchainAdapter::processGetOutpoints(const bs::message::Envelope& env
   , const ArmoryMessage_GetOutpointsForAddrList& request)
{
   auto cbOutpoints = [this, env, stopped = stopped_]
      (const OutpointBatch& outpointBatch)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_out_points();
      msgResp->set_height_cutoff(outpointBatch.heightCutoff_);
      msgResp->set_zc_index_cutoff(outpointBatch.zcIndexCutoff_);
      for (const auto& outpointsItem : outpointBatch.outpoints_) {
         auto opData = msgResp->add_outpoints();
         opData->set_id(outpointsItem.first.toBinStr());
         for (const auto& outpoint : outpointsItem.second) {
            auto inputData = opData->add_outpoints();
            inputData->set_hash(outpoint.txHash_.toBinStr());
            inputData->set_index(outpoint.txOutIndex_);
            inputData->set_tx_height(outpoint.txHeight_);
            inputData->set_tx_index(outpoint.txIndex_);
            inputData->set_value(outpoint.value_);
            inputData->set_spent(outpoint.isSpent_);
            inputData->set_spender_hash(outpoint.spenderHash_.toBinStr());
         }
      }
      if (*stopped) {
         return;
      }
      if (pushResponse(user_, env, msg.SerializeAsString())) {
         std::unique_lock<std::mutex> lock(mtxReqPool_);
         requestsPool_.erase(env.foreignId());
      }
   };
   {
      std::unique_lock<std::mutex> lock(mtxReqPool_);
      requestsPool_[env.foreignId()] = env;
   }
   std::vector<BinaryData> addrVec;
   for (const auto& addr : request.addresses()) {
      try {
         addrVec.push_back(bs::Address::fromAddressString(addr).prefixed());
      } catch (const std::exception& e) {
         SPDLOG_LOGGER_WARN(logger_, "invalid address: {}", e.what());
      }
   }
   if (addrVec.empty()) {
      cbOutpoints({});
   } else {
      if (!armory_->getOutpointsFor(addrVec, cbOutpoints, request.height()
         , request.zc_index())) {
         return false;
      }
   }
   return true;
}

bool BlockchainAdapter::processSpentnessRequest(const bs::message::Envelope& env
   , const ArmoryMessage_GetSpentness& request)
{
   std::map<BinaryData, std::set<uint32_t>> inputs;
   for (int i = 0; i < request.outpoints_size(); ++i) {
      const auto opData = request.outpoints(i);
      const auto txHash = BinaryData::fromString(opData.tx_hash());
      for (int j = 0; j < opData.out_indices_size(); ++j) {
         inputs[txHash].insert(opData.out_indices(j));
      }
   }
   const auto& sendSpentness = [this, env, inputs, stopped = stopped_]
   (const std::map<BinaryData, std::map<unsigned int, SpentnessResult>>& map
      , const std::string& errMsg = {})
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_spentness();
      if (errMsg.empty()) {
         for (const auto& input : inputs) {
            std::string outpoints;
            for (const auto& op : input.second) {
               outpoints += std::to_string(op) + " ";
            }
            if (*stopped) {
               return;
            }
            logger_->debug("[BlockchainAdapter::processSpentnessRequest] input"
               " {}, outpoints: {}", input.first.toHexStr(true), outpoints);
         }
         for (const auto& input : map) {
            auto inputData = msgResp->add_inputs();
            inputData->set_utxo_hash(input.first.toBinStr());
            for (const auto& spentness : input.second) {
               logger_->debug("[BlockchainAdapter::processSpentnessRequest] UTXO"
                  " {}: idx: {}, TXhash: {}, height: {}", input.first.toHexStr(true)
                  , spentness.first, spentness.second.spender_.toHexStr(true)
                  , spentness.second.height_);
               auto spentnessData = inputData->add_spentness();
               spentnessData->set_out_index(spentness.first);
               spentnessData->set_tx_hash(spentness.second.spender_.toBinStr());
               spentnessData->set_height(spentness.second.height_);
               spentnessData->set_state(static_cast<ArmoryMessage_Spentness_State>(spentness.second.state_));
            }
         }
      } else {
         msgResp->set_error_text(errMsg);
      }
      if (pushResponse(user_, env, msg.SerializeAsString())) {
         std::unique_lock<std::mutex> lock(mtxReqPool_);
         requestsPool_.erase(env.foreignId());
      }
   };

   const auto cbSpentness = [this, sendSpentness, request, stopped = stopped_]
   (const std::map<BinaryData, std::map<unsigned int, SpentnessResult>>& map
      , std::exception_ptr ePtr)
   {
      if (ePtr != nullptr) {
         try {
            std::rethrow_exception(ePtr);
         } catch (const std::exception& e) {
            sendSpentness({}, e.what());
            return;
         }
      }
      if (!request.allow_zc()) {
         sendSpentness(map);
         return;
      }
      bool found = false;
      for (const auto& input : map) {
         for (const auto& spentness : input.second) {
            if (*stopped) {
               return;
            }
            if (spentness.second.state_ == OutputSpentnessState::Spent) {
               found = true;
               break;
            }
         }
      }
      if (found) {
         sendSpentness(map);
         return;
      }

      const auto& cbZcSpentness = [this, sendSpentness, request]
      (const std::map<BinaryData, std::map<unsigned int, SpentnessResult>>& zcMap
         , std::exception_ptr ePtr)
      {
         if (ePtr != nullptr) {
            try {
               std::rethrow_exception(ePtr);
            } catch (const std::exception& e) {
               sendSpentness({}, e.what());
               return;
            }
         }
         sendSpentness(zcMap);
      };
      const auto& txHash = BinaryData::fromString(request.tx_hash());
      const std::map<BinaryData, std::set<uint32_t>> zcInputs{ { txHash, {0} } };
      logger_->debug("[BlockchainAdapter::processSpentnessRequest] calling"
         " ZC spentness for {}", txHash.toHexStr(true));
      armory_->getSpentnessForZcOutputs(zcInputs, cbZcSpentness);
   };

   {
      std::unique_lock<std::mutex> lock(mtxReqPool_);
      requestsPool_[env.foreignId()] = env;
   }
   armory_->getSpentnessForOutputs(inputs, cbSpentness);
   return true;
}

bool BlockchainAdapter::processGetOutputsForOPs(const bs::message::Envelope& env
   , const ArmoryMessage_GetOutputsForOPs& request)
{
   auto cbOutputs = [this, env, reqId = request.request_id(), stopped = stopped_]
   (const std::vector<UTXO>& utxos, std::exception_ptr eptr)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_outputs_for_ops();
      msgResp->set_request_id(reqId);
      if (!eptr) {
         for (const auto& utxo : utxos) {
            msgResp->add_utxos(utxo.serialize().toBinStr());
         }
      }
      if (*stopped) {
         return;
      }
      if (pushResponse(user_, env, msg.SerializeAsString())) {
         std::unique_lock<std::mutex> lock(mtxReqPool_);
         requestsPool_.erase(env.foreignId());
      }
   };
   {
      std::unique_lock<std::mutex> lock(mtxReqPool_);
      requestsPool_[env.foreignId()] = env;
   }
   std::map<BinaryData, std::set<uint32_t>> outpoints;
   for (int i = 0; i < request.outpoints_size(); ++i) {
      const auto& outpoint = request.outpoints(i);
      auto& indices = outpoints[BinaryData::fromString(outpoint.tx_hash())];
      for (int j = 0; j < outpoint.out_indices_size(); ++j) {
         indices.insert(outpoint.out_indices(j));
      }
   }
   if (outpoints.empty()) {
      cbOutputs({}, nullptr);
   } else {
      if (!armory_->getOutputsForOutpoints(outpoints, request.with_zc(), cbOutputs)) {
         SPDLOG_LOGGER_ERROR(logger_, "getOutputsForOutpoints failed");
         return false;
      }
   }
   return true;
}

bool BlockchainAdapter::processSubscribeAddressTX(const bs::message::Envelope& env
   , const std::string& addr)
{
   if (suspended_) {
      return false;
   }
   bs::Address address;
   try {
      address = bs::Address::fromAddressString(addr);
   }
   catch (const std::exception&) {
      logger_->error("[{}] invalid address {}", __func__, addr);
      return true;
   }
   const auto& itAddr = addrTxSubscriptions_.find(address);
   if (itAddr != addrTxSubscriptions_.end()) {
      logger_->debug("[{}] unsubscribing address {}", __func__, addr);
      unregisterWallet(addr);
      addrTxSubscriptions_.erase(itAddr);
      return true;
   }
   logger_->debug("[{}] subscribing address {}", __func__, addr);
   Wallet addrWallet;
   addrWallet.addresses = { address };
   addrWallet.asNew = true;
   registerWallet(addr, addrWallet);
   addrTxSubscriptions_[address] = { env.foreignId(), env.sender };
   return true;
}

void BlockchainAdapter::processZcForAddrSubscriptions(const bs::TXEntry& entry)
{
   for (const auto& subscription : addrTxSubscriptions_) {
      const auto& addrStr = subscription.first.display();
      const auto& itId = entry.walletIds.find(addrStr);
      if (itId == entry.walletIds.end()) {
         continue;
      }
      logger_->debug("[{}] found ZC {} for {}", __func__, entry.value, addrStr);
      ArmoryMessage msg;
      auto msgResp = msg.mutable_address_tx();
      msgResp->set_address(addrStr);
      msgResp->set_value(entry.value);
      msgResp->set_tx_hash(entry.txHash.toBinStr());
      pushResponse(user_, subscription.second.subscriber
         , msg.SerializeAsString(), (SeqId)EnvelopeType::Publish);
   }
}

void BlockchainAdapter::singleAddrWalletRegistered(const AddressHistRequest& request)
{
   const auto& itWallet = wallets_.find(request.walletId);
   if (itWallet != wallets_.end()) {
      itWallet->second.registered = true;
   }
   const auto& entries = std::make_shared<std::vector<bs::TXEntry>>();
   const auto& cbLedger = [this, request, itWallet, entries, stopped = stopped_]
      (const std::shared_ptr<AsyncClient::LedgerDelegate>& delegate)
   {
      const auto& cbPage = [this, delegate, request, itWallet, entries, stopped]
         (ReturnMessage<uint64_t> pageCntReturn)
      {
         uint32_t pageCnt = 0;
         try {
            pageCnt = (uint32_t)pageCntReturn.get();
         }
         catch (const std::exception&) {
            return;
         }
         for (uint32_t page = 0; page < pageCnt; ++page) {
            if (*stopped || suspended_) {
               return;
            }
            const auto& cbEntries = [this, request, page, pageCnt, itWallet, entries, stopped]
               (ReturnMessage<std::vector<ClientClasses::LedgerEntry>> entriesRet)
            {
               try {
                  auto le = entriesRet.get();
                  for (auto& entry : bs::TXEntry::fromLedgerEntries(le)) {
                     entry.nbConf = armory_->getConfirmationsNumber(entry.blockNum);
                     entries->emplace_back(std::move(entry));
                  }
               }
               catch (const std::exception&) {}
               if (page == (pageCnt - 1)) {  // remove temporary wallet on completion
                  itWallet->second.wallet->unregister();
                  wallets_.erase(itWallet);

                  ArmoryMessage msg;
                  auto msgResp = msg.mutable_address_history();
                  msgResp->set_address(request.address.display());
                  for (const auto& entry : *entries) {
                     auto msgEntry = msgResp->add_entries();
                     msgEntry->set_tx_hash(entry.txHash.toBinStr());
                     msgEntry->set_value(entry.value);
                     msgEntry->set_block_num(entry.blockNum);
                     msgEntry->set_tx_time(entry.txTime);
                     msgEntry->set_rbf(entry.isRBF);
                     msgEntry->set_chained_zc(entry.isChainedZC);
                     msgEntry->set_recv_time(entry.recvTime.time_since_epoch().count());
                     msgEntry->set_nb_conf(entry.nbConf);
                     if ((entry.walletIds.size() == 1) && entry.walletIds.cbegin()->empty()) {
                        msgEntry->add_wallet_ids(request.walletId);
                     }
                     else {
                        for (const auto& walletId : entry.walletIds) {
                           msgEntry->add_wallet_ids(walletId);
                        }
                     }
                     for (const auto& addr : entry.addresses) {
                        msgEntry->add_addresses(addr.display());
                     }
                  }
                  if (*stopped) {
                     return;
                  }
                  pushResponse(user_, request.env, msg.SerializeAsString());
               }
            };
            delegate->getHistoryPage(uint32_t(page), cbEntries);
         }
      };
      if (!delegate) {
         logger_->error("[BlockchainAdapter::processLedgerEntries] invalid ledger for {}", request.address.display());
         return;
      }
      delegate->getPageCount(cbPage);
   };
   armory_->getLedgerDelegateForAddress(request.walletId, request.address, cbLedger);
}
