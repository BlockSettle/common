#include "SettlementMonitor.h"

#include "CheckRecipSigner.h"
#include "CoinSelection.h"
#include "FastLock.h"
#include "TradesVerification.h"
#include "Wallets/SyncWallet.h"

std::string PayinStateToString(const bs::SettlementMonitor::PayinState& state)
{
   static const std::string stateUnknown = "Unknown";
   static const std::string stateAllOk = "AllOk";
   static const std::string stateNotSpent = "NotSpent";
   static const std::string stateDoubleSpend = "DoubleSpend";
   static const std::string stateSuspicious = "Suspicious";
   static const std::string stateBroadcastFailed = "BroadcastFailed";

   switch (state) {
   case bs::SettlementMonitor::PayinState::Unknown: return stateUnknown;
   case bs::SettlementMonitor::PayinState::AllOk: return stateAllOk;
   case bs::SettlementMonitor::PayinState::NotSpent: return stateNotSpent;
   case bs::SettlementMonitor::PayinState::DoubleSpend: return stateDoubleSpend;
   case bs::SettlementMonitor::PayinState::Suspicious: return stateSuspicious;
   case bs::SettlementMonitor::PayinState::BroadcastFailed: return stateBroadcastFailed;
   default:
      return std::string("Unexpeted payin state ") + std::to_string(static_cast<int>(state));
   }
}

std::string PayoutStateToString(const bs::SettlementMonitor::PayoutState& state)
{
   static const std::string stateNotDetected = "NotDetected";
   static const std::string stateSpent = "Spent";
   static const std::string stateRevoked = "Revoked";
   static const std::string stateSuspicious = "Suspicious";

   switch (state) {
   case bs::SettlementMonitor::PayoutState::NotDetected: return stateNotDetected;
   case bs::SettlementMonitor::PayoutState::Spent: return stateSpent;
   case bs::SettlementMonitor::PayoutState::Revoked: return stateRevoked;
   case bs::SettlementMonitor::PayoutState::Suspicious: return stateSuspicious;
   default:
      return std::string("Unexpeted payout state ") + std::to_string(static_cast<int>(state));
   }
}

bs::SettlementMonitor::SettlementMonitor(const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<IdenticalTimersQueue>& rebroadcastTimerQueue
   )
   : SettlementTransactionBroadcaster(logger, armory.get(), rebroadcastTimerQueue)
   , armoryPtr_(armory)
   , logger_(logger)
{
   init(armory.get());
}

bool bs::SettlementMonitor::InitMonitor(const std::shared_ptr<XBTTradeData>& tradeData
                                        , const EventCb &cb)
{
   try {
      const auto settlementId = BinaryData::CreateFromHex(tradeData->settlementId);

      const auto buyAuthKey = tradeData->requestorSellXBT
         ? BinaryData::CreateFromHex(tradeData->dealerPublicKey)
         : BinaryData::CreateFromHex(tradeData->requestorPublicKey);
      const auto sellAuthKey = tradeData->requestorSellXBT
         ? BinaryData::CreateFromHex(tradeData->requestorPublicKey)
         : BinaryData::CreateFromHex(tradeData->dealerPublicKey);

      buyAuthKey_ = CryptoECDSA::PubKeyScalarMultiply(buyAuthKey, settlementId);
      sellAuthKey_ = CryptoECDSA::PubKeyScalarMultiply(sellAuthKey, settlementId);

      bs::CheckRecipSigner deserSigner(tradeData->unsignedPayin);

      std::vector<UTXO> payinInputs;
      payinInputs.reserve(deserSigner.spenders().size());
      for (const auto &spender : deserSigner.spenders()) {
         payinInputs.push_back(spender->getUtxo());
      }

      for (const auto &utxo : payinInputs) {
         spentnessToTrack_[utxo.getTxHash()].insert(utxo.getTxOutIndex());
      }
      nbInputs_ = payinInputs.size();  // spentnessToTrack_.size() won't give accurate info

      settlAddress_ = bs::Address::fromAddressString(tradeData->settlementAddress);

      payinHash_ = tradeData->payinHash;
      payoutSpentnessToTrack_[payinHash_] = { 0 };

      settlValue_ = tradeData->xbtAmount.GetValue();

      Tx payoutTx(tradeData->signedPayout);
      payoutHash_ = payoutTx.getThisHash();

      btcWallet_ = armoryPtr_->instantiateWallet(tradeData->settlementId);

      tradeData_ = tradeData;
      cb_ = cb;

   } catch (const std::exception &e) {
      logger_->error("[SettlementMonitor::InitMonitor] failed to init with exception : {}"
                     , e.what());
      return false;
   } catch (...) {
      logger_->error("[SettlementMonitor::InitMonitor] failed to init with exception");
      return false;
   }

   return true;
}

bool bs::SettlementMonitor::PushSettlementOnChain()
{
   // register address and broadcast
   registrationId_ = btcWallet_->registerAddresses({settlAddress_.prefixed()}, false);
   return true;
}

void bs::SettlementMonitor::StopMonitoring()
{
   cb_ = nullptr;
   cleanup();
}

void bs::SettlementMonitor::onNewBlock(unsigned int, unsigned int)
{
   process();
}

void bs::SettlementMonitor::DumpSpentnessInfo(const std::map<BinaryData, std::map<unsigned int, std::pair<BinaryData, unsigned int>>> &map)
{
   logger_->debug("[SettlementMonitor::DumpSpentnessInfo] payin hash: {}. Size {}"
                  , payinHash_.toHexStr(true), map.size());

   for (const auto &it : map) {
      logger_->debug("[SettlementMonitor::DumpSpentnessInfo]    {} - size {}", it.first.toHexStr(true), it.second.size());
      for (const auto& item : it.second) {
         logger_->debug("[SettlementMonitor::DumpSpentnessInfo]        {} - {}  {}", item.first, item.second.first.toHexStr(true), item.second.second);
      }
   }
}

void bs::SettlementMonitor::process()
{
   if (!cb_) {
      // callback not set, skip any check. we either not initialized or stopped
      return;
   }
   const auto cbDetectPayout = [this](PayinState payinStateCurrent, int confPayIn) {
      const auto cbPayoutSpentness = [this, payinStateCurrent, confPayIn]
         (const std::map<BinaryData, std::map<unsigned int, std::pair<BinaryData, unsigned int>>> &map
         , std::exception_ptr ePtr)
      {
         BinaryDataRef spenderHash;
         unsigned spenderHeight;
         if (map.size() == 1) {
            auto mapIter = map.cbegin();
            if (mapIter->first == payinHash_ && mapIter->second.size() == 1) { //check outpoint hash
               auto spenderIter = mapIter->second.begin();
               spenderHeight = spenderIter->second.second;
               if (spenderIter->first == 0) { //check the outpoint id
                  spenderHash = spenderIter->second.first.getRef();
               }
            }
         }

         if (spenderHash.getSize() != 32) {
            UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::NotDetected, -1);
            return;
         }

         payoutConfirmations_ = armoryPtr_->getConfirmationsNumber(spenderHeight);

         //if the spender hash is that of our signed payout, things are going as expected
         if (spenderHash == payoutHash_) {
            UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::Spent, payoutConfirmations_);
            return;
         }

         //otherwise, grab the spender by hash and check the sigs
         const auto cbTX = [this, payinStateCurrent, confPayIn](const Tx &tx) {

            // xxx : missing the tx shouldn't result in payout undetected. something is wrong with the db
            // at this point in time, this need immediate human attention
            // all settlement monitoring results are not trusted after this point and should be stopped
            if (!tx.isInitialized()) {
               UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::NotDetected, -1);
               return;
            }
            std::string errorMsg;
            auto payoutSignedBy = TradesVerification::whichSignature(tx, settlValue_, settlAddress_, buyAuthKey_, sellAuthKey_, &errorMsg);
            if (!errorMsg.empty()) {
               SPDLOG_LOGGER_ERROR(logger_, "signature detection failed: {}", errorMsg);
               UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::NotDetected, payoutConfirmations_);
               return;
            }
            switch (payoutSignedBy) {
            case bs::PayoutSignatureType::ByBuyer:
               UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::Spent, payoutConfirmations_);
               break;
            case bs::PayoutSignatureType::BySeller:
               UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::Revoked, payoutConfirmations_);
               break;
            case bs::PayoutSignatureType::Failed:
            case bs::PayoutSignatureType::Undefined:
            default:
               logger_->debug("[SettlementMonitor::process] failed to detect signature side : {} {}"
                              , tradeData_->settlementId, static_cast<int>(payoutSignedBy));
               UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::NotDetected, payoutConfirmations_);
               break;
            }
         };

         if (!armoryPtr_->getTxByHash(spenderHash, cbTX)) {
            // armory connection is dowm, settlement result will be updated once armory back
            UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::NotDetected, -1);
            return;
         }
      };
      if (!armoryPtr_->getSpentnessForOutputs(payoutSpentnessToTrack_, cbPayoutSpentness)) {
         UpdateSettlementStatus(payinStateCurrent, confPayIn, PayoutState::NotDetected, payoutConfirmations_);
      }
   };

   const auto cbSpentness = [this, cbDetectPayout]
      (const std::map<BinaryData, std::map<unsigned int, std::pair<BinaryData, unsigned int>>> &map
      , std::exception_ptr ePtr)
   {
      if (ePtr) {
         return;
      }

      if (map.empty() || map.cbegin()->second.empty()) {
         // do not update that inputs are not spent
         return;
      }

      unsigned int payinHashMatches = 0;
      unsigned int maxHeight = 0;
      for (const auto &item : map) {
         for (const auto &txPair : item.second) {
            if (txPair.second.first == payinHash_) {
               payinHashMatches++;
            }
            if (txPair.second.second > maxHeight) {
               maxHeight = txPair.second.second;
            }
         }
      }

      PayinState payinStateLocal = PayinState::Unknown;

      if (payinHashMatches == nbInputs_) {
         payinStateLocal = PayinState::AllOk;
      }
      else if (payinHashMatches == 0) {
         payinStateLocal = PayinState::DoubleSpend;
         logger_->debug("[SettlementMonitor::SettlementMonitor] double spent detected: {}", tradeData_->settlementId);
         DumpSpentnessInfo(map);
      }
      else {
         payinStateLocal = PayinState::Suspicious;
         logger_->debug("[SettlementMonitor::SettlementMonitor] suspicious spent detected: {}", tradeData_->settlementId);
         DumpSpentnessInfo(map);
      }
      auto payinConfirmationsLocal = armoryPtr_->getConfirmationsNumber(maxHeight);

      if (payinStateLocal == PayinState::AllOk) {
         cbDetectPayout(payinStateLocal, payinConfirmationsLocal);
      } else {
         UpdateSettlementStatus(payinStateLocal, payinConfirmationsLocal, PayoutState::NotDetected, -1);
      }
   };
   armoryPtr_->getSpentnessForOutputs(spentnessToTrack_, cbSpentness);
}

bs::SettlementMonitor::~SettlementMonitor() noexcept
{
   // Stop callbacks just in case (calling cleanup below should be enough)
   validityFlag_.reset();

   StopMonitoring();
}

void bs::SettlementMonitor::onRefresh(const std::vector<BinaryData>& ids, bool)
{
   for (const auto& id : ids) {
      if (id == registrationId_) {
         // push pay-in on chain
         logger_->debug("[SettlementMonitor::onRefresh] broadcasting pay-in {}"
                        , tradeData_->settlementId);
         BroadcastTX(tradeData_->signedPayin, payinHash_);
      }
   }
}

void bs::SettlementMonitor::UpdateSettlementStatus(PayinState payinState, int confPayIn
                                                   , PayoutState payoutState, int confPayOut)
{
   if (!cb_) {
      // monitoring stopped, no reason to updated anything
      return;
   }

   if (payinState_ != payinState) {
      logger_->debug("[SettlementMonitor::UpdateSettlementStatus] payin state changed from {} to {} on {}"
                     , PayinStateToString(payinState_), PayinStateToString(payinState), tradeData_->settlementId);
   }
   payinState_ = payinState;
   payinConfirmations_ = confPayIn;

   if (payoutState_ != payoutState) {
      logger_->debug("[SettlementMonitor::UpdateSettlementStatus] payout state changed from {} to {} on {}"
                     , PayoutStateToString(payoutState_), PayoutStateToString(payoutState), tradeData_->settlementId);
   }
   payoutState_ = payoutState;
   payoutConfirmations_ = confPayOut;

   cb_(payinState_, payinConfirmations_, payoutState_, payoutConfirmations_);
}

bool bs::SettlementMonitor::IsOurTx(const BinaryData& txHash)
{
   return txHash == payinHash_ || txHash == payoutHash_;
}

void bs::SettlementMonitor::ProcessBroadcastedZC(const BinaryData& txHash)
{
   if (txHash == payinHash_) {
      logger_->debug("[SettlementMonitor::ProcessBroadcastedZC] payin broadcasted for {}. Broadcasting payout"
                     , tradeData_->settlementId);

      UpdateSettlementStatus(PayinState::AllOk, 0, PayoutState::NotDetected, -1);
      BroadcastTX(tradeData_->signedPayout, payoutHash_);
   } else if (txHash == payoutHash_) {
      logger_->debug("[SettlementMonitor::ProcessBroadcastedZC] payout broadcasted for {}");
      UpdateSettlementStatus(payinState_, payinConfirmations_, PayoutState::Spent, 0);
   }
}

bool bs::SettlementMonitor::ProcessTxConflict(const BinaryData& txHash, const std::string& errorMessage)
{
   if (txHash == payinHash_) {
      // report status. trade should be monitored further. possible double spend
      logger_->error("[SettlementMonitor::ProcessTxConflict] {} : {} on payin"
                     , tradeData_->settlementId, errorMessage);
      UpdateSettlementStatus(PayinState::DoubleSpend, payinConfirmations_, payoutState_, payoutConfirmations_);
   } else if (txHash == payoutHash_) {
      logger_->critical("[SettlementMonitor::ProcessTxConflict] {} : {} on payout"
                        , tradeData_->settlementId, errorMessage);
      UpdateSettlementStatus(PayinState::Suspicious, payinConfirmations_, PayoutState::Suspicious, payoutConfirmations_);
   }
   return true;
}

bool bs::SettlementMonitor::ProcessFailedBroadcast(const BinaryData& txHash, const std::string& errorMessage)
{
   // cancel trade
   if (txHash == payinHash_) {
      // report status. trade should be monitored further. possible double spend
      logger_->error("[SettlementMonitor::ProcessFailedBroadcast] {} : {} on payin"
                     , tradeData_->settlementId, errorMessage);
      UpdateSettlementStatus(PayinState::BroadcastFailed, -1, payoutState_, payoutConfirmations_);
   } else if (txHash == payoutHash_) {
      logger_->critical("[SettlementMonitor::ProcessFailedBroadcast] {} : {} on payout"
                        , tradeData_->settlementId, errorMessage);
      UpdateSettlementStatus(PayinState::Suspicious, payinConfirmations_, PayoutState::Suspicious, payoutConfirmations_);
   }
   return true;
}

bool bs::SettlementMonitor::ProcessInvalidatedTx(const BinaryData& txHash)
{
   // NOTE: we should not care about this notifications for settlement TX
   // when we receive this notification tha means that TZ is not in a mempool any more.
   // that could happen in 2 different cases
   // our TX was mined or other TX using same inputs is mined
   // in both cases we'll get our status during spentness check on a new block notification.
   return true;
}
