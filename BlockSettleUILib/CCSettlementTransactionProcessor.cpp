#include "CCSettlementTransactionProcessor.h"

#include "ApplicationSettings.h"
#include "AssetManager.h"
#include "CheckRecipSigner.h"
#include "SignContainer.h"
#include "QuoteProvider.h"
#include "SelectedTransactionInputs.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include "CelerClient.h"
#include "ReqCCSettlementContainer.h"

#include <QLabel>

#include <spdlog/logger.h>

CCSettlementTransactionProcessor::~CCSettlementTransactionProcessor() noexcept = default;

CCSettlementTransactionProcessor::CCSettlementTransactionProcessor(
   const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<CelerClient> &celerClient
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<ReqCCSettlementContainer> &settlContainer
   , const std::shared_ptr<ConnectionManager> &connectionManager
   , QWidget* parent)
   : QDialog(parent)
   , logger_(logger)
   , appSettings_(appSettings)
   , settlContainer_(settlContainer)
   , connectionManager_(connectionManager)
{
  // connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &CCSettlementTransactionProcessor::onCancel);
  // connect(ui_->pushButtonAccept, &QPushButton::clicked, this, &CCSettlementTransactionProcessor::onAccept);
   connect(settlContainer_.get(), &ReqCCSettlementContainer::genAddrVerified, this, &CCSettlementTransactionProcessor::onGenAddrVerified, Qt::QueuedConnection);
   connect(settlContainer_.get(), &ReqCCSettlementContainer::paymentVerified, this, &CCSettlementTransactionProcessor::onPaymentVerified, Qt::QueuedConnection);
   connect(settlContainer_.get(), &ReqCCSettlementContainer::error, this, &CCSettlementTransactionProcessor::onError);
   connect(settlContainer_.get(), &ReqCCSettlementContainer::info, this, &CCSettlementTransactionProcessor::onInfo);
   connect(settlContainer_.get(), &ReqCCSettlementContainer::timerTick, this, &CCSettlementTransactionProcessor::onTimerTick);
   connect(settlContainer_.get(), &ReqCCSettlementContainer::timerExpired, this, &CCSettlementTransactionProcessor::onTimerExpired);
  // connect(settlContainer_.get(), &ReqCCSettlementContainer::timerStarted, [this](int msDuration) { ui_->progressBar->setMaximum(msDuration); });
   connect(settlContainer_.get(), &ReqCCSettlementContainer::walletInfoReceived, this, &CCSettlementTransactionProcessor::initSigning);
   connect(celerClient.get(), &CelerClient::OnConnectionClosed, this, &CCSettlementTransactionProcessor::onCancel);
  // connect(ui_->widgetSubmitKeys, &WalletKeysSubmitWidget::keyChanged, this, &CCSettlementTransactionProcessor::onKeyChanged);

   settlContainer_->activate();

   //ui_->pushButtonCancel->setEnabled(true);
   populateDetails();
}

void CCSettlementTransactionProcessor::onCancel()
{
   settlContainer_->cancel();
   //ui_->widgetSubmitKeys->cancel();
}

void CCSettlementTransactionProcessor::onTimerTick(int msCurrent, int)
{
   //ui_->progressBar->setValue(msCurrent);
   //ui_->progressBar->setFormat(tr("%n second(s) remaining", "", msCurrent / 1000));
}

void CCSettlementTransactionProcessor::onTimerExpired()
{
   onCancel();
}

void CCSettlementTransactionProcessor::populateDetails()
{
//   ui_->labelProductGroup->setText(tr(bs::network::Asset::toString(settlContainer_->assetType())));
//   ui_->labelSecurityID->setText(QString::fromStdString(settlContainer_->security()));
//   ui_->labelProduct->setText(QString::fromStdString(settlContainer_->product()));
//   ui_->labelSide->setText(tr(bs::network::Side::toString(settlContainer_->side())));

//   ui_->labelQuantity->setText(tr("%1 %2")
//      .arg(UiUtils::displayCCAmount(settlContainer_->quantity()))
//      .arg(QString::fromStdString(settlContainer_->product())));
//   ui_->labelPrice->setText(UiUtils::displayPriceCC(settlContainer_->price()));

//   ui_->labelTotalValue->setText(tr("%1").arg(UiUtils::displayAmount(settlContainer_->amount())));

//   if (settlContainer_->side() == bs::network::Side::Sell) {
//      window()->setWindowTitle(tr("Settlement Delivery"));
//      ui_->labelPaymentName->setText(tr("Delivery"));
//   }
//   else {
//      window()->setWindowTitle(tr("Settlement Payment"));
//      ui_->labelPaymentName->setText(tr("Payment"));
//   }

//   // addDetailRow(tr("Receipt address"), QString::fromStdString(dealerAddress_));
//   ui_->labelGenesisAddress->setText(tr("Verifying"));

//   ui_->labelPasswordHint->setText(tr("Enter \"%1\" wallet password to accept")
//      .arg(settlContainer_->walletInfo().name()));

   updateAcceptButton();
}

void CCSettlementTransactionProcessor::onGenAddrVerified(bool result, QString error)
{
//   logger_->debug("[CCSettlementTransactionProcessor::onGenAddrVerified] result = {} ({})", result, error.toStdString());
//   ui_->labelGenesisAddress->setText(result ? sValid_ : sInvalid_);
//   updateAcceptButton();

//   if (!result) {
//      ui_->labelHint->setText(tr("Failed to verify genesis address: %1").arg(error));
//   } else {
//      ui_->labelHint->setText(tr("Accept offer to send your own signed half of the CoinJoin transaction"));
//      initSigning();
//   }
}

void CCSettlementTransactionProcessor::initSigning()
{
   if (settlContainer_->walletInfo().encTypes().empty() || !settlContainer_->walletInfo().keyRank().first
      || !settlContainer_->isAcceptable()) {
      return;
   }

//   ui_->widgetSubmitKeys->init(AutheIDClient::SettlementTransaction, settlContainer_->walletInfo()
//      , WalletKeyWidget::UseType::RequestAuthInParent, logger_, appSettings_, connectionManager_);

//   ui_->widgetSubmitKeys->setFocus();
//   ui_->widgetSubmitKeys->resume();
}

void CCSettlementTransactionProcessor::onPaymentVerified(bool result, QString error)
{
//   if (!error.isEmpty()) {
//      ui_->labelHint->setText(error);
//   }
//   ui_->labelPayment->setText(result ? sValid_ : sInvalid_);
//   updateAcceptButton();
}

void CCSettlementTransactionProcessor::onError(QString text)
{
//   ui_->labelHint->setText(text);
//   updateAcceptButton();
}

void CCSettlementTransactionProcessor::onInfo(QString text)
{
//   ui_->labelHint->setText(text);
}

void CCSettlementTransactionProcessor::onKeyChanged()
{
//   updateAcceptButton();
//   if (ui_->widgetSubmitKeys->isKeyFinal()) {
//      onAccept();
//   }
}

void CCSettlementTransactionProcessor::onAccept()
{
//   ui_->progressBar->setValue(0);
//   ui_->labelHint->clear();
//   ui_->pushButtonAccept->setEnabled(false);
//   ui_->widgetSubmitKeys->setEnabled(false);

//   ui_->pushButtonCancel->setEnabled(false);

//   settlContainer_->accept(ui_->widgetSubmitKeys->key());
}

void CCSettlementTransactionProcessor::updateAcceptButton()
{
//   ui_->pushButtonAccept->setEnabled(settlContainer_->isAcceptable()
//      && ui_->widgetSubmitKeys->isValid());
}
