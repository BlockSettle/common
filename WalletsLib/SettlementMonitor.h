#ifndef __SETTLEMENT_MONITOR_H__
#define __SETTLEMENT_MONITOR_H__

#include "ArmoryConnection.h"
#include "SettlementAddressEntry.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>

#include <QObject>

namespace bs
{

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
      , const std::shared_ptr<bs::SettlementAddressEntry> &
      , const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<ArmoryConnection> &, std::function<void(Type)>);
};

class SettlementMonitor
{
public:
   SettlementMonitor(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
      , const std::shared_ptr<ArmoryConnection> &
      , const shared_ptr<bs::SettlementAddressEntry> &
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

   std::string                               addressString_;
   shared_ptr<bs::SettlementAddressEntry>    addressEntry_;

   int payinConfirmations_ = -1;
   int payoutConfirmations_ = -1;

   bool payinInBlockChain_ = false;
   bool payoutConfirmedFlag_ = false;

   PayoutSigner::Type payoutSignedBy_ = PayoutSigner::Type::SignatureUndefined;

protected:
   std::shared_ptr<ArmoryConnection>   armory_;
   std::shared_ptr<spdlog::logger>     logger_;

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
      , const shared_ptr<bs::SettlementAddressEntry> &
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
{};

} //namespace bs

#endif // __SETTLEMENT_MONITOR_H__