#ifndef __SETTLEMENT_MONITOR_H__
#define __SETTLEMENT_MONITOR_H__

#include "ArmoryConnection.h"
#include "SettlementAddressEntry.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>

#include <QObject>

namespace bs {

   class SettlementAddress
   {
   public:
      SettlementAddress(const std::vector<BinaryData> &scripts
         , const BinaryData &buyKey, const BinaryData &sellKey)
         : scripts_(scripts), buyPubKey_(buyKey), sellPubKey_(sellKey) {}

      std::vector<BinaryData> supportedAddrHashes() const { return scripts_; }
      BinaryData getScript() const {
         if (!scripts_.empty()) {
            return scripts_[0];
         }
         return {};
      }

      BinaryData buyChainedPubKey() const { return buyPubKey_; }
      BinaryData sellChainedPubKey() const { return sellPubKey_; }

   private:
      std::vector<BinaryData> scripts_;
      BinaryData  buyPubKey_;
      BinaryData  sellPubKey_;
   };


   struct PayoutSigner
   {
      enum Type {
         SignatureUndefined,
         SignedByBuyer,
         SignedBySeller
      };

      static const char *toString(const Type t) {
         switch (t) {
         case SignedByBuyer:        return "buyer";
         case SignedBySeller:       return "seller";
         case SignatureUndefined:
         default:                   return "undefined";
         }
      }

      static void WhichSignature(const Tx &
         , uint64_t value
         , const std::shared_ptr<SettlementAddress> &
         , const std::shared_ptr<spdlog::logger> &
         , const std::shared_ptr<ArmoryConnection> &, std::function<void(Type)>);
   };

   std::shared_ptr<SettlementAddress> entryToAddress(const std::shared_ptr<core::SettlementAddressEntry> &);

   class SettlementMonitor
   {
   public:
      SettlementMonitor(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<core::SettlementAddressEntry> &
         , const std::shared_ptr<spdlog::logger> &);
      SettlementMonitor(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<SettlementAddress> &, const bs::Address &
         , const std::shared_ptr<spdlog::logger> &);

      virtual ~SettlementMonitor() noexcept;

      int getPayinConfirmations() const { return payinConfirmations_; }
      int getPayoutConfirmations() const { return payoutConfirmations_; }

      PayoutSigner::Type getPayoutSignerSide() const { return payoutSignedBy_; }

      int confirmedThreshold() const { return 6; }

   protected:
      // payin detected is sent on ZC and once it's get to block.
      // if payin is already on chain before monitor started, payInDetected will
      // emited only once
      virtual void onPayInDetected(int confirmationsNumber, const BinaryData &txHash) = 0;
      virtual void onPayOutDetected(int confirmationsNumber, PayoutSigner::Type signedBy) = 0;
      virtual void onPayOutConfirmed(PayoutSigner::Type signedBy) = 0;

   public:
      void checkNewEntries();

   private:
      std::atomic_flag                          walletLock_ = ATOMIC_FLAG_INIT;
      std::shared_ptr<AsyncClient::BtcWallet>   rtWallet_;
      std::set<BinaryData>                      ownAddresses_;

      std::shared_ptr<SettlementAddress>  addressEntry_;

      int payinConfirmations_ = -1;
      int payoutConfirmations_ = -1;

      bool payinInBlockChain_ = false;
      bool payoutConfirmedFlag_ = false;

      PayoutSigner::Type payoutSignedBy_ = PayoutSigner::Type::SignatureUndefined;

   protected:
      std::shared_ptr<ArmoryConnection>         armory_;
      std::shared_ptr<spdlog::logger>           logger_;
      std::string                               addressString_;

   protected:
      void IsPayInTransaction(const ClientClasses::LedgerEntry &, std::function<void(bool)>) const;
      void IsPayOutTransaction(const ClientClasses::LedgerEntry &, std::function<void(bool)>) const;

      void CheckPayoutSignature(const ClientClasses::LedgerEntry &, std::function<void(PayoutSigner::Type)>) const;

      void SendPayInNotification(const int confirmationsNumber, const BinaryData &txHash);
      void SendPayOutNotification(const ClientClasses::LedgerEntry &);
   };

   class SettlementMonitorQtSignals : public QObject, public SettlementMonitor
   {
   Q_OBJECT;
   public:
      SettlementMonitorQtSignals(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<core::SettlementAddressEntry> &
         , const std::shared_ptr<spdlog::logger> &);
      SettlementMonitorQtSignals(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<SettlementAddress> &, const bs::Address &
         , const std::shared_ptr<spdlog::logger> &);
      ~SettlementMonitorQtSignals() noexcept override;

      SettlementMonitorQtSignals(const SettlementMonitorQtSignals&) = delete;
      SettlementMonitorQtSignals& operator = (const SettlementMonitorQtSignals&) = delete;

      SettlementMonitorQtSignals(SettlementMonitorQtSignals&&) = delete;
      SettlementMonitorQtSignals& operator = (SettlementMonitorQtSignals&&) = delete;

      void start();
      void stop();

   protected slots:
      void onBlockchainEvent(unsigned int);
      void onZCEvent(const std::vector<bs::TXEntry>);

   signals:
      void payInDetected(int confirmationsNumber, const BinaryData &txHash);
      void payOutDetected(int confirmationsNumber, PayoutSigner::Type signedBy);
      void payOutConfirmed(PayoutSigner::Type signedBy);

   protected:
      void onPayInDetected(int confirmationsNumber, const BinaryData &txHash) override;
      void onPayOutDetected(int confirmationsNumber, PayoutSigner::Type signedBy) override;
      void onPayOutConfirmed(PayoutSigner::Type signedBy) override;
   };

   class SettlementMonitorCb : public SettlementMonitor
   {
   public:
      using onPayInDetectedCB = std::function<void (int, const BinaryData &)>;
      using onPayOutDetectedCB = std::function<void (int, PayoutSigner::Type )>;
      using onPayOutConfirmedCB = std::function<void (PayoutSigner::Type )>;

   public:
      SettlementMonitorCb(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<core::SettlementAddressEntry> &
         , const std::shared_ptr<spdlog::logger> &);
      SettlementMonitorCb(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const std::shared_ptr<SettlementAddress> &, const bs::Address &
         , const std::shared_ptr<spdlog::logger> &);
      ~SettlementMonitorCb() noexcept override;

      SettlementMonitorCb(const SettlementMonitorCb&) = delete;
      SettlementMonitorCb& operator = (const SettlementMonitorCb&) = delete;

      SettlementMonitorCb(SettlementMonitorCb&&) = delete;
      SettlementMonitorCb& operator = (SettlementMonitorCb&&) = delete;

      void start(const onPayInDetectedCB& onPayInDetected
         , const onPayOutDetectedCB& onPayOutDetected
         , const onPayOutConfirmedCB& onPayOutConfirmed);

      void stop();

   protected:
      void onPayInDetected(int confirmationsNumber, const BinaryData &txHash) override;
      void onPayOutDetected(int confirmationsNumber, PayoutSigner::Type signedBy) override;
      void onPayOutConfirmed(PayoutSigner::Type signedBy) override;

   private:
      onPayInDetectedCB    onPayInDetected_;
      onPayOutDetectedCB   onPayOutDetected_;
      onPayOutConfirmedCB  onPayOutConfirmed_;
   };

} //namespace bs

#endif // __SETTLEMENT_MONITOR_H__
