#ifndef __SETTLEMENT_TRANSACTION_BROADCASTER_H__
#define __SETTLEMENT_TRANSACTION_BROADCASTER_H__

#include "ArmoryConnection.h"
#include "BinaryData.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <map>

class IdenticalTimersQueue;
class SingleShotTimer;

class SettlementTransactionBroadcaster : public ArmoryCallbackTarget
{
public:
   enum class BroadcastResultType
   {
      // SuccessfullyBroadcasted - for payin this means that pay-out could be broadcasted now
      SuccessfullyBroadcasted,
      // TXConflict - possbile double spent. TX was rejected but we should confirm that
      // spentness is confirmed before releasing cash reservation
      TXConflict,
      // RejectedByChain - TX itself was rejected. And will never be accepted. For payin - reservation could be released
      // for payout - should never happen
      // either broadcast error, or invalidated ZC
      RejectedByChain
   };
public:
   SettlementTransactionBroadcaster(const std::shared_ptr<spdlog::logger>& logger
                                    , ArmoryConnection *armory
                                    , const std::shared_ptr<IdenticalTimersQueue>& rebroadcastTimerQueue);
   ~SettlementTransactionBroadcaster() noexcept;

   SettlementTransactionBroadcaster(const SettlementTransactionBroadcaster&) = delete;
   SettlementTransactionBroadcaster& operator = (const SettlementTransactionBroadcaster&) = delete;

   SettlementTransactionBroadcaster(SettlementTransactionBroadcaster&&) = delete;
   SettlementTransactionBroadcaster& operator = (SettlementTransactionBroadcaster&&) = delete;

   void BroadcastTX(const BinaryData& tx, const BinaryData& txHash);

protected:
   virtual bool IsOurTx(const BinaryData& txHash) = 0;

   virtual void ProcessBroadcastedZC(const BinaryData& txHash) = 0;
   virtual bool ProcessTxConflict(const BinaryData& txHash, const std::string& errorMessage) = 0;
   virtual bool ProcessFailedBroadcast(const BinaryData& txHash, const std::string& errorMessage) = 0;
   virtual bool ProcessInvalidatedTx(const BinaryData& txHash) = 0;

public:
   void onZCReceived(const std::vector<bs::TXEntry>& entries) override;
   void onTxBroadcastError(const std::string &txHash, const std::string &errMsg) override;
   void onZCInvalidated(const std::set<BinaryData> &ids) override;

protected:
   bool RemoveTxFromPool(const BinaryData& txHash);

private:
   bool AddTxToBroadcastPool(const BinaryData& tx, const BinaryData& txHash);

   void RebroadcastPendingTransactions();

private:
   std::shared_ptr<spdlog::logger>  logger_;

   std::atomic_flag                 pendingTxLock_ = ATOMIC_FLAG_INIT;
   std::map<BinaryData, BinaryData> pendingTxMap_;

   std::shared_ptr<IdenticalTimersQueue>  rebroadcastTimerQueue_;
   std::shared_ptr<SingleShotTimer>       rebroadcastTimer_;
};

#endif // __SETTLEMENT_TRANSACTION_BROADCASTER_H__
