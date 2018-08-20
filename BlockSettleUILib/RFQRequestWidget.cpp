
#include "RFQRequestWidget.h"
#include "ui_RFQRequestWidget.h"
#include "ApplicationSettings.h"
#include "AuthAddressManager.h"
#include "CelerClient.h"
#include "DialogManager.h"
#include "NotificationCenter.h"
#include "OrderListModel.h"
#include "QuoteProvider.h"
#include "RFQDialog.h"
#include "SignContainer.h"
#include "OrdersView.h"

#include <QPushButton>
#include <QLineEdit>


RFQRequestWidget::RFQRequestWidget(QWidget* parent)
   : TabWithShortcut(parent)
   , ui_(new Ui::RFQRequestWidget())
   , authAddressManager_(nullptr)
   , walletsManager_(nullptr)
{
   ui_->setupUi(this);

   connect(ui_->pageRFQTicket, &RFQTicketXBT::submitRFQ, this, &RFQRequestWidget::onRFQSubmit);

   connect(ui_->widgetMarketData, &MarketDataWidget::CurrencySelected, ui_->pageRFQTicket, &RFQTicketXBT::setSecurityId);
   connect(ui_->widgetMarketData, &MarketDataWidget::BuyClicked, ui_->pageRFQTicket, &RFQTicketXBT::setSecuritySell);
   connect(ui_->widgetMarketData, &MarketDataWidget::SellClicked, ui_->pageRFQTicket, &RFQTicketXBT::setSecurityBuy);
}

RFQRequestWidget::~RFQRequestWidget()
{}

void RFQRequestWidget::SetWalletsManager(const std::shared_ptr<WalletsManager> &walletsManager)
{
   if (walletsManager_ == nullptr) {
      walletsManager_ = walletsManager;
      ui_->pageRFQTicket->setWalletsManager(walletsManager);
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

void RFQRequestWidget::init(std::shared_ptr<spdlog::logger> logger
   , const std::shared_ptr<CelerClient>& celerClient
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , std::shared_ptr<QuoteProvider> quoteProvider
   , const std::shared_ptr<MarketDataProvider>& mdProvider
   , const std::shared_ptr<AssetManager>& assetManager
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<DialogManager> &dialogManager
   , const std::shared_ptr<SignContainer> &container)
{
   logger_ = logger;
   celerClient_ = celerClient;
   authAddressManager_ = authAddressManager;
   quoteProvider_ = quoteProvider;
   assetManager_ = assetManager;
   dialogManager_ = dialogManager;
   signingContainer_ = container;

   ui_->widgetMarketData->init(appSettings, ApplicationSettings::Filter_MD_RFQ, mdProvider);
   ui_->pageRFQTicket->init(authAddressManager, assetManager, quoteProvider, container);

   auto ordersModel = new OrderListModel(quoteProvider_, assetManager, ui_->treeViewOrders);
   ui_->treeViewOrders->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
   ui_->treeViewOrders->setModel(ordersModel);
   ui_->treeViewOrders->initWithModel(ordersModel);
   connect(quoteProvider_.get(), &QuoteProvider::quoteOrderFilled, [this](const std::string &quoteId) {
      NotificationCenter::notify(bs::ui::NotifyType::CelerOrder, {true, QString::fromStdString(quoteId)});
   });
   connect(quoteProvider_.get(), &QuoteProvider::orderFailed, [this](const std::string &quoteId, const std::string &reason) {
      NotificationCenter::notify(bs::ui::NotifyType::CelerOrder
         , { false, QString::fromStdString(quoteId), QString::fromStdString(reason) });
   });

   connect(celerClient_.get(), &CelerClient::OnConnectionClosed, ui_->pageRFQTicket, &RFQTicketXBT::disablePanel);
}

void RFQRequestWidget::onRFQSubmit(const bs::network::RFQ& rfq)
{
   RFQDialog* dialog = new RFQDialog(logger_, rfq, ui_->pageRFQTicket->GetTransactionData(), quoteProvider_,
      authAddressManager_, assetManager_, walletsManager_, signingContainer_, celerClient_, this);

   dialog->setAttribute(Qt::WA_DeleteOnClose);

   dialogManager_->adjustDialogPosition(dialog);
   dialog->show();

   ui_->pageRFQTicket->resetTicket();
}

