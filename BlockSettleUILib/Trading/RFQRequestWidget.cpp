#include "RFQRequestWidget.h"

#include <QLineEdit>
#include <QPushButton>

#include "ApplicationSettings.h"
#include "AuthAddressManager.h"
#include "CelerClient.h"
#include "DialogManager.h"
#include "NotificationCenter.h"
#include "OrderListModel.h"
#include "OrdersView.h"
#include "QuoteProvider.h"
#include "RFQDialog.h"
#include "SignContainer.h"
#include "Wallets/SyncWalletsManager.h"
#include "CurrencyPair.h"
#include "ui_RFQRequestWidget.h"


namespace  {
   const QString shieldLogin = QLatin1String("Login to submit RFQs");
   const QString shieldTraidingParticipantOnly = QLatin1String("Reserved for Trading Participants");
   const QString shieldCreateWallet = QLatin1String("Create Wallet");
   const QString shieldPromoteToPrimary = QLatin1String("Promote to Primary");
   const QString shieldCreateXXXWallet = QLatin1String("Create %1 wallet");

   const QString shieldButtonPromote = QLatin1String("Promote");
   const QString shieldButtonCreate = QLatin1String("Create");

   enum class RFQPages : int
   {
      ShieldPage = 0,
      EditableRFQPage
   };

   enum class ProductGroup
   {
      PM = 0,
      XBT,
      FX,
      NONE
   };

   ProductGroup getProductGroup(const QString &productGroup)
   {
      if (productGroup == QLatin1String("Private Market")) {
         return ProductGroup::PM;
      } else if (productGroup == QLatin1String("Spot XBT")) {
         return ProductGroup::XBT;
      } else if (productGroup == QLatin1String("Spot FX")) {
         return ProductGroup::FX;
      }
#ifndef QT_NO_DEBUG
      // You need to add logic for new Product group type
      Q_ASSERT(false);
#endif
      return ProductGroup::NONE;
   }
}

RFQRequestWidget::RFQRequestWidget(QWidget* parent)
   : TabWithShortcut(parent)
   , ui_(new Ui::RFQRequestWidget())
{
   ui_->setupUi(this);

   connect(ui_->pageRFQTicket, &RFQTicketXBT::submitRFQ, this, &RFQRequestWidget::onRFQSubmit);
   showShieldLoginRequiered();
}

RFQRequestWidget::~RFQRequestWidget() = default;

void RFQRequestWidget::setWalletsManager(const std::shared_ptr<bs::sync::WalletsManager> &walletsManager)
{
   if (walletsManager_ == nullptr) {
      walletsManager_ = walletsManager;
      ui_->pageRFQTicket->setWalletsManager(walletsManager);

      connect(walletsManager_.get(), &bs::sync::WalletsManager::CCLeafCreated, this, &RFQRequestWidget::forceCheckCondition);
      connect(walletsManager_.get(), &bs::sync::WalletsManager::AuthLeafCreated, this, &RFQRequestWidget::forceCheckCondition);
      connect(walletsManager_.get(), &bs::sync::WalletsManager::walletChanged, this, &RFQRequestWidget::forceCheckCondition);
      connect(walletsManager_.get(), &bs::sync::WalletsManager::walletDeleted, this, &RFQRequestWidget::forceCheckCondition);
      connect(walletsManager_.get(), &bs::sync::WalletsManager::walletAdded, this, &RFQRequestWidget::forceCheckCondition);
      connect(walletsManager_.get(), &bs::sync::WalletsManager::walletsReady, this, &RFQRequestWidget::forceCheckCondition);
      connect(walletsManager_.get(), &bs::sync::WalletsManager::walletsSynchronized, this, &RFQRequestWidget::forceCheckCondition);
   }
}

void RFQRequestWidget::shortcutActivated(ShortcutType s)
{
   switch (s) {
      case ShortcutType::Alt_1 : {
         ui_->widgetMarketData->view()->activate();
      }
         break;

      case ShortcutType::Alt_2 : {
         if (ui_->pageRFQTicket->lineEditAmount()->isVisible())
            ui_->pageRFQTicket->lineEditAmount()->setFocus();
         else
            ui_->pageRFQTicket->setFocus();
      }
         break;

      case ShortcutType::Alt_3 : {
         ui_->treeViewOrders->activate();
      }
         break;

      case ShortcutType::Ctrl_S : {
         if (ui_->pageRFQTicket->submitButton()->isEnabled())
            ui_->pageRFQTicket->submitButton()->click();
      }
         break;

      case ShortcutType::Alt_S : {
         ui_->pageRFQTicket->sellButton()->click();
      }
         break;

      case ShortcutType::Alt_B : {
         ui_->pageRFQTicket->buyButton()->click();
      }
         break;

      case ShortcutType::Alt_P : {
         if (ui_->pageRFQTicket->numCcyButton()->isChecked())
            ui_->pageRFQTicket->denomCcyButton()->click();
         else
            ui_->pageRFQTicket->numCcyButton()->click();
      }
         break;

      default :
         break;
   }
}

void RFQRequestWidget::setAuthorized(bool authorized)
{
   ui_->widgetMarketData->setAuthorized(authorized);
}

void RFQRequestWidget::showShieldLoginRequiered()
{
   prepareAndPopShield(shieldLogin);
}

void RFQRequestWidget::showShieldReservedTraidingParticipant()
{
   prepareAndPopShield(shieldTraidingParticipantOnly);
}

void RFQRequestWidget::showShieldPromoteToPrimaryWallet()
{
   prepareAndPopShield(shieldPromoteToPrimary, true, shieldButtonPromote);
}

void RFQRequestWidget::showShieldCreateXXXLeaf(const QString& product)
{
   prepareAndPopShield(shieldCreateXXXWallet.arg(product), true, shieldButtonCreate);
}

void RFQRequestWidget::showShieldCreateWallet()
{
   prepareAndPopShield(shieldCreateWallet, true, shieldButtonCreate);
}

void RFQRequestWidget::showEditableRFQPage()
{
   ui_->stackedWidgetRFQ->setEnabled(true);
   ui_->pageRFQTicket->enablePanel();
   ui_->stackedWidgetRFQ->setCurrentIndex(static_cast<int>(RFQPages::EditableRFQPage));
}

void RFQRequestWidget::prepareAndPopShield(const QString& labelText,
      bool showButton /*= false*/, const QString& buttonText /*= QLatinString1()*/)
{
   ui_->stackedWidgetRFQ->setEnabled(true);

   ui_->shieldText->setText(labelText);

   ui_->shieldButton->setVisible(showButton);
   ui_->shieldButton->setEnabled(showButton);
   ui_->shieldButton->setText(buttonText);

   ui_->stackedWidgetRFQ->setCurrentIndex(static_cast<int>(RFQPages::ShieldPage));
   ui_->pageRFQTicket->disablePanel();
}

void RFQRequestWidget::initWidgets(const std::shared_ptr<MarketDataProvider>& mdProvider
   , const std::shared_ptr<ApplicationSettings> &appSettings)
{
   appSettings_ = appSettings;
   ui_->widgetMarketData->init(appSettings, ApplicationSettings::Filter_MD_RFQ, mdProvider);
}

void RFQRequestWidget::init(std::shared_ptr<spdlog::logger> logger
   , const std::shared_ptr<BaseCelerClient>& celerClient
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , std::shared_ptr<QuoteProvider> quoteProvider
   , const std::shared_ptr<AssetManager>& assetManager
   , const std::shared_ptr<DialogManager> &dialogManager
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<ConnectionManager> &connectionManager
)
{
   logger_ = logger;
   celerClient_ = celerClient;
   authAddressManager_ = authAddressManager;
   quoteProvider_ = quoteProvider;
   assetManager_ = assetManager;
   dialogManager_ = dialogManager;
   signingContainer_ = container;
   armory_ = armory;
   connectionManager_ = connectionManager;

   ui_->pageRFQTicket->init(authAddressManager, assetManager, quoteProvider, container, armory);

   auto ordersModel = new OrderListModel(quoteProvider_, assetManager, ui_->treeViewOrders);
   ui_->treeViewOrders->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
   ui_->treeViewOrders->setModel(ordersModel);
   ui_->treeViewOrders->initWithModel(ordersModel);
   connect(quoteProvider_.get(), &QuoteProvider::quoteOrderFilled, [](const std::string &quoteId) {
      NotificationCenter::notify(bs::ui::NotifyType::CelerOrder, {true, QString::fromStdString(quoteId)});
   });
   connect(quoteProvider_.get(), &QuoteProvider::orderFailed, [](const std::string &quoteId, const std::string &reason) {
      NotificationCenter::notify(bs::ui::NotifyType::CelerOrder
         , { false, QString::fromStdString(quoteId), QString::fromStdString(reason) });
   });

   connect(celerClient_.get(), &BaseCelerClient::OnConnectedToServer, this, &RFQRequestWidget::onConnectedToCeler);
   connect(celerClient_.get(), &BaseCelerClient::OnConnectionClosed, this, &RFQRequestWidget::onDisconnectedFromCeler);

   ui_->pageRFQTicket->disablePanel();
}

void RFQRequestWidget::onConnectedToCeler()
{
   marketDataConnection.push_back(connect(ui_->widgetMarketData, &MarketDataWidget::CurrencySelected,
                                          this, &RFQRequestWidget::onCurrencySelected));
   marketDataConnection.push_back(connect(ui_->widgetMarketData, &MarketDataWidget::BuyClicked,
                                          this, &RFQRequestWidget::onBuyClicked));
   marketDataConnection.push_back(connect(ui_->widgetMarketData, &MarketDataWidget::SellClicked,
                                          this, &RFQRequestWidget::onSellClicked));
   marketDataConnection.push_back(connect(ui_->widgetMarketData, &MarketDataWidget::MDHeaderClicked,
                                          this, &RFQRequestWidget::onDisableSelectedInfo));

   showEditableRFQPage();
}

void RFQRequestWidget::onDisconnectedFromCeler()
{  
   for (QMetaObject::Connection &conn : marketDataConnection) {
      QObject::disconnect(conn);
   }

   showShieldLoginRequiered();
}

void RFQRequestWidget::onRFQSubmit(const bs::network::RFQ& rfq)
{
   auto authAddr = ui_->pageRFQTicket->selectedAuthAddress();

   RFQDialog* dialog = new RFQDialog(logger_, rfq, ui_->pageRFQTicket->GetTransactionData(), quoteProvider_,
      authAddressManager_, assetManager_, walletsManager_, signingContainer_, armory_, celerClient_, appSettings_, connectionManager_, authAddr, this);

   dialog->setAttribute(Qt::WA_DeleteOnClose);

   dialogManager_->adjustDialogPosition(dialog);
   dialog->show();

   ui_->pageRFQTicket->resetTicket();
}

bool RFQRequestWidget::checkConditions(const MarkeSelectedInfo& selectedInfo)
{
   ui_->stackedWidgetRFQ->setEnabled(true);
   using UserType = CelerClient::CelerUserType;
   const UserType userType = celerClient_->celerUserType();

   const ProductGroup group = getProductGroup(selectedInfo.productGroup_);

   switch (userType) {
   case UserType::Market: {
      if (group == ProductGroup::XBT || group == ProductGroup::FX) {
         showShieldReservedTraidingParticipant();
         return false;
      } else if (checkWalletSettings(selectedInfo)) {
         return false;
      }
      break;
   }
   case UserType::Dealing:
   case UserType::Trading: {
      if ((group == ProductGroup::XBT || group == ProductGroup::PM) && checkWalletSettings(selectedInfo)) {
         return false;
      }
      break;
   }
   default: {
      break;
   }
   }

   if (ui_->stackedWidgetRFQ->currentIndex() != static_cast<int>(RFQPages::EditableRFQPage)) {
      showEditableRFQPage();
   }

   return true;
}

bool RFQRequestWidget::checkWalletSettings(const MarkeSelectedInfo& selectedInfo)
{
   // No wallet at all
   if (!walletsManager_ || walletsManager_->walletsCount() == 0) {
      showShieldCreateWallet();
      ui_->shieldButton->disconnect();
      connect(ui_->shieldButton, &QPushButton::clicked, this, [this]() {
         ui_->shieldButton->setDisabled(true);
         emit requestPrimaryWalletCreation();
      });
      return true;
   }

   // No primary wallet
   if (!walletsManager_->hasPrimaryWallet()) {
      showShieldPromoteToPrimaryWallet();
      ui_->shieldButton->disconnect();
      connect(ui_->shieldButton, &QPushButton::clicked, this, [this]() {
         ui_->shieldButton->setDisabled(true);
         emit requestPrimaryWalletCreation();
      });
      return true;
   }

   // No path
   const CurrencyPair cp(selectedInfo.currencyPair_.toStdString());
   const QString currentProduct_ = QString::fromStdString(cp.NumCurrency());
   if (!walletsManager_->getCCWallet(currentProduct_.toStdString())) {
      showShieldCreateXXXLeaf(currentProduct_);
      ui_->shieldButton->disconnect();
      connect(ui_->shieldButton, &QPushButton::clicked, this, [this, currentProduct_]() {
         ui_->shieldButton->setDisabled(true);
         walletsManager_->CreateCCLeaf(currentProduct_.toStdString());
      });
      return true;
   }

   return false;
}

void RFQRequestWidget::forceCheckCondition()
{
   if (!ui_->widgetMarketData) {
      return;
   }

   const auto& currentInfo = ui_->widgetMarketData->getCurrentlySelectedInfo();

   if (!currentInfo.isValid()) {
      return;
   }

   onSellClicked(currentInfo);
}

void RFQRequestWidget::onCurrencySelected(const MarkeSelectedInfo& selectedInfo)
{
   if (!checkConditions(selectedInfo)) {
      return;
   }

   ui_->pageRFQTicket->setSecurityId(selectedInfo.productGroup_, selectedInfo.currencyPair_,
                                     selectedInfo.bidPrice_, selectedInfo.offerPrice_);
}

void RFQRequestWidget::onBuyClicked(const MarkeSelectedInfo& selectedInfo)
{
   if (!checkConditions(selectedInfo)) {
      return;
   }

   ui_->pageRFQTicket->setSecuritySell(selectedInfo.productGroup_, selectedInfo.currencyPair_,
                                       selectedInfo.bidPrice_, selectedInfo.offerPrice_);
}

void RFQRequestWidget::onSellClicked(const MarkeSelectedInfo& selectedInfo)
{
   if (!checkConditions(selectedInfo)) {
      return;
   }

   ui_->pageRFQTicket->setSecurityBuy(selectedInfo.productGroup_, selectedInfo.currencyPair_,
                                       selectedInfo.bidPrice_, selectedInfo.offerPrice_);
}

void RFQRequestWidget::onDisableSelectedInfo()
{
   ui_->stackedWidgetRFQ->setDisabled(true);
}
