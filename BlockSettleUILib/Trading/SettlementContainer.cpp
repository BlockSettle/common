#include "SettlementContainer.h"
#include "UiUtils.h"

using namespace bs;
using namespace bs::sync;

SettlementContainer::SettlementContainer()
   : QObject(nullptr)
{}

sync::PasswordDialogData SettlementContainer::toPasswordDialogData() const
{
   bs::sync::PasswordDialogData info;

   info.setValue(PasswordDialogData::SettlementId, QString::fromStdString(id()));
   info.setValue(PasswordDialogData::Duration, durationMs());

   info.setValue(PasswordDialogData::ProductGroup, tr(bs::network::Asset::toString(assetType())));
   info.setValue(PasswordDialogData::Security, QString::fromStdString(security()));
   info.setValue(PasswordDialogData::Product, QString::fromStdString(product()));
   info.setValue(PasswordDialogData::Side, tr(bs::network::Side::toString(side())));

   return info;
}

sync::PasswordDialogData SettlementContainer::toPayOutTxDetailsPasswordDialogData(core::wallet::TXSignRequest payOutReq) const
{
   bs::sync::PasswordDialogData dialogData = toPasswordDialogData();

   dialogData.setValue(PasswordDialogData::Title, tr("Settlement Pay-Out"));
   dialogData.setValue(PasswordDialogData::Duration, 30000);
   dialogData.setValue(PasswordDialogData::SettlementPayOutVisible, true);

   return dialogData;
}

void SettlementContainer::startTimer(const unsigned int durationSeconds)
{
   timer_.stop();
   timer_.setInterval(250);
   msDuration_ = durationSeconds * 1000;
   startTime_ = std::chrono::steady_clock::now();

   connect(&timer_, &QTimer::timeout, [this] {
      const auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime_);
      msTimeLeft_ = msDuration_ - timeDiff.count();
      if (msTimeLeft_ < 0) {
         timer_.stop();
         msDuration_ = 0;
         msTimeLeft_ = 0;
         emit timerExpired();
      }
   });
   timer_.start();
   emit timerStarted(msDuration_);
}

void SettlementContainer::stopTimer()
{
   msDuration_ = 0;
   msTimeLeft_ = 0;
   timer_.stop();
   emit timerStopped();
}
