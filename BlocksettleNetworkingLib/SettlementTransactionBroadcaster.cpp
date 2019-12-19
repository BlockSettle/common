#include "SettlementTransactionBroadcaster.h"

#include "FastLock.h"
#include "IdenticalTimersQueue.h"
#include "SingleShotTimer.h"

#include <chrono>


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
   const std::string MempoolFool = "mempool full";

   // TxBroadcastTimeout - armory error message. should not happen on mainnet,
   // but just in case. We should report to log and try to broadcast further
   const std::string TxBroadcastTimeout = "tx broadcast timed out (send)";

   // no need to add this errors. Any other should be a good enough reason to cancel reservation
   //A transaction that spends outputs that would be replaced by it is invalid
   //TxValidationResult::TX_CONSENSUS, "bad-txns-spends-conflicting-tx"
};

SettlementTransactionBroadcaster::SettlementTransactionBroadcaster(const std::shared_ptr<spdlog::logger>& logger
                                                                   , ArmoryConnection *armory
                                                                   , const std::shared_ptr<IdenticalTimersQueue>& rebroadcastTimerQueue)
  : logger_{logger}
  , rebroadcastTimerQueue_{rebroadcastTimerQueue}
{
   init(armory);

   rebroadcastTimer_ = rebroadcastTimerQueue->CreateTimer([this](){ RebroadcastPendingTransactions(); }, "SettlementTransactionBroadcaster");
}

SettlementTransactionBroadcaster::~SettlementTransactionBroadcaster() noexcept
{
   cleanup();
}

void SettlementTransactionBroadcaster::onZCReceived(const std::vector<bs::TXEntry>& entries)
{
   for (const auto &entry : entries) {
      if (IsOurTx(entry.txHash)) {
         if (RemoveTxFromPool(entry.txHash)) {
            ProcessBroadcastedZC(entry.txHash);
         }
      }
   }
}

void SettlementTransactionBroadcaster::onTxBroadcastError(const std::string &txHashString, const std::string &errMsg)
{
   const auto txHash = BinaryData::CreateFromHex(txHashString);

   if (!IsOurTx(txHash)) {
      return;
   }

   if (errMsg == KnownBitcoinCoreErrors::TxAlreadyInMemPool) {
      logger_->debug("[SettlementTransactionBroadcaster::onTxBroadcastError] {} already in mempool. Processing as broadcasted"
                     , txHashString);

      RemoveTxFromPool(txHash);
      ProcessBroadcastedZC(txHash);
      return;
   }

   if (  errMsg == KnownBitcoinCoreErrors::TxAlreadyKnown
      || errMsg == KnownBitcoinCoreErrors::TxMempoolConflict)
   {
      logger_->error("[SettlementTransactionBroadcaster::onTxBroadcastError] {} - {}. Possible double spend"
                     , txHashString, errMsg);

      RemoveTxFromPool(txHash);
      ProcessTxConflict(txHash, errMsg);
      return;
   }

   if (errMsg == KnownBitcoinCoreErrors::MempoolFool) {
      // ignore. it will be rebroadcasted
      logger_->debug("[SettlementTransactionBroadcaster::onTxBroadcastError] {} not added to mempool. Keep trying."
                     , txHashString);
      return;
   }

   if (errMsg == KnownBitcoinCoreErrors::TxBroadcastTimeout) {
      logger_->debug("[SettlementTransactionBroadcaster::onTxBroadcastError] TX {} broadcast timeout. Keep trying"
                     , txHashString);
      return;
   }

   // any other error - we could stop settlement address watching
   RemoveTxFromPool(txHash);
   ProcessFailedBroadcast(txHash, errMsg);
}

void SettlementTransactionBroadcaster::onZCInvalidated(const std::set<BinaryData> &ids)
{
   for ( const auto& txHash : ids) {
      if (IsOurTx(txHash)) {
         ProcessInvalidatedTx(txHash);
      }
   }
}

void SettlementTransactionBroadcaster::BroadcastTX(const BinaryData& tx, const BinaryData& txHash)
{
   AddTxToBroadcastPool(tx, txHash);

   // ignore result. if failed it will stay in pool until broadcasted
   armory_->pushZC(tx);
}


bool SettlementTransactionBroadcaster::AddTxToBroadcastPool(const BinaryData& tx, const BinaryData& txHash)
{
   bool startTimer = false;
   {
      FastLock lock{pendingTxLock_};

      startTimer = pendingTxMap_.empty();

      auto it = pendingTxMap_.find(txHash);
      if (it != pendingTxMap_.end()) {
         return false;
      }

      pendingTxMap_.emplace_hint(it, txHash, tx);
   }

   if (startTimer) {
      rebroadcastTimerQueue_->ActivateTimer(rebroadcastTimer_);
   }
   return true;
}

bool SettlementTransactionBroadcaster::RemoveTxFromPool(const BinaryData& txHash)
{
   bool poolEmpty = false;
   {
      FastLock lock{pendingTxLock_};

      auto it = pendingTxMap_.find(txHash);
      if (it == pendingTxMap_.end()) {
         return false;
      }
      pendingTxMap_.erase(it);
      poolEmpty = pendingTxMap_.empty();
   }
   if (poolEmpty) {
      rebroadcastTimerQueue_->StopTimer(rebroadcastTimer_);
   }
   return true;
}

void SettlementTransactionBroadcaster::RebroadcastPendingTransactions()
{
   std::vector<BinaryData> txToBroadcast;

   {
      FastLock lock{pendingTxLock_};
      for (const auto& it  : pendingTxMap_) {
         txToBroadcast.emplace_back(it.second);
      }
   }

   for (const auto& tx : txToBroadcast) {
      armory_->pushZC(tx);
   }
}
