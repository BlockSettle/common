#include "XBTSettlementTransactionQmlInvoker.h"
#include "ui_XBTSettlementTransactionQmlInvoker.h"

#include "ReqXBTSettlementContainer.h"
#include "SelectedTransactionInputs.h"
#include "SignContainer.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include <CelerClient.h>

#include <spdlog/logger.h>


XBTSettlementTransactionQmlInvoker::XBTSettlementTransactionQmlInvoker(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<BaseCelerClient> &celerClient
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<ReqXBTSettlementContainer> &settlContainer
   , const std::shared_ptr<ConnectionManager> &connectionManager
   , QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::XBTSettlementTransactionQmlInvoker())
   , logger_(logger)
   , appSettings_(appSettings)
   , settlContainer_(settlContainer)
   , connectionManager_(connectionManager)
   , sValid_(tr("<span style=\"color: #22C064;\">Verified</span>"))
   , sInvalid_(tr("<span style=\"color: #CF292E;\">Invalid</span>"))
   , sFailed_(tr("<span style=\"color: #CF292E;\">Failed</span>"))
{
   ui_->setupUi(this);

//   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &XBTSettlementTransactionQmlInvoker::onCancel);
//   connect(ui_->pushButtonAccept, &QPushButton::clicked, this, &XBTSettlementTransactionQmlInvoker::onAccept);

   //connect(ui_->widgetSubmitKeys, &WalletKeysSubmitWidget::keyChanged, [this] { updateAcceptButton(); });
   //connect(ui_->widgetSubmitKeysAuth, &WalletKeysSubmitWidget::keyChanged, [this] { updateAcceptButton(); });

   connect(celerClient.get(), &BaseCelerClient::OnConnectionClosed,
      this, &XBTSettlementTransactionQmlInvoker::onCancel);

//   connect(settlContainer_.get(), &ReqXBTSettlementContainer::DealerVerificationStateChanged
//      , this, &XBTSettlementTransactionQmlInvoker::onDealerVerificationStateChanged, Qt::QueuedConnection);
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::error, this, &XBTSettlementTransactionQmlInvoker::onError);
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::info, this, &XBTSettlementTransactionQmlInvoker::onInfo);
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::timerTick, this, &XBTSettlementTransactionQmlInvoker::onTimerTick);
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::timerExpired, this, &XBTSettlementTransactionQmlInvoker::onTimerExpired);
   //connect(settlContainer_.get(), &ReqXBTSettlementContainer::timerStarted, [this](int msDuration) { ui_->progressBar->setMaximum(msDuration); });
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::retry, this, &XBTSettlementTransactionQmlInvoker::onRetry);
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::stop, this, &XBTSettlementTransactionQmlInvoker::onStop);
   connect(settlContainer_.get(), &ReqXBTSettlementContainer::authWalletInfoReceived, this, &XBTSettlementTransactionQmlInvoker::onAuthWalletInfoReceived);

   settlContainer_->activate();

   populateDetails();
   //ui_->pushButtonCancel->setEnabled(true);
   updateAcceptButton();
}

XBTSettlementTransactionQmlInvoker::~XBTSettlementTransactionQmlInvoker() noexcept = default;

void XBTSettlementTransactionQmlInvoker::onCancel()
{
   settlContainer_->cancel();
//   ui_->widgetSubmitKeys->cancel();
//   ui_->widgetSubmitKeysAuth->cancel();
}

void XBTSettlementTransactionQmlInvoker::onError(QString text)
{
   //ui_->labelHintPassword->setText(text);
   updateAcceptButton();
}

void XBTSettlementTransactionQmlInvoker::onInfo(QString text)
{
   //ui_->labelHintPassword->setText(text);
}

void XBTSettlementTransactionQmlInvoker::onTimerTick(int msCurrent, int)
{
//   ui_->progressBar->setValue(msCurrent);
//   ui_->progressBar->setFormat(tr("%n second(s) remaining", "", msCurrent / 1000));
//   ui_->labelTimeLeft->setText(tr("%n second(s) remaining", "", msCurrent / 1000));
}

void XBTSettlementTransactionQmlInvoker::onTimerExpired()
{
   //ui_->pushButtonCancel->setEnabled(false);
}

void XBTSettlementTransactionQmlInvoker::populateDetails()
{
//   ui_->labelProductGroup->setText(tr(bs::network::Asset::toString(settlContainer_->assetType())));
//   ui_->labelSecurityID->setText(QString::fromStdString(settlContainer_->security()));
//   ui_->labelProduct->setText(QString::fromStdString(settlContainer_->product()));
//   ui_->labelSide->setText(tr(bs::network::Side::toString(settlContainer_->side())));

//   QString qtyProd = UiUtils::XbtCurrency;
//   ui_->labelQuantity->setText(tr("%1 %2")
//      .arg(UiUtils::displayAmountForProduct(settlContainer_->amount(), qtyProd, bs::network::Asset::Type::SpotXBT))
//      .arg(qtyProd));

//   ui_->labelPrice->setText(UiUtils::displayPriceXBT(settlContainer_->price()));

//   const auto fxProd = QString::fromStdString(settlContainer_->fxProduct());
//   ui_->labelTotalValue->setText(tr("%1 %2")
//      .arg(UiUtils::displayAmountForProduct(settlContainer_->amount() * settlContainer_->price(), fxProd, bs::network::Asset::Type::SpotXBT))
//      .arg(fxProd));

//   populateXBTDetails();

//   if (settlContainer_->weSell()) {
//      if (settlContainer_->isSellFromPrimary()) {
//         ui_->labelHintAuthPassword->hide();
//         ui_->horizontalWidgetAuthPassword->hide();
//         //ui_->widgetSubmitKeysAuth->suspend();
//      }
//      else {
//         ui_->labelHintAuthPassword->setText(tr("Enter password for \"%1\" wallet to sign revoke Pay-Out")
//            .arg(settlContainer_->walletInfoAuth().name()));
//      }
//   }
//   else {
//      ui_->labelHintPassword->setText(tr("Enter password for \"%1\" wallet to sign Pay-Out")
//         .arg(settlContainer_->walletInfoAuth().name()));
//      ui_->labelHintAuthPassword->hide();
//      ui_->horizontalWidgetAuthPassword->hide();
//      //ui_->widgetSubmitKeysAuth->suspend();
//   }
}

void XBTSettlementTransactionQmlInvoker::onDealerVerificationStateChanged(AddressVerificationState state)
{
//   QString text;
//   switch (state) {
//   case AddressVerificationState::Verified: {
//         text = sValid_;
//         ui_->widgetSubmitKeys->init(AutheIDClient::SettlementTransaction, settlContainer_->walletInfo()
//            , WalletKeyWidget::UseType::RequestAuthInParent, logger_, appSettings_, connectionManager_);
//         ui_->widgetSubmitKeys->setFocus();
//         // tr("%1 Settlement %2").arg(QString::fromStdString(rfq_.security)).arg(clientSells_ ? tr("Pay-In") : tr("Pay-Out"))

//         if (settlContainer_->weSell() && !settlContainer_->isSellFromPrimary()) {
//            ui_->widgetSubmitKeysAuth->init(AutheIDClient::SettlementTransaction, settlContainer_->walletInfoAuth()
//            , WalletKeyWidget::UseType::RequestAuthInParent, logger_, appSettings_, connectionManager_);
//         }
//         QApplication::processEvents();
//         adjustSize();
//   }
//      break;
//   case AddressVerificationState::VerificationFailed:
//      text = sFailed_;
//      break;
//   default:
//      text = sInvalid_;
//      break;
//   }

//   ui_->labelDealerAuthAddress->setText(text);
//   updateAcceptButton();
}

void XBTSettlementTransactionQmlInvoker::onAuthWalletInfoReceived()
{
   //ui_->widgetSubmitKeysAuth->resume();
}

void XBTSettlementTransactionQmlInvoker::populateXBTDetails()
{
//   ui_->labelDealerAuthAddress->setText(tr("Validating"));
//   ui_->labelUserAuthAddress->setText(settlContainer_->userKeyOk() ? sValid_ : sInvalid_);

   if (settlContainer_->weSell()) {
      window()->setWindowTitle(tr("Settlement Pay-In (XBT)"));

      // addDetailRow(tr("Sending wallet"), tr("<b>%1</b> (%2)").arg(QString::fromStdString(transactionData_->GetWallet()->GetWalletName()))
      //    .arg(QString::fromStdString(transactionData_->GetWallet()->GetWalletId())));
      // addDetailRow(tr("Number of inputs"), tr("T<b>%L1</b>")
      //    .arg(QString::number(transactionData_->GetTransactionSummary().usedTransactions)));

      //ui_->labelHintPassword->setText(tr("Enter Password and press \"Accept\" to send Pay-In"));
   }
   else {
      window()->setWindowTitle(tr("Settlement Pay-Out (XBT)"));

      // addDetailRow(tr("Receiving wallet"), tr("<b>%1</b> (%2)").arg(QString::fromStdString(transactionData_->GetWallet()->GetWalletName()))
      //    .arg(QString::fromStdString(transactionData_->GetWallet()->GetWalletId())));
      // addDetailRow(tr("Receiving address"), tr("<b>%1</b>").arg(recvAddr_.display()));

     // ui_->labelHintPassword->setText(tr("Enter Password and press \"Accept\" to send Pay-Out to dealer"));
   }


}

void XBTSettlementTransactionQmlInvoker::onAccept()
{
   // FIXME: this widget needs to be reimplemented to move signing to signer

//   SecureBinaryData authKey = (settlContainer_->weSell() && !settlContainer_->isSellFromPrimary() && settlContainer_->payinReceived())
//      ? ui_->widgetSubmitKeysAuth->key() : ui_->widgetSubmitKeys->key();
//   settlContainer_->accept(authKey);

//   if (!settlContainer_->payinReceived() && !settlContainer_->weSell()) {
//      ui_->pushButtonCancel->setEnabled(false);
//   }
//   ui_->labelHintPassword->clear();
//   ui_->pushButtonAccept->setEnabled(false);
}

void XBTSettlementTransactionQmlInvoker::onStop()
{
//   ui_->progressBar->setValue(0);
//   ui_->widgetSubmitKeys->setEnabled(false);
//   ui_->labelHintAuthPassword->clear();
//   ui_->widgetSubmitKeysAuth->setEnabled(false);
}

void XBTSettlementTransactionQmlInvoker::onRetry()
{
//   ui_->pushButtonAccept->setText(tr("Retry"));
//   ui_->widgetSubmitKeys->setEnabled(true);
//   ui_->widgetSubmitKeysAuth->setEnabled(true);
//   ui_->pushButtonAccept->setEnabled(true);
}

void XBTSettlementTransactionQmlInvoker::updateAcceptButton()
{
//   bool isPasswordValid = ui_->widgetSubmitKeys->isValid();
//   if (settlContainer_->weSell() && !settlContainer_->isSellFromPrimary()) {
//      isPasswordValid = ui_->widgetSubmitKeysAuth->isValid();
//   }
//   ui_->pushButtonAccept->setEnabled(settlContainer_->isAcceptable() && isPasswordValid);
}
