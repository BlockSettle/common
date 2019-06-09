#ifndef __CC_SETTLEMENT_TRANSACTION_PROCESSOR_H__
#define __CC_SETTLEMENT_TRANSACTION_PROCESSOR_H__

#include <QDialog>
#include <QTimer>
#include <memory>
#include <atomic>

#include "BinaryData.h"
#include "CheckRecipSigner.h"
#include "CommonTypes.h"
#include "UtxoReservation.h"

namespace spdlog {
   class logger;
}
class ApplicationSettings;
class CelerClient;
class WalletsManager;
class ReqCCSettlementContainer;
class ConnectionManager;

namespace SwigClient
{
   class BtcWallet;
}

class CCSettlementTransactionProcessor : public QDialog
{
Q_OBJECT

public:
   CCSettlementTransactionProcessor(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<CelerClient> &
      , const std::shared_ptr<ApplicationSettings> &
      , const std::shared_ptr<ReqCCSettlementContainer> &
      , const std::shared_ptr<ConnectionManager> &connectionManager
      , QWidget* parent = nullptr);
   ~CCSettlementTransactionProcessor() noexcept override;

private:
   void onCancel();
   void onAccept();
   void populateDetails();

private slots:
   void onTimerExpired();
   void onTimerTick(int msCurrent, int msDuration);
   void initSigning();
   void updateAcceptButton();
   void onGenAddrVerified(bool, QString);
   void onPaymentVerified(bool, QString);
   void onError(QString);
   void onInfo(QString);
   void onKeyChanged();

private:
   std::shared_ptr<spdlog::logger>     logger_;
   const std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<ReqCCSettlementContainer>    settlContainer_;
   std::shared_ptr<ConnectionManager>           connectionManager_;
};

#endif // __CC_SETTLEMENT_TRANSACTION_PROCESSOR_H__
