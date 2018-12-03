#include "RFQDealerReply.h"
#include "ui_RFQDealerReply.h"

#include <spdlog/logger.h>

#include <chrono>

#include <QComboBox>
#include <QFileDialog>

#include "ApplicationSettings.h"
#include "AssetManager.h"
#include "AuthAddressManager.h"
#include "CelerSubmitQuoteNotifSequence.h"
#include "CoinControlDialog.h"
#include "CoinControlWidget.h"
#include "CurrencyPair.h"
#include "EnterWalletPassword.h"
#include "FastLock.h"
#include "HDWallet.h"
#include "BSMessageBox.h"
#include "QuoteProvider.h"
#include "SelectedTransactionInputs.h"
#include "SignContainer.h"
#include "TransactionData.h"
#include "TxClasses.h"
#include "UiUtils.h"
#include "UtxoReserveAdapters.h"
#include "WalletsManager.h"
#include "WalletKeysSubmitWidget.h"
#include "UserScriptRunner.h"

using namespace bs::ui;

RFQDealerReply::RFQDealerReply(QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::RFQDealerReply())
   , walletsManager_(nullptr)
   , authAddressManager_(nullptr)
   , assetManager_(nullptr)
   , dealerSellXBT_(false)
   , autoUpdatePrices_(true)
   , aq_(nullptr)
   , aqLoaded_(false)
{
   ui_->setupUi(this);
   initUi();

   connect(ui_->spinBoxBidPx, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, &RFQDealerReply::priceChanged);
   connect(ui_->spinBoxOfferPx, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, &RFQDealerReply::priceChanged);

   ui_->spinBoxBidPx->installEventFilter(this);
   ui_->spinBoxOfferPx->installEventFilter(this);

   connect(ui_->pushButtonSubmit, &QPushButton::clicked, this, &RFQDealerReply::submitButtonClicked);
   connect(ui_->pushButtonPull, &QPushButton::clicked, this, &RFQDealerReply::pullButtonClicked);
   connect(ui_->checkBoxAQ, &ToggleSwitch::stateChanged, this, &RFQDealerReply::aqStateChanged);
   connect(ui_->comboBoxAQScript, SIGNAL(activated(int)), this, SLOT(aqScriptChanged(int)));
   connect(this, &RFQDealerReply::aqScriptLoaded, this, &RFQDealerReply::onAqScriptLoaded);
   connect(ui_->pushButtonAdvanced, &QPushButton::clicked, this, &RFQDealerReply::showCoinControl);

   connect(ui_->comboBoxWallet, SIGNAL(currentIndexChanged(int)), this, SLOT(walletSelected(int)));
   connect(ui_->authenticationAddressComboBox, SIGNAL(currentIndexChanged(int)), SLOT(updateSubmitButton()));

   connect(ui_->checkBoxAutoSign, &ToggleSwitch::clicked, this, &RFQDealerReply::onAutoSignActivated);

   ui_->responseTitle->hide();
}

RFQDealerReply::~RFQDealerReply()
{
   bs::UtxoReservation::delAdapter(utxoAdapter_);
}

void RFQDealerReply::init(const std::shared_ptr<spdlog::logger> logger
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , const std::shared_ptr<AssetManager>& assetManager
   , const std::shared_ptr<QuoteProvider>& quoteProvider
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<ArmoryConnection> &armory
   , std::shared_ptr<MarketDataProvider> mdProvider)
{
   logger_ = logger;
   assetManager_ = assetManager;
   quoteProvider_ = quoteProvider;
   authAddressManager_ = authAddressManager;
   appSettings_ = appSettings;
   signingContainer_ = container;
   armory_ = armory;

   connect(authAddressManager_.get(), &AuthAddressManager::VerifiedAddressListUpdated, [this] {
      UiUtils::fillAuthAddressesComboBox(ui_->authenticationAddressComboBox, authAddressManager_);
   });

   utxoAdapter_ = std::make_shared<bs::DealerUtxoResAdapter>(logger_, nullptr);
   connect(quoteProvider_.get(), &QuoteProvider::orderUpdated, utxoAdapter_.get(), &bs::OrderUtxoResAdapter::onOrder);
   connect(quoteProvider_.get(), &QuoteProvider::orderUpdated, this, &RFQDealerReply::onOrderUpdated);
   connect(utxoAdapter_.get(), &bs::OrderUtxoResAdapter::reservedUtxosChanged, this, &RFQDealerReply::onReservedUtxosChanged, Qt::QueuedConnection);

   aq_ = new UserScriptRunner(quoteProvider_, utxoAdapter_, signingContainer_,
      mdProvider, assetManager_, logger_, this);

   if (walletsManager_) {
      aq_->setWalletsManager(walletsManager_);
   }

   connect(aq_, &UserScriptRunner::aqScriptLoaded, [this] (const QString &fileName) {
      aqLoaded_ = true;
      validateGUI();
      emit aqScriptLoaded(fileName);
   });
   connect(aq_, &UserScriptRunner::failedToLoad, [this](const QString &err) {
      logger_->error("Script loading failed: {}", err.toStdString());

      auto scripts = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
      scripts.removeAt(ui_->comboBoxAQScript->currentIndex() - 1);
      appSettings_->set(ApplicationSettings::aqScripts, scripts);
      appSettings_->reset(ApplicationSettings::lastAqScript);
      aqFillHistory();

      aqLoaded_ = false;
      validateGUI();
   });
   connect(aq_, &UserScriptRunner::sendQuote, this, &RFQDealerReply::onAQReply,
      Qt::QueuedConnection);
   connect(aq_, &UserScriptRunner::pullQuoteNotif, this, &RFQDealerReply::pullQuoteNotif,
      Qt::QueuedConnection);

   if (signingContainer_) {
      connect(signingContainer_.get(), &SignContainer::HDLeafCreated, this, &RFQDealerReply::onHDLeafCreated);
      connect(signingContainer_.get(), &SignContainer::Error, this, &RFQDealerReply::onCreateHDWalletError);
      connect(signingContainer_.get(), &SignContainer::ready, this, &RFQDealerReply::onSignerStateUpdated, Qt::QueuedConnection);
      connect(signingContainer_.get(), &SignContainer::disconnected, this, &RFQDealerReply::onSignerStateUpdated, Qt::QueuedConnection);
      connect(signingContainer_.get(), &SignContainer::AutoSignStateChanged, this, &RFQDealerReply::onAutoSignStateChanged);
      connect(signingContainer_.get(), &SignContainer::HDWalletInfo, this, &RFQDealerReply::onHDWalletInfo);
   }

   UtxoReservation::addAdapter(utxoAdapter_);

   aqFillHistory();

   onSignerStateUpdated();
}

void RFQDealerReply::initUi()
{
   ui_->labelRecvAddr->hide();
   ui_->comboBoxRecvAddr->hide();
   ui_->authenticationAddressLabel->hide();
   ui_->authenticationAddressComboBox->hide();
   ui_->pushButtonSubmit->setEnabled(false);
   ui_->pushButtonPull->setEnabled(false);
   ui_->widgetWallet->hide();

   ui_->spinBoxBidPx->clear();
   ui_->spinBoxOfferPx->clear();
   ui_->spinBoxBidPx->setEnabled(false);
   ui_->spinBoxOfferPx->setEnabled(false);

   ui_->labelProductGroup->clear();

   ui_->checkBoxAQ->setEnabled(false);

   validateGUI();
}

void RFQDealerReply::setWalletsManager(const std::shared_ptr<WalletsManager> &walletsManager)
{
   walletsManager_ = walletsManager;
   UiUtils::fillHDWalletsComboBox(ui_->comboBoxWalletAS, walletsManager_);
   updateAutoSignState();

   if (aq_) {
      aq_->setWalletsManager(walletsManager_);
   }
}

bool RFQDealerReply::autoSign() const
{
   return ui_->checkBoxAutoSign->isChecked();
}

CustomDoubleSpinBox* RFQDealerReply::bidSpinBox() const
{
   return ui_->spinBoxBidPx;
}

CustomDoubleSpinBox* RFQDealerReply::offerSpinBox() const
{
   return ui_->spinBoxOfferPx;
}

QPushButton* RFQDealerReply::pullButton() const
{
   return ui_->pushButtonPull;
}

QPushButton* RFQDealerReply::quoteButton() const
{
   return ui_->pushButtonSubmit;
}

void RFQDealerReply::onSignerStateUpdated()
{
   ui_->groupBoxAutoSign->setVisible(signingContainer_ && !signingContainer_->isOffline());
   disableAutoSign();
}

void RFQDealerReply::onAutoSignActivated()
{
   if (ui_->checkBoxAutoSign->isChecked()) {
      tryEnableAutoSign();
   } else {
      disableAutoSign();
   }
}

void RFQDealerReply::onHDWalletInfo(unsigned int id
   , std::vector<bs::wallet::EncryptionType> encTypes, std::vector<SecureBinaryData> encKeys
   , bs::wallet::KeyRank keyRank)
{
   if (autoSignWalletInfoReqId_ != id) {
      return;
   }

   EnterWalletPassword passwordDialog(MobileClientRequest::SettlementTransaction, this);
   passwordDialog.init(autoSignWalletId_, keyRank
      , encTypes, encKeys, appSettings_
      , tr("Activate Auto-Sign"));
   passwordDialog.setWindowTitle(tr("Activate Auto-Sign"));
   if (passwordDialog.exec() != QDialog::Accepted) {
      disableAutoSign();
      return;
   }

   ui_->checkBoxAutoSign->setChecked(true);
   auto password = passwordDialog.getPassword();
   emit autoSignActivated(password, QString::fromStdString(autoSignWalletId_), true);
   updateAutoSignState();
}

bs::Address RFQDealerReply::getRecvAddress() const
{
   if (!curWallet_) {
      logger_->warn("[RFQDealerReply::getRecvAddress] no current wallet");
      return {};
   }

   const auto index = ui_->comboBoxRecvAddr->currentIndex();
   logger_->debug("[RFQDealerReply::getRecvAddress] obtaining addr #{} from wallet {}", index, curWallet_->GetWalletName());
   if (index <= 0) {
      const auto recvAddr = curWallet_->GetNewIntAddress();
      if (transactionData_) {
         transactionData_->createAddress(recvAddr, curWallet_);
      }
      if (curWallet_->GetType() != bs::wallet::Type::ColorCoin) {
         curWallet_->SetAddressComment(recvAddr, bs::wallet::Comment::toString(bs::wallet::Comment::SettlementPayOut));
      }
      curWallet_->RegisterWallet();
      return recvAddr;
   }
   return curWallet_->GetExtAddressList()[index - 1];
}

void RFQDealerReply::updateRecvAddresses()
{
   if (prevWallet_ == curWallet_) {
      return;
   }

   ui_->comboBoxRecvAddr->clear();
   ui_->comboBoxRecvAddr->addItem(tr("Auto Create"));
   if (curWallet_ != nullptr) {
      for (const auto &addr : curWallet_->GetExtAddressList()) {
         ui_->comboBoxRecvAddr->addItem(addr.display());
      }
   }
}

void RFQDealerReply::updateRespQuantity()
{
   if (currentQRN_.empty()) {
      ui_->labelRespQty->clear();
      return;
   }

   if (product_ == bs::network::XbtCurrency) {
      ui_->labelRespQty->setText(UiUtils::displayAmount(getValue()));
   } else {
      ui_->labelRespQty->setText(UiUtils::displayCurrencyAmount(getValue()));
   }
}

void RFQDealerReply::reset()
{
   payInRecipId_ = UINT_MAX;
   if (currentQRN_.empty()) {
      ui_->labelProductGroup->clear();
      ui_->labelSecurity->clear();
      ui_->labelReqProduct->clear();
      ui_->labelReqSide->clear();
      ui_->labelReqQty->clear();
      ui_->labelRespProduct->clear();
      indicBid_ = indicAsk_ = 0;
      setBalanceOk(true);
   }
   else {
      CurrencyPair cp(currentQRN_.security);
      baseProduct_ = cp.NumCurrency();
      product_ = cp.ContraCurrency(currentQRN_.product);

      transactionData_ = nullptr;
      if (currentQRN_.assetType != bs::network::Asset::SpotFX) {
         transactionData_ = std::make_shared<TransactionData>([this]() { onTransactionDataChanged(); }
            , true, true);
         if (walletsManager_ != nullptr) {
            const auto &cbFee = [this](float feePerByte) {
               transactionData_->SetFeePerByte(feePerByte);
            };
            walletsManager_->estimatedFeePerByte(2, cbFee, this);
         }
         if (currentQRN_.assetType == bs::network::Asset::SpotXBT) {
            transactionData_->SetWallet(curWallet_, armory_->topBlock());
         }
         else if (currentQRN_.assetType == bs::network::Asset::PrivateMarket) {
            std::shared_ptr<bs::Wallet> wallet, xbtWallet = getXbtWallet();
            if (currentQRN_.side == bs::network::Side::Buy) {
               wallet = getCCWallet(currentQRN_.product);
            }
            else {
               wallet = xbtWallet;
            }
            if (wallet && (!ccCoinSel_ || (ccCoinSel_->GetWallet() != wallet))) {
               ccCoinSel_ = std::make_shared<SelectedTransactionInputs>(wallet, true, true);
            }
            transactionData_->SetSigningWallet(wallet);
            transactionData_->SetWallet(xbtWallet, armory_->topBlock());
         }
      }

      const auto assetType = assetManager_->GetAssetTypeForSecurity(currentQRN_.security);
      if (assetType == bs::network::Asset::Type::Undefined) {
         logger_->error("[RFQDealerReply::reset] could not get asset type for {}", currentQRN_.security);
      }
      const auto priceDecimals = UiUtils::GetPricePrecisionForAssetType(assetType);
      ui_->spinBoxBidPx->setDecimals(priceDecimals);
      ui_->spinBoxOfferPx->setDecimals(priceDecimals);
      ui_->spinBoxBidPx->setSingleStep(std::pow(10, -priceDecimals));
      ui_->spinBoxOfferPx->setSingleStep(std::pow(10, -priceDecimals));

      const auto priceWidget = getActivePriceWidget();
      ui_->spinBoxBidPx->setEnabled(priceWidget == ui_->spinBoxBidPx);
      ui_->spinBoxOfferPx->setEnabled(priceWidget == ui_->spinBoxOfferPx);
/*      priceWidget->setFocus();
      priceWidget->selectAll();*/

      ui_->labelProductGroup->setText(tr(bs::network::Asset::toString(currentQRN_.assetType)));
      ui_->labelSecurity->setText(QString::fromStdString(currentQRN_.security));
      ui_->labelReqProduct->setText(QString::fromStdString(currentQRN_.product));
      ui_->labelReqSide->setText(tr(bs::network::Side::toString(currentQRN_.side)));
      ui_->labelReqQty->setText(UiUtils::displayAmountForProduct(currentQRN_.quantity
         , QString::fromStdString(currentQRN_.product), currentQRN_.assetType));

      ui_->labelRespProduct->setText(QString::fromStdString(product_));
   }

   updateRespQuantity();
   updateRecvAddresses();

   if (!qFuzzyIsNull(indicBid_)) {
      ui_->spinBoxBidPx->setValue(indicBid_);
   }
   else {
      ui_->spinBoxBidPx->clear();
   }

   if (!qFuzzyIsNull(indicAsk_)) {
      ui_->spinBoxOfferPx->setValue(indicAsk_);
   }
   else {
      ui_->spinBoxOfferPx->clear();
   }
}

void RFQDealerReply::quoteReqNotifStatusChanged(const bs::network::QuoteReqNotification &qrn)
{
   if (!QuoteProvider::isRepliableStatus(qrn.status)) {
      sentNotifs_.erase(qrn.quoteRequestId);
   }

   if (qrn.quoteRequestId == currentQRN_.quoteRequestId) {
      updateQuoteReqNotification(qrn);
   }
}

void RFQDealerReply::setQuoteReqNotification(const bs::network::QuoteReqNotification &qrn, double indicBid, double indicAsk)
{
   indicBid_ = indicBid;
   indicAsk_ = indicAsk;

   updateQuoteReqNotification(qrn);
}

void RFQDealerReply::updateQuoteReqNotification(const bs::network::QuoteReqNotification &qrn)
{
   const auto &oldReqId = currentQRN_.quoteRequestId;
   const bool qrnChanged = (oldReqId != qrn.quoteRequestId);
   currentQRN_ = qrn;

   const bool isXBT = (qrn.assetType == bs::network::Asset::SpotXBT);
   const bool isPrivMkt = (qrn.assetType == bs::network::Asset::PrivateMarket);

   ui_->authenticationAddressLabel->setVisible(isXBT);
   ui_->authenticationAddressComboBox->setVisible(isXBT);
   ui_->widgetWallet->setVisible(isXBT || isPrivMkt);
   ui_->pushButtonAdvanced->setVisible(isXBT && (qrn.side == bs::network::Side::Buy));
   ui_->labelRecvAddr->setVisible(isXBT || isPrivMkt);
   ui_->comboBoxRecvAddr->setVisible(isXBT || isPrivMkt);

   dealerSellXBT_ = (isXBT || isPrivMkt) && ((qrn.product == bs::network::XbtCurrency) ^ (qrn.side == bs::network::Side::Sell));

   updateUiWalletFor(qrn);

   if (qrnChanged) {
      reset();
   }

   if (qrn.assetType == bs::network::Asset::SpotFX ||
      qrn.assetType == bs::network::Asset::Undefined) {
         ui_->responseTitle->hide();
   } else {
      ui_->responseTitle->show();
   }

   updateSubmitButton();
}

std::shared_ptr<bs::Wallet> RFQDealerReply::getCCWallet(const std::string &cc)
{
   if (ccWallet_ && (ccWallet_->GetShortName() == cc)) {
      return ccWallet_;
   }
   if (walletsManager_) {
      ccWallet_ = walletsManager_->GetCCWallet(cc);
   }
   else {
      ccWallet_ = nullptr;
   }
   return ccWallet_;
}

std::shared_ptr<bs::Wallet> RFQDealerReply::getXbtWallet()
{
   if (!xbtWallet_ && walletsManager_) {
      xbtWallet_ = walletsManager_->GetDefaultWallet();
   }
   return xbtWallet_;
}

void RFQDealerReply::updateUiWalletFor(const bs::network::QuoteReqNotification &qrn)
{
   if (armory_->state() != ArmoryConnection::State::Ready) {
      return;
   }
   if (qrn.assetType == bs::network::Asset::PrivateMarket) {
      if (qrn.side == bs::network::Side::Sell) {
         const auto &ccWallet = getCCWallet(qrn.product);
         if (!ccWallet) {
            if (leafCreateReqId_) {
               return;
            }

            if (signingContainer_ && !signingContainer_->isOffline()) {
               MessageBoxCCWalletQuestion qryCCWallet(QString::fromStdString(qrn.product), this);

               if (qryCCWallet.exec() == QDialog::Accepted) {
                  bs::hd::Path path;
                  path.append(bs::hd::purpose, true);
                  path.append(bs::hd::BlockSettle_CC, true);
                  path.append(qrn.product, true);
                  leafCreateReqId_ = signingContainer_->CreateHDLeaf(walletsManager_->GetPrimaryWallet(), path);
               }
            } else {
               BSMessageBox errorMessage(BSMessageBox::critical, tr("Signer not connected")
                  , tr("Could not create CC subwallet.")
                  , this);
               errorMessage.exec();
            }
         } else if (ccWallet != curWallet_) {
            ui_->comboBoxWallet->clear();
            ui_->comboBoxWallet->addItem(QString::fromStdString(ccWallet->GetWalletName()));
            ui_->comboBoxWallet->setItemData(0, QString::fromStdString(ccWallet->GetWalletId()), UiUtils::WalletIdRole);
            setCurrentWallet(ccWallet);
         }
      }
      else {
         if (!curWallet_ || (ccWallet_ && (ccWallet_ == curWallet_))) {
            walletSelected(UiUtils::fillWalletsComboBox(ui_->comboBoxWallet, walletsManager_, signingContainer_));
         }
      }
   }
   else if (qrn.assetType == bs::network::Asset::SpotXBT) {
      walletSelected(UiUtils::fillWalletsComboBox(ui_->comboBoxWallet, walletsManager_, signingContainer_));
   }
}

void RFQDealerReply::priceChanged()
{
   updateRespQuantity();
   updateSubmitButton();
}

void RFQDealerReply::updateSubmitButton()
{
   bool isQRNRepliable = (!currentQRN_.empty() && QuoteProvider::isRepliableStatus(currentQRN_.status));
   if ((currentQRN_.assetType != bs::network::Asset::SpotFX)
      && (!signingContainer_ || signingContainer_->isOffline())) {
      isQRNRepliable = false;
   }
   ui_->pushButtonSubmit->setEnabled(isQRNRepliable);
   ui_->pushButtonPull->setEnabled(isQRNRepliable);
   if (!isQRNRepliable) {
      ui_->spinBoxBidPx->setEnabled(false);
      ui_->spinBoxOfferPx->setEnabled(false);
      return;
   }

   const auto itQN = sentNotifs_.find(currentQRN_.quoteRequestId);
   ui_->pushButtonPull->setEnabled(itQN != sentNotifs_.end());

   const auto price = getPrice();
   if (qFuzzyIsNull(price) || ((itQN != sentNotifs_.end()) && qFuzzyCompare(itQN->second, price))) {
      ui_->pushButtonSubmit->setEnabled(false);
      return;
   }

   if ((currentQRN_.assetType == bs::network::Asset::SpotXBT)
      && (ui_->authenticationAddressComboBox->currentIndex() == -1)) {
      ui_->pushButtonSubmit->setEnabled(false);
      return;
   }

   if (!assetManager_) {
      ui_->pushButtonSubmit->setEnabled(false);
      return;
   }

   const bool isBalanceOk = checkBalance();
   ui_->pushButtonSubmit->setEnabled(isBalanceOk);
   setBalanceOk(isBalanceOk);
}

void RFQDealerReply::setBalanceOk(bool ok)
{
   if (!ok) {
      QPalette palette = ui_->labelRespQty->palette();
      palette.setColor(QPalette::WindowText, Qt::red);
      ui_->labelRespQty->setPalette(palette);
      return;
   }
   ui_->labelRespQty->setPalette(QPalette());
}

bool RFQDealerReply::checkBalance() const
{
   if (!assetManager_) {
      return false;
   }

   if ((currentQRN_.side == bs::network::Side::Buy) ^ (product_ == baseProduct_)) {
      const auto amount = getAmount();
      if ((currentQRN_.assetType == bs::network::Asset::SpotXBT) && transactionData_) {
         return (amount <= (transactionData_->GetTransactionSummary().availableBalance
            - transactionData_->GetTransactionSummary().totalFee / BTCNumericTypes::BalanceDivider));
      }
      else if ((currentQRN_.assetType == bs::network::Asset::PrivateMarket) && ccCoinSel_) {
         uint64_t balance = 0;
         for (const auto &utxo : utxoAdapter_->get(currentQRN_.quoteRequestId)) {
            balance += utxo.getValue();
         }
         if (!balance) {
            balance = ccCoinSel_->GetBalance();
         }
         return (amount <= ccCoinSel_->GetWallet()->GetTxBalance(balance));
      }
      const auto product = (product_ == baseProduct_) ? product_ : currentQRN_.product;
      return assetManager_->checkBalance(product, amount);
   }
   else if ((currentQRN_.side == bs::network::Side::Buy) && (product_ == baseProduct_)) {
      return assetManager_->checkBalance(currentQRN_.product, currentQRN_.quantity);
   }

   if ((currentQRN_.assetType == bs::network::Asset::PrivateMarket) && transactionData_) {
      return (currentQRN_.quantity * getPrice() <= transactionData_->GetTransactionSummary().availableBalance
         - transactionData_->GetTransactionSummary().totalFee / BTCNumericTypes::BalanceDivider);
   }

   const double value = getValue();
   if (qFuzzyIsNull(value)) {
      return true;
   }
   const bool isXbt = (currentQRN_.assetType == bs::network::Asset::PrivateMarket) ||
      ((currentQRN_.assetType == bs::network::Asset::SpotXBT) && (product_ == baseProduct_));
   if (isXbt && transactionData_) {
      return (value <= (transactionData_->GetTransactionSummary().availableBalance
         - transactionData_->GetTransactionSummary().totalFee / BTCNumericTypes::BalanceDivider));
   }
   return assetManager_->checkBalance(product_, value);
}

void RFQDealerReply::setCurrentWallet(const std::shared_ptr<bs::Wallet> &newWallet)
{
   prevWallet_ = curWallet_;
   curWallet_ = newWallet;

   if (newWallet != ccWallet_) {
      xbtWallet_ = newWallet;
   }

   if (newWallet != nullptr) {
      if (transactionData_ != nullptr) {
         transactionData_->SetWallet(newWallet, armory_->topBlock());
      }
   }
}

void RFQDealerReply::walletSelected(int index)
{
   if (walletsManager_) {
      setCurrentWallet(walletsManager_->GetWalletById(ui_->comboBoxWallet->currentData(UiUtils::WalletIdRole).toString().toStdString()));
   }

   updateRecvAddresses();
   updateSubmitButton();
}

QDoubleSpinBox *RFQDealerReply::getActivePriceWidget() const
{
   if (currentQRN_.empty()) {
      return nullptr;
   }

   if (baseProduct_ == currentQRN_.product) {
      if (currentQRN_.side == bs::network::Side::Buy) {
         return ui_->spinBoxOfferPx;
      }
      return ui_->spinBoxBidPx;
   }
   else {
      if (currentQRN_.side == bs::network::Side::Buy) {
         return ui_->spinBoxBidPx;
      }
      return ui_->spinBoxOfferPx;
   }
}

double RFQDealerReply::getPrice() const
{
   const auto spinBox = getActivePriceWidget();
   return spinBox ? spinBox->value() : 0;
}

double RFQDealerReply::getValue() const
{
   const double price = getPrice();
   if (baseProduct_ == product_) {
      if (!qFuzzyIsNull(price)) {
         return  currentQRN_.quantity / price;
      }
      return 0;
   }
   return price * currentQRN_.quantity;
}

double RFQDealerReply::getAmount() const
{
   if (baseProduct_ == product_) {
      const double price = getPrice();
      if (!qFuzzyIsNull(price)) {
         return  currentQRN_.quantity / price;
      }
      return 0;
   }
   return currentQRN_.quantity;
}

bool RFQDealerReply::submitReply(const std::shared_ptr<TransactionData> transData
   , const bs::network::QuoteReqNotification &qrn, double price
   , std::function<void(bs::network::QuoteNotification)> cb)
{
   if (qFuzzyIsNull(price)) {
      return false;
   }
   const auto itQN = sentNotifs_.find(qrn.quoteRequestId);
   if ((itQN != sentNotifs_.end()) && (itQN->second == price)) {
      return false;
   }
   std::string authKey, txData;
   bool isBid = (qrn.side == bs::network::Side::Buy);

   if ((qrn.assetType == bs::network::Asset::SpotXBT) && authAddressManager_ && transData) {
      authKey = authAddressManager_->GetPublicKey(authAddressManager_->FromVerifiedIndex(ui_->authenticationAddressComboBox->currentIndex())).toHexStr();
      if (authKey.empty()) {
         logger_->error("[RFQDealerReply::submit] empty auth key");
         return false;
      }
      logger_->debug("[RFQDealerReply::submit] using wallet {}", transData->GetWallet()->GetWalletName());

      const bool reversed = (qrn.product != bs::network::XbtCurrency);
      if (reversed) {
         isBid = !isBid;
      }
      const double quantity = reversed ? qrn.quantity / price : qrn.quantity;

      if (isBid) {
         if ((payInRecipId_ == UINT_MAX) || !transData->GetRecipientsCount()) {
            const std::string &comment = std::string(bs::network::Side::toString(bs::network::Side::invert(qrn.side)))
               + " " + qrn.security + " @ " + std::to_string(price);
            const auto settlAddr = walletsManager_->GetSettlementWallet()->newAddress(
               BinaryData::CreateFromHex(qrn.settlementId), BinaryData::CreateFromHex(qrn.requestorAuthPublicKey),
               BinaryData::CreateFromHex(authKey), comment);

            payInRecipId_ = transData->RegisterNewRecipient();
            transData->UpdateRecipientAmount(payInRecipId_, quantity);
            if (!transData->UpdateRecipientAddress(payInRecipId_, settlAddr)) {
               logger_->warn("[RFQDealerReply::submit] Failed to update address for recipient {}", payInRecipId_);
            }
         }
         else {
            transData->UpdateRecipientAmount(payInRecipId_, quantity);
         }

         try {
            if (transData->IsTransactionValid()) {
               bs::wallet::TXSignRequest unsignedTxReq;
               if (transData->GetTransactionSummary().hasChange) {
                  const auto changeAddr = transData->GetWallet()->GetNewChangeAddress();
                  transData->createAddress(changeAddr);
                  unsignedTxReq = transData->CreateUnsignedTransaction(false, changeAddr);
                  transData->GetWallet()->SetAddressComment(changeAddr, bs::wallet::Comment::toString(bs::wallet::Comment::ChangeAddress));
               }
               else {
                  unsignedTxReq = transData->CreateUnsignedTransaction();
               }
               quoteProvider_->saveDealerPayin(qrn.settlementId, unsignedTxReq.serializeState());
               txData = unsignedTxReq.txId().toHexStr();

               utxoAdapter_->reserve(unsignedTxReq, qrn.settlementId);
            }
            else {
               logger_->warn("[RFQDealerReply::submit] pay-in transaction is invalid!");
            }
         }
         catch (const std::exception &e) {
            logger_->error("[RFQDealerReply::submit] failed to create pay-in transaction: {}", e.what());
            return false;
         }
      }
      else {
         transData->SetFallbackRecvAddress(getRecvAddress());
      }
   }

   auto qn = new bs::network::QuoteNotification(qrn, authKey, price, txData);

   if (qrn.assetType == bs::network::Asset::PrivateMarket) {
      qn->receiptAddress = getRecvAddress().display<std::string>();
      qn->reqAuthKey = qrn.requestorRecvAddress;

      std::shared_ptr<bs::Wallet> wallet = transData->GetSigningWallet();
      auto spendVal = new uint64_t;
      *spendVal = 0;

      const auto &cbFee = [this, qrn, transData, spendVal, wallet, cb, qn](float feePerByte) {
         const auto recipient = bs::Address(qrn.requestorRecvAddress).getRecipient(*spendVal);
         std::vector<UTXO> inputs = utxoAdapter_->get(qn->quoteRequestId);
         if (inputs.empty() && ccCoinSel_) {
            inputs = ccCoinSel_->GetSelectedTransactions();
            if (inputs.empty()) {
               logger_->error("[RFQDealerReply::submit] no suitable inputs for CC sell");
               cb({});
               delete spendVal;
               return;
            }
         }
         try {
            const auto changeAddr = wallet->GetNewChangeAddress();
            transData->createAddress(changeAddr, wallet);
            const auto txReq = wallet->CreatePartialTXRequest(*spendVal, inputs, changeAddr, feePerByte
               , { recipient }, BinaryData::CreateFromHex(qrn.requestorAuthPublicKey));
            qn->transactionData = txReq.serializeState().toHexStr();
            utxoAdapter_->reserve(txReq, qn->quoteRequestId);
            delete spendVal;
         }
         catch (const std::exception &e) {
            delete spendVal;
            logger_->error("[RFQDealerReply::submit] error creating own unsigned half: {}", e.what());
            cb({});
            return;
         }
         if (!transData->GetSigningWallet()) {
            transData->SetSigningWallet(wallet);
         }
         cb(*qn);
         delete qn;
      };

      if (qrn.side == bs::network::Side::Buy) {
         if (!wallet) {
            wallet = getCCWallet(qrn.product);
         }
         *spendVal = qrn.quantity * assetManager_->getCCLotSize(qrn.product);
         cbFee(0);
         return true;
      }
      else {
         if (!wallet) {
            wallet = getXbtWallet();
         }
         *spendVal = qrn.quantity * price * BTCNumericTypes::BalanceDivider;
         walletsManager_->estimatedFeePerByte(2, cbFee, this);
         return true;
      }
   }

   cb(*qn);
   delete qn;
   return true;
}

void RFQDealerReply::tryEnableAutoSign()
{
   ui_->checkBoxAQ->setChecked(false);

   if (!walletsManager_ || !signingContainer_) {
      return;
   }

   auto walletId = ui_->comboBoxWalletAS->currentData(UiUtils::WalletIdRole).toString().toStdString();

   const auto wallet = walletsManager_->GetHDWalletById(walletId);
   if (!wallet) {
      logger_->error("Failed to obtain auto-sign wallet for id {}", walletId);
      return;
   }

   autoSignWalletInfoReqId_ = signingContainer_->GetInfo(wallet);
   autoSignWalletId_ = walletId;
}

void RFQDealerReply::disableAutoSign()
{
   auto walletId = ui_->comboBoxWalletAS->currentData(UiUtils::WalletIdRole).toString();
   ui_->checkBoxAutoSign->setChecked(false);
   emit autoSignActivated({}, walletId, false);
   updateAutoSignState();
}

void RFQDealerReply::updateAutoSignState()
{
   ui_->checkBoxAutoSign->setEnabled(ui_->comboBoxWalletAS->count() > 0);
   ui_->comboBoxWalletAS->setEnabled(!ui_->checkBoxAutoSign->isChecked());
}

void RFQDealerReply::onReservedUtxosChanged(const std::string &walletId, const std::vector<UTXO> &utxos)
{
   if (ccCoinSel_ && (ccCoinSel_->GetWallet()->GetWalletId() == walletId)) {
      ccCoinSel_->Reload(utxos);
   }
   if (transactionData_ && transactionData_->GetWallet() && (transactionData_->GetWallet()->GetWalletId() == walletId)) {
      transactionData_->ReloadSelection(utxos);
   }
   onTransactionDataChanged();
}

void RFQDealerReply::submitButtonClicked()
{
   const auto price = getPrice();
   if (!ui_->pushButtonSubmit->isEnabled() || qFuzzyIsNull(price)) {
      return;
   }

   const auto &cbSubmit = [this, price](bs::network::QuoteNotification qn) {
      if (!qn.quoteRequestId.empty()) {
         logger_->debug("Submitted quote reply on {}: {}/{}", currentQRN_.quoteRequestId, qn.bidPx, qn.offerPx);
         sentNotifs_[qn.quoteRequestId] = price;
         QMetaObject::invokeMethod(this, [this, qn] { emit submitQuoteNotif(qn); });
      }
   };
   submitReply(transactionData_, currentQRN_, price, cbSubmit);
   updateSubmitButton();
}

void RFQDealerReply::pullButtonClicked()
{
   if (currentQRN_.empty()) {
      return;
   }
   emit pullQuoteNotif(QString::fromStdString(currentQRN_.quoteRequestId), QString::fromStdString(currentQRN_.sessionToken));
   sentNotifs_.erase(currentQRN_.quoteRequestId);
   updateSubmitButton();
}

bool RFQDealerReply::eventFilter(QObject *watched, QEvent *evt)
{
   if (evt->type() == QEvent::KeyPress) {
      auto keyID = static_cast<QKeyEvent *>(evt)->key();
      if ((keyID == Qt::Key_Return) || (keyID == Qt::Key_Enter)) {
         submitButtonClicked();
      }
   } else if ((evt->type() == QEvent::FocusIn) || (evt->type() == QEvent::FocusOut)) {
      auto activePriceWidget = getActivePriceWidget();
      if (activePriceWidget != nullptr) {
         autoUpdatePrices_ = !(activePriceWidget->hasFocus());
      } else {
         autoUpdatePrices_ = false;
      }
   }
   return QWidget::eventFilter(watched, evt);
}

void RFQDealerReply::showCoinControl()
{
   if (currentQRN_.assetType == bs::network::Asset::PrivateMarket) {
      CoinControlDialog(ccCoinSel_, this).exec();
   }
   else {
      CoinControlDialog(transactionData_->GetSelectedInputs(), this).exec();
   }
}

std::shared_ptr<TransactionData> RFQDealerReply::getTransactionData(const std::string &reqId) const
{
   if ((reqId == currentQRN_.quoteRequestId) && transactionData_) {
      return transactionData_;
   }

   if (aq_) {
      return aq_->getTransactionData(reqId);
   } else {
      return nullptr;
   }
}

void RFQDealerReply::validateGUI()
{
   updateSubmitButton();
   if (!aqLoaded_) {
      ui_->checkBoxAQ->setChecked(false);
   }

   ui_->pushButtonAQScript->setEnabled(!ui_->checkBoxAQ->isChecked());
}

void RFQDealerReply::onTransactionDataChanged()
{
   QMetaObject::invokeMethod(this, "updateSubmitButton");
}

void RFQDealerReply::initAQ(const QString &filename)
{
   if (filename.isEmpty()) {
      return;
   }
   aqLoaded_ = false;

   aq_->initAQ(filename);
}

void RFQDealerReply::aqFillHistory()
{
   if (!appSettings_) {
      return;
   }
   ui_->comboBoxAQScript->clear();
   int curIndex = -1;
   ui_->comboBoxAQScript->addItem(tr("Load new AQ script"));
   const auto scripts = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
   if (!scripts.isEmpty()) {
      const auto lastScript = appSettings_->get<QString>(ApplicationSettings::lastAqScript);
      for (int i = 0; i < scripts.size(); i++) {
         QFileInfo fi(scripts[i]);
         ui_->comboBoxAQScript->addItem(fi.fileName());

         if (scripts[i] == lastScript) {
            curIndex = i + 1; // note the "Load" row in the head
         }
      }
   }
   ui_->comboBoxAQScript->setCurrentIndex(curIndex);
}

void RFQDealerReply::aqScriptChanged(int curIndex)
{
   if (curIndex < 0) {
      return;
   }
   if (curIndex == 0) {
      const auto scriptFN = QFileDialog::getOpenFileName(this, tr("Open Auto-quoting script file"), QString()
         , tr("QML files (*.qml)"));
      if (scriptFN.isEmpty()) {
         aqFillHistory();
         return;
      }
      else {
         initAQ(scriptFN);
      }
   }
   else {
      // enable toggleswitch if a script is selected
      // celer is connected
      if (celerConnected_) {
         ui_->checkBoxAQ->setEnabled(true);
      }
   }
}

void RFQDealerReply::onAqScriptLoaded(const QString &filename)
{
   auto scripts = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
   if (scripts.indexOf(filename) < 0) {
      scripts << filename;
      appSettings_->set(ApplicationSettings::aqScripts, scripts);
   }
   appSettings_->set(ApplicationSettings::lastAqScript, filename);
   aqFillHistory();
}

void RFQDealerReply::aqStateChanged(int state)
{
   if (state == Qt::Unchecked) {
      aq_->disableAQ();
   }
   else {
      const auto scripts = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
      int index = ui_->comboBoxAQScript->currentIndex() - 1;
      if (index >= 0 && index < scripts.size()) {
         initAQ(scripts[index]);
      }
   }
}

void RFQDealerReply::onOrderUpdated(const bs::network::Order &order)
{
   if ((order.assetType == bs::network::Asset::PrivateMarket) && (order.status == bs::network::Order::Failed)) {
      const auto &quoteReqId = quoteProvider_->getQuoteReqId(order.quoteId);
      if (!quoteReqId.empty()) {
         utxoAdapter_->unreserve(quoteReqId);
      }
   }
}

void RFQDealerReply::onMDUpdate(bs::network::Asset::Type, const QString &security, bs::network::MDFields mdFields)
{
   const double bid = bs::network::MDField::get(mdFields, bs::network::MDField::PriceBid).value;
   const double ask = bs::network::MDField::get(mdFields, bs::network::MDField::PriceOffer).value;
   const double last = bs::network::MDField::get(mdFields, bs::network::MDField::PriceLast).value;

   auto &mdInfo = mdInfo_[security.toStdString()];
   if (bid > 0) {
      mdInfo.bidPrice = bid;
   }
   if (ask > 0) {
      mdInfo.askPrice = ask;
   }
   if (last > 0) {
      mdInfo.lastPrice = last;
   }

   if (autoUpdatePrices_ && (currentQRN_.security == security.toStdString())
      && (bestQPrices_.find(currentQRN_.quoteRequestId) == bestQPrices_.end())) {
      if (!qFuzzyIsNull(bid)) {
         ui_->spinBoxBidPx->setValue(bid);
      }
      if (!qFuzzyIsNull(ask)) {
         ui_->spinBoxOfferPx->setValue(ask);
      }
   }
}

void RFQDealerReply::onBestQuotePrice(const QString reqId, double price, bool own)
{
   bestQPrices_[reqId.toStdString()] = price;

   if (!currentQRN_.empty() && (currentQRN_.quoteRequestId == reqId.toStdString())) {
      if (autoUpdatePrices_) {
         auto priceWidget = getActivePriceWidget();
         if (priceWidget && !own) {
            double improvedPrice = price;
            const auto assetType = assetManager_->GetAssetTypeForSecurity(currentQRN_.security);
            if (assetType != bs::network::Asset::Type::Undefined) {
               const auto pip = std::pow(10, -UiUtils::GetPricePrecisionForAssetType(assetType));
               if (priceWidget == ui_->spinBoxBidPx) {
                  improvedPrice += pip;
               } else {
                  improvedPrice -= pip;
               }
            } else {
               logger_->error("[RFQDealerReply::onBestQuotePrice] could not get type for {}", currentQRN_.security);
            }
            priceWidget->setValue(improvedPrice);
         }
      }
   }
}

void RFQDealerReply::onAQReply(const bs::network::QuoteReqNotification &qrn, double price)
{
   const auto &cbSubmit = [this, qrn](bs::network::QuoteNotification qn) {
      if (!qn.quoteRequestId.empty()) {
         logger_->debug("Submitted AQ reply on {}: {}/{}", qrn.quoteRequestId, qn.bidPx, qn.offerPx);
         QMetaObject::invokeMethod(this, [this, qn] { emit submitQuoteNotif(qn); });
      }
   };

   std::shared_ptr<TransactionData> transData;
   if (qrn.assetType != bs::network::Asset::SpotFX) {
      auto wallet = getCurrentWallet();
      if (!wallet) {
         wallet = walletsManager_->GetDefaultWallet();
      }
      transData = std::make_shared<TransactionData>();
      transData->SetWallet(wallet, armory_->topBlock());
      if (qrn.assetType == bs::network::Asset::PrivateMarket) {
         const auto &cc = qrn.product;
         const auto& ccWallet = getCCWallet(cc);
         if (qrn.side == bs::network::Side::Buy) {
            transData->SetSigningWallet(ccWallet);
            curWallet_ = wallet;
         }
         else {
            if (!ccWallet) {
               ui_->checkBoxAQ->setChecked(false);
               BSMessageBox(BSMessageBox::critical, tr("Auto Quoting")
                  , tr("No wallet created for %1 - auto-quoting disabled").arg(QString::fromStdString(cc))
               ).exec();
               return;
            }
            transData->SetSigningWallet(wallet);
            curWallet_ = ccWallet;
         }
      }

      const auto &cbFee = [this, qrn, price, transData, cbSubmit](float feePerByte) {
         transData->SetFeePerByte(feePerByte);
         aq_->setTxData(qrn.quoteRequestId, transData);
         submitReply(transData, qrn, price, cbSubmit);
      };
      walletsManager_->estimatedFeePerByte(2, cbFee, this);
      return;
   }

   submitReply(transData, qrn, price, cbSubmit);
}

void RFQDealerReply::onAutoSignStateChanged(const std::string &walletId, bool active, const std::string &error)
{
   if (active) {
      return;
   }

   ui_->checkBoxAutoSign->setChecked(false);
   updateAutoSignState();

   if (!error.empty()) {
      BSMessageBox(BSMessageBox::warning, tr("Auto-Sign deactivated"), tr("Signer returned error: %1")
         .arg(QString::fromStdString(error))).exec();
   }
}

void RFQDealerReply::onHDLeafCreated(unsigned int id, BinaryData pubKey, BinaryData chainCode, std::string walletId)
{
   if (!leafCreateReqId_ || (leafCreateReqId_ != id)) {
      return;
   }
   leafCreateReqId_ = 0;
   const auto &priWallet = walletsManager_->GetPrimaryWallet();
   auto group = priWallet->getGroup(bs::hd::BlockSettle_CC);
   if (!group) {
      group = priWallet->createGroup(bs::hd::BlockSettle_CC);
   }
   const auto netType = (priWallet->getXBTGroupType() == bs::hd::CoinType::Bitcoin_main)
      ? NetworkType::MainNet : NetworkType::TestNet;
   const auto leafNode = std::make_shared<bs::hd::Node>(pubKey, chainCode, netType);
   const auto leaf = group->createLeaf(baseProduct_, leafNode);

   if (leaf) {
      leaf->setData(assetManager_->getCCGenesisAddr(baseProduct_).display<std::string>());
      leaf->setData(assetManager_->getCCLotSize(baseProduct_));

      ccWallet_ = leaf;
      updateUiWalletFor(currentQRN_);
      reset();
      updateRecvAddresses();
   } else {
      BSMessageBox(BSMessageBox::critical, tr("Failed to create wallet")
         , tr("Failed to create wallet")
         , tr("%1 Wallet").arg(QString::fromStdString(baseProduct_))).exec();
   }
}

void RFQDealerReply::onCreateHDWalletError(unsigned int id, std::string errMsg)
{
   if (!leafCreateReqId_ || (leafCreateReqId_ != id)) {
      return;
   }

   leafCreateReqId_ = 0;
   BSMessageBox(BSMessageBox::critical, tr("Failed to create wallet")
      , tr("Failed to create wallet")
      , tr("%1 Wallet").arg(QString::fromStdString(product_))).exec();
}

void RFQDealerReply::onCelerConnected()
{
   // enable toggleswitch only if a script file is already selected
   if (ui_->comboBoxAQScript->currentIndex() > 0) {
      ui_->checkBoxAQ->setEnabled(true);
   }
   celerConnected_ = true;
   ui_->groupBoxAutoSign->setEnabled(true);
}

void RFQDealerReply::onCelerDisconnected()
{
   celerConnected_ = false;
   ui_->checkBoxAQ->setEnabled(false);
   ui_->checkBoxAQ->setCheckState(Qt::Unchecked);
   ui_->groupBoxAutoSign->setEnabled(false);
   aqStateChanged(Qt::Unchecked);
   emit autoSignActivated({}, ui_->comboBoxWalletAS->currentData(UiUtils::WalletIdRole).toString(), false);
   logger_->info("Disabled auto-quoting due to Celer disconnection");
}
