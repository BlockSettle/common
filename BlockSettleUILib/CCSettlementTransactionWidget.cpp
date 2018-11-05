#include "CCSettlementTransactionWidget.h"
#include "ui_CCSettlementTransactionWidget.h"

#include "AssetManager.h"
#include "CheckRecipSigner.h"
#include "SignContainer.h"
#include "QuoteProvider.h"
#include "SelectedTransactionInputs.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include "WalletsManager.h"
#include "HDWallet.h"
#include "CelerClient.h"

#include <QLabel>
#include <QtConcurrent/QtConcurrentRun>

#include <spdlog/logger.h>

const unsigned int WaitTimeoutInSec = 30;

CCSettlementTransactionWidget::CCSettlementTransactionWidget(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<AssetManager> &assetManager
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<CelerClient> &celerClient
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::CCSettlementTransactionWidget())
   , logger_(logger)
   , assetManager_(assetManager)
   , signingContainer_(container)
   , armory_(armory)
   , timer_(this)
   , sValid(tr("<span style=\"color: #22C064;\">Verified</span>"))
   , sInvalid(tr("<span style=\"color: #CF292E;\">Invalid</span>"))
   , appSettings_(appSettings)
   , signer_(armory_)
{
   ui_->setupUi(this);

   setupTimer();

   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &CCSettlementTransactionWidget::onCancel);
   connect(ui_->pushButtonAccept, &QPushButton::clicked, this, &CCSettlementTransactionWidget::onAccept);
   connect(this, &CCSettlementTransactionWidget::genAddrVerified, this, &CCSettlementTransactionWidget::onGenAddrVerified);
   connect(ui_->widgetSubmitKeys, &WalletKeysSubmitWidget::keyChanged, [this] { updateAcceptButton(); });

   utxoAdapter_ = std::make_shared<bs::UtxoReservation::Adapter>();
   bs::UtxoReservation::addAdapter(utxoAdapter_);

   connect(celerClient.get(), &CelerClient::OnConnectionClosed,
      this, &CCSettlementTransactionWidget::onCancel);

   connect(signingContainer_.get(), &SignContainer::HDWalletInfo, this, &CCSettlementTransactionWidget::onHDWalletInfo);
   connect(signingContainer_.get(), &SignContainer::TXSigned, this, &CCSettlementTransactionWidget::onTXSigned);

   ui_->pushButtonAccept->setEnabled(false);
}

CCSettlementTransactionWidget::~CCSettlementTransactionWidget()
{
   bs::UtxoReservation::delAdapter(utxoAdapter_);
}

void CCSettlementTransactionWidget::setupTimer()
{
   ui_->progressBar->setMaximum(WaitTimeoutInSec * 1000);
   timer_.setInterval(500);
   connect(&timer_, &QTimer::timeout, this, &CCSettlementTransactionWidget::ticker);
}

void CCSettlementTransactionWidget::onCancel()
{
   cancel();
   emit settlementCancelled();
}

void CCSettlementTransactionWidget::cancel()
{
   timer_.stop();
   if (clientSells_) {
      utxoAdapter_->unreserve(reserveId_);
   }
   ui_->widgetSubmitKeys->cancel();
}

void CCSettlementTransactionWidget::reset(const std::shared_ptr<WalletsManager> &walletsManager)
{
   walletsManager_ = walletsManager;
   expireTime_ = QDateTime::currentDateTime().addSecs(WaitTimeoutInSec);
   ticker();
   timer_.start();
   ui_->pushButtonCancel->setEnabled(true);
}

void CCSettlementTransactionWidget::ticker()
{
   const auto timeDiff = QDateTime::currentDateTime().msecsTo(expireTime_);
   if (timeDiff < 0) {
      timer_.stop();
      emit settlementCancelled();
   } else {
      ui_->progressBar->setValue(timeDiff);
      ui_->progressBar->setFormat(tr("%n second(s) remaining", "", timeDiff / 1000));
   }
}

void CCSettlementTransactionWidget::populateDetails(const bs::network::RFQ& rfq
   , const bs::network::Quote& quote
   , const std::shared_ptr<TransactionData>& transactionData
   , const bs::Address &genAddr)
{
   transactionData_ = transactionData;
   rfq_ = rfq;
   quote_ = quote;

   product_ = rfq.product;

   amount_ = quote.quantity;
   QString qtyProd = QString::fromStdString(quote.product);
   price_ = quote.price;

   ui_->labelProductGroup->setText(tr(bs::network::Asset::toString(rfq.assetType)));
   ui_->labelSecurityID->setText(QString::fromStdString(rfq.security));
   ui_->labelProduct->setText(QString::fromStdString(rfq.product));
   ui_->labelSide->setText(tr(bs::network::Side::toString(rfq.side)));

   ui_->labelQuantity->setText(tr("%1 %2")
      .arg(UiUtils::displayCCAmount(amount_))
      .arg(qtyProd));

   ui_->labelPrice->setText(UiUtils::displayPriceCC(quote.price));

   ui_->labelTotalValue->setText(tr("%1")
      .arg(UiUtils::displayAmount(amount_ * price_)));


   clientSells_ = (rfq.side == bs::network::Side::Sell);

   if (clientSells_) {
      window()->setWindowTitle(tr("Settlement Delivery"));
      ui_->labelPaymentName->setText(tr("Delivery"));
   } else {
      window()->setWindowTitle(tr("Settlement Payment"));
      ui_->labelPaymentName->setText(tr("Payment"));
   }

   populateCCDetails(rfq, quote, genAddr);

   auto signingWallet = transactionData->GetSigningWallet();
   if (signingWallet) {
      const auto &rootWallet = walletsManager_->GetHDRootForLeaf(signingWallet->GetWalletId());
      infoReqId_ = signingContainer_->GetInfo(rootWallet);
      auto walletName = QString::fromStdString(rootWallet->getName());
      ui_->labelPasswordHint->setText(tr("Enter \"%1\" wallet password to accept").arg(walletName));
   }
   updateAcceptButton();
}

void CCSettlementTransactionWidget::populateCCDetails(const bs::network::RFQ& rfq, const bs::network::Quote& quote, const bs::Address &genAddr)
{
   userKeyOk_ = true;
   // for SPOT CC dealerAuthPublicKey used to pass along dealer recv address
   dealerAddress_ = quote.dealerAuthPublicKey;

   reserveId_ = rfq.requestId;
   dealerTx_ = BinaryData::CreateFromHex(quote.dealerTransaction);
   requesterTx_ = BinaryData::CreateFromHex(rfq.coinTxInput);

   // addDetailRow(tr("Receipt address"), QString::fromStdString(dealerAddress_));

   if (!clientSells_) {
      if ((amount_ * price_) > assetManager_->getBalance(bs::network::XbtCurrency, transactionData_->GetSigningWallet())) {
         ui_->labelHint->setText(tr("Insufficient XBT balance in signing wallet"));
         userKeyOk_ = false;
      }

      if (!userKeyOk_) {
         ui_->labelPayment->setText(sInvalid);
         return;
      }
   }

   userKeyOk_ = false;
   bool foundRecipAddr = false;
   bool amountValid = false;
   const auto lotSize = assetManager_->getCCLotSize(product_);
   try {
      if (!lotSize) {
         throw std::runtime_error("invalid lot size");
      }
      signer_.deserializeState(dealerTx_);
      foundRecipAddr = signer_.findRecipAddress(bs::Address(rfq.receiptAddress)
         , [lotSize, quote, &amountValid](uint64_t value, uint64_t valReturn, uint64_t valInput) {
         if ((quote.side == bs::network::Side::Sell) && qFuzzyCompare(quote.quantity, value / lotSize)) {
            amountValid = valInput == (value + valReturn);
         }
         else if (quote.side == bs::network::Side::Buy) {
            const auto quoteVal = static_cast<uint64_t>(quote.quantity * quote.price * BTCNumericTypes::BalanceDivider);
            const auto diff = (quoteVal > value) ? quoteVal - value : value - quoteVal;
            if (diff < 3) {
               amountValid = valInput > (value + valReturn);
            }
         }
      });
   }
   catch (const std::exception &e) {
      logger_->debug("Signer deser exc: {}", e.what());
      ui_->labelHint->setText(tr("Failed to verify dealer's TX: %1").arg(QLatin1String(e.what())));
   }

   ui_->labelPayment->setText((foundRecipAddr && amountValid) ? sValid : sInvalid);

   if (genAddr.isNull()) {
      emit genAddrVerified(false);
   }
   else if (rfq.side == bs::network::Side::Buy) {
      ui_->labelHint->setText(tr("Waiting for genesis address verification to complete..."));
      ui_->labelGenesisAddress->setText(tr("Verifying"));

      const auto &cbHasInput = [this](bool has) {
         QMetaObject::invokeMethod(this, [this, has] { emit genAddrVerified(has); });
      };
      signer_.hasInputAddress(genAddr, cbHasInput, lotSize);
   }
   else {
      emit genAddrVerified(true);
   }

   if (!createCCUnsignedTXdata()) {
      ui_->labelHint->setText(tr("Failed to create unsigned CC transaction"));
      userKeyOk_ = false;
   }
}

void CCSettlementTransactionWidget::onGenAddrVerified(bool result)
{
   userKeyOk_ = result;
   logger_->debug("[CCSettlementTransactionWidget::onGenAddrVerified] result = {}", result);
   if (!result) {
      transactionData_->SetSigningWallet(nullptr);
      ui_->labelHint->setText(tr("Failed to verify genesis address"));
   } else {
      ui_->labelHint->setText(tr("Accept offer to send your own signed half of the CoinJoin transaction"));
      initSigning();
   }
   ui_->labelGenesisAddress->setText(result ? sValid : sInvalid);
}

void CCSettlementTransactionWidget::initSigning()
{
   if (encTypes_.empty() || !keyRank_.first) {
      return;
   }
   const auto &rootWallet = walletsManager_->GetHDRootForLeaf(transactionData_->GetSigningWallet()->GetWalletId());
   ui_->widgetSubmitKeys->init(MobileClientRequest::SignWallet, rootWallet->getWalletId()
      , keyRank_, encTypes_, encKeys_, appSettings_);
   ui_->widgetSubmitKeys->setFocus();
   updateAcceptButton();
   QApplication::processEvents();
   adjustSize();
}

void CCSettlementTransactionWidget::onAccept()
{
   timer_.stop();
   ui_->progressBar->setValue(0);
   ui_->labelHint->clear();
   ui_->pushButtonAccept->setEnabled(false);
   ui_->widgetSubmitKeys->setEnabled(false);

   acceptSpotCC();
}

void CCSettlementTransactionWidget::onTXSigned(unsigned int id, BinaryData signedTX, std::string error,
   bool cancelledByUser)
{
   if (ccSignId_ && (ccSignId_ == id)) {
      ccSignId_ = 0;
      if (!error.empty()) {
         logger_->warn("[CCSettlementTransactionWidget::onTXSigned] CC TX sign failure: {}", error);
         ui_->labelHint->setText(tr("own TX half signing failed: %1").arg(QString::fromStdString(error)));
         return;
      }
      ccTxSigned_ = signedTX.toHexStr();
      emit settlementAccepted();
   }
}

void CCSettlementTransactionWidget::acceptSpotCC()
{
   ui_->pushButtonCancel->setEnabled(false);
   if (!createCCSignedTXdata()) {
      emit settlementCancelled();
   }
}

bool CCSettlementTransactionWidget::createCCUnsignedTXdata()
{
   const auto wallet = transactionData_->GetSigningWallet();
   if (!wallet) {
      logger_->error("[CCSettlementTransactionWidget::createCCUnsignedTXdata] failed to get signing wallet");
      return false;
   }

   if (clientSells_) {
      const uint64_t spendVal = amount_ * assetManager_->getCCLotSize(product_);
      logger_->debug("[CCSettlementTransactionWidget::createCCUnsignedTXdata] sell amount={}, spend value = {}", amount_, spendVal);
      ccTxData_.walletId = wallet->GetWalletId();
      ccTxData_.prevStates = { dealerTx_ };
      const auto recipient = bs::Address(dealerAddress_).getRecipient(spendVal);
      if (recipient) {
         ccTxData_.recipients.push_back(recipient);
      }
      else {
         logger_->error("[CCSettlementTransactionWidget::createCCUnsignedTXdata] failed to create recipient from {} and value {}"
            , dealerAddress_, spendVal);
         return false;
      }
      ccTxData_.populateUTXOs = true;
      ccTxData_.inputs = utxoAdapter_->get(reserveId_);
      logger_->debug("[CCSettlementTransactionWidget::createCCUnsignedTXdata] {} CC inputs reserved ({} recipients)"
         , ccTxData_.inputs.size(), ccTxData_.recipients.size());
      QMetaObject::invokeMethod(this, [this] { emit sendOrder(); });
   }
   else {
      const auto &cbFee = [this](float feePerByte) {
         const uint64_t spendVal = amount_ * price_ * BTCNumericTypes::BalanceDivider;
         const auto &cbTxOutList = [this, feePerByte, spendVal] (std::vector<UTXO> utxos) {
            try {
               const auto recipient = bs::Address(dealerAddress_).getRecipient(spendVal);
               if (!recipient) {
                  logger_->error("[CCSettlementTransactionWidget::createCCUnsignedTXdata] invalid recipient: {}", dealerAddress_);
                  return;
               }
               ccTxData_ = transactionData_->CreatePartialTXRequest(spendVal, feePerByte, { recipient }
                  , dealerTx_, utxos);
               logger_->debug("{} inputs in ccTxData", ccTxData_.inputs.size());
               QMetaObject::invokeMethod(this, [this] { emit sendOrder(); });
            }
            catch (const std::exception &e) {
               logger_->error("[CCSettlementTransactionWidget::createCCUnsignedTXdata] Failed to create partial CC TX to {}: {}"
                  , dealerAddress_, e.what());
               ui_->labelHint->setText(tr("Failed to create CC TX half"));
            }
         };
         if (!transactionData_->GetWallet()->getSpendableTxOutList(cbTxOutList, this, spendVal)) {
            logger_->error("[CCSettlementTransactionWidget::createCCUnsignedTXdata] getSpendableTxOutList failed");
         }
      };
      walletsManager_->estimatedFeePerByte(0, cbFee, this);
   }

   signingContainer_->SyncAddresses(transactionData_->createAddresses());
   return true;
}

bool CCSettlementTransactionWidget::createCCSignedTXdata()
{
   if (clientSells_) {
      if (!ccTxData_.isValid()) {
         logger_->error("[CCSettlementTransactionWidget::createCCSignedTXdata] CC TX half wasn't created properly");
         ui_->labelHint->setText(tr("Failed to create TX half"));
         return false;
      }
   }

   ccSignId_ = signingContainer_->SignPartialTXRequest(ccTxData_, false, ui_->widgetSubmitKeys->key());
   logger_->debug("[CCSettlementTransactionWidget::createCCSignedTXdata] {} recipients", ccTxData_.recipients.size());
   return (ccSignId_ > 0);
}

const std::string CCSettlementTransactionWidget::getCCTxData() const
{
   logger_->debug("getCCTxData = {}", ccTxData_.serializeState().toHexStr());
   return ccTxData_.serializeState().toHexStr();
}

const std::string CCSettlementTransactionWidget::getTxSignedData() const
{
   return ccTxSigned_;
}

void CCSettlementTransactionWidget::updateAcceptButton()
{
   ui_->pushButtonAccept->setEnabled(userKeyOk_ && ui_->widgetSubmitKeys->isValid());
}

void CCSettlementTransactionWidget::onHDWalletInfo(unsigned int id, std::vector<bs::wallet::EncryptionType> encTypes
   , std::vector<SecureBinaryData> encKeys, bs::wallet::KeyRank keyRank)
{
   if (!infoReqId_ || (id != infoReqId_)) {
      return;
   }
   encTypes_ = encTypes;
   encKeys_ = encKeys;
   keyRank_ = keyRank;

   if (userKeyOk_) {
      initSigning();
   }
}
