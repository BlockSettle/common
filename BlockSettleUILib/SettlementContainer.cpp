#include "SettlementContainer.h"
#include "ArmoryConnection.h"
#include "UiUtils.h"

using namespace bs;

SettlementContainer::SettlementContainer(const std::shared_ptr<ArmoryConnection> &armory)
   : QObject(nullptr), ArmoryCallbackTarget(armory.get())
{}

sync::SettlementInfo SettlementContainer::toSettlementInfo() const
{
   bs::sync::SettlementInfo info;
   info.setProductGroup(tr(bs::network::Asset::toString(assetType())));
   info.setSecurity(QString::fromStdString(security()));
   info.setProduct(QString::fromStdString(product()));
   info.setSide(tr(bs::network::Side::toString(side())));

   info.setPrice(UiUtils::displayPriceCC(price()));
   info.setQuantity(tr("%1 %2")
                    .arg(UiUtils::displayCCAmount(quantity()))
                    .arg(QString::fromStdString(product())));
   info.setTotalValue(tr("%1").arg(UiUtils::displayAmount(amount())));

   info.setGenesisAddress(tr("Verifying"));

   return info;
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
      else {
         emit timerTick(msTimeLeft_, msDuration_);
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
