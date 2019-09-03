#ifndef __XBT_SETTLEMENT_TRANSACTION_QML_INVOKER_H__
#define __XBT_SETTLEMENT_TRANSACTION_QML_INVOKER_H__

#include <QWidget>
#include <QTimer>

#include <memory>
#include <atomic>

#include "AuthAddress.h"
#include "CommonTypes.h"
#include "ConnectionManager.h"

namespace Ui {
    class XBTSettlementTransactionQmlInvoker;
}
namespace spdlog {
   class logger;
}
class ApplicationSettings;
class BaseCelerClient;
class ReqXBTSettlementContainer;


class XBTSettlementTransactionQmlInvoker : public QWidget
{
Q_OBJECT

public:
   XBTSettlementTransactionQmlInvoker(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<BaseCelerClient> &
      , const std::shared_ptr<ApplicationSettings> &
      , const std::shared_ptr<ReqXBTSettlementContainer> &
      , const std::shared_ptr<ConnectionManager> &
      , QWidget* parent = nullptr);
   ~XBTSettlementTransactionQmlInvoker() noexcept override;

private:
   void populateDetails();
   void onCancel();
   void onAccept();

   void populateXBTDetails();

private slots:
   void onStop();
   void onRetry();
   void updateAcceptButton();
   void onTimerExpired();
   void onTimerTick(int msCurrent, int msDuration);

   void onError(QString);
   void onInfo(QString);

   void onDealerVerificationStateChanged(AddressVerificationState);
   void onAuthWalletInfoReceived();

private:
   std::unique_ptr<Ui::XBTSettlementTransactionQmlInvoker> ui_;
   std::shared_ptr<spdlog::logger>              logger_;
   std::shared_ptr<ApplicationSettings>         appSettings_;
   std::shared_ptr<ReqXBTSettlementContainer>   settlContainer_;
   std::shared_ptr<ConnectionManager>           connectionManager_;

   const QString  sValid_;
   const QString  sInvalid_;
   const QString  sFailed_;
};

#endif // __XBT_SETTLEMENT_TRANSACTION_QML_INVOKER_H__
