#ifndef __SETTLEMENT_MONITOR_H__
#define __SETTLEMENT_MONITOR_H__

#include "ArmoryConnection.h"
#include "CoreWallet.h"
#include "TradesVerification.h"
#include "ValidityFlag.h"
#include "SettlementTransactionBroadcaster.h"
#include "XBTTradeData.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>


namespace bs {

   class SettlementMonitor : public SettlementTransactionBroadcaster
   {
   public:
      enum class PayinState {
         Unknown,
         AllOk,
         NotSpent,
         DoubleSpend,
         Suspicious,
         BroadcastFailed
      };
      enum class PayoutState {
         NotDetected,
         Spent,
         Revoked,
         Suspicious
      };
      using EventCb = std::function<void(PayinState, int confPayIn
         , PayoutState, int confPayOut)>;

      SettlementMonitor(const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<spdlog::logger> &
         , const std::shared_ptr<IdenticalTimersQueue>& rebroadcastTimerQueue);

      ~SettlementMonitor() noexcept override;

      bool InitMonitor(const std::shared_ptr<XBTTradeData>& tradeData, const EventCb &userCb);
      bool PushSettlementOnChain();
      void StopMonitoring();

      void process();

   protected:
      bool IsOurTx(const BinaryData& txHash) override;
      void ProcessBroadcastedZC(const BinaryData& txHash) override;
      bool ProcessTxConflict(const BinaryData& txHash, const std::string& errorMessage) override;
      bool ProcessFailedBroadcast(const BinaryData& txHash, const std::string& errorMessage) override;
      bool ProcessInvalidatedTx(const BinaryData& txHash) override;

   protected:
      void onNewBlock(unsigned int height, unsigned int branchHgt) override;
      void onRefresh(const std::vector<BinaryData> &, bool) override;

   private:
      void UpdateSettlementStatus(PayinState, int confPayIn, PayoutState, int confPayOut);

      void DumpSpentnessInfo(const std::map<BinaryData, std::map<unsigned int, std::pair<BinaryData, unsigned int>>> &map);

   private:
      std::shared_ptr<ArmoryConnection>   armoryPtr_;
      std::shared_ptr<spdlog::logger>     logger_;

      std::shared_ptr<XBTTradeData>       tradeData_;

      bs::Address                         settlAddress_;
      SecureBinaryData                    buyAuthKey_;
      SecureBinaryData                    sellAuthKey_;
      uint64_t                            settlValue_{};

      ValidityFlag                        validityFlag_;

      BinaryData           payinHash_;
      BinaryData           payoutHash_;

      EventCb              cb_{};

      PayinState  payinState_{PayinState::Unknown};
      PayoutState  payoutState_{PayoutState::NotDetected};

      int payinConfirmations_ = -1;
      int payoutConfirmations_ = -1;

      std::map<BinaryData, std::set<uint32_t>>  spentnessToTrack_;
      std::map<BinaryData, std::set<uint32_t>>  payoutSpentnessToTrack_;
      unsigned int                              nbInputs_{};

      std::shared_ptr<AsyncClient::BtcWallet>   btcWallet_;

      std::string                               registrationId_;
   };

} //namespace bs

#endif // __SETTLEMENT_MONITOR_H__
