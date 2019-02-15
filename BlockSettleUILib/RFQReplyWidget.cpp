
#include "RFQReplyWidget.h"
#include "ui_RFQReplyWidget.h"
#include <spdlog/logger.h>

#include <QDesktopWidget>
#include <QPushButton>

#include "AssetManager.h"
#include "AuthAddressManager.h"
#include "CelerClient.h"
#include "CelerSubmitQuoteNotifSequence.h"
#include "DealerCCSettlementContainer.h"
#include "DealerCCSettlementDialog.h"
#include "DealerXBTSettlementContainer.h"
#include "DealerXBTSettlementDialog.h"
#include "DialogManager.h"
#include "HDWallet.h"
#include "MarketDataProvider.h"
#include "BSMessageBox.h"
#include "OrderListModel.h"
#include "QuoteProvider.h"
#include "RFQDialog.h"
#include "SignContainer.h"
#include "RFQBlotterTreeView.h"
#include "CustomDoubleSpinBox.h"
#include "OrdersView.h"

using namespace bs::ui;

RFQReplyWidget::RFQReplyWidget(QWidget* parent)
   : TabWithShortcut(parent)
   , ui_(new Ui::RFQReplyWidget())
{
   ui_->setupUi(this);

   connect(ui_->widgetQuoteRequests, &QuoteRequestsWidget::quoteReqNotifStatusChanged, ui_->pageRFQReply
      , &RFQDealerReply::quoteReqNotifStatusChanged, Qt::QueuedConnection);
   connect(ui_->pageRFQReply, &RFQDealerReply::autoSignActivated, this, &RFQReplyWidget::onAutoSignActivated);
}

RFQReplyWidget::~RFQReplyWidget() = default;

void RFQReplyWidget::SetWalletsManager(const std::shared_ptr<WalletsManager> &walletsManager)
{
   if (!walletsManager_ && walletsManager) {
      walletsManager_ = walletsManager;
      ui_->pageRFQReply->setWalletsManager(walletsManager_);

      if (signingContainer_) {
         auto primaryWallet = walletsManager_->GetPrimaryWallet();
         if (primaryWallet != nullptr) {
            signingContainer_->GetInfo(primaryWallet->getWalletId());
         }
      }
   }
}

void RFQReplyWidget::shortcutActivated(ShortcutType s)
{
   switch (s) {
      case ShortcutType::Alt_1 : {
         ui_->widgetQuoteRequests->view()->activate();
      }
         break;

      case ShortcutType::Alt_2 : {
         if (ui_->pageRFQReply->bidSpinBox()->isVisible()) {
            if (ui_->pageRFQReply->bidSpinBox()->isEnabled())
               ui_->pageRFQReply->bidSpinBox()->setFocus();
            else
               ui_->pageRFQReply->offerSpinBox()->setFocus();
         } else {
            ui_->pageRFQReply->setFocus();
         }
      }
         break;

      case ShortcutType::Alt_3 : {
         ui_->treeViewOrders->activate();
      }
         break;

      case ShortcutType::Ctrl_Q : {
         if (ui_->pageRFQReply->quoteButton()->isEnabled())
            ui_->pageRFQReply->quoteButton()->click();
      }
         break;

      case ShortcutType::Ctrl_P : {
         if (ui_->pageRFQReply->pullButton()->isEnabled())
            ui_->pageRFQReply->pullButton()->click();
      }
         break;

      default :
         break;
   }
}

void RFQReplyWidget::init(std::shared_ptr<spdlog::logger> logger
   , const std::shared_ptr<CelerClient>& celerClient
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , const std::shared_ptr<QuoteProvider>& quoteProvider
   , const std::shared_ptr<MarketDataProvider>& mdProvider
   , const std::shared_ptr<AssetManager>& assetManager
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<DialogManager> &dialogManager
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<ArmoryConnection> &armory)
{
   logger_ = logger;
   celerClient_ = celerClient;
   authAddressManager_ = authAddressManager;
   quoteProvider_ = quoteProvider;
   assetManager_ = assetManager;
   dialogManager_ = dialogManager;
   signingContainer_ = container;
   armory_ = armory;
   appSettings_ = appSettings;

   statsCollector_ = std::make_shared<bs::SecurityStatsCollector>(appSettings, ApplicationSettings::Filter_MD_QN_cnt);
   connect(ui_->pageRFQReply, &RFQDealerReply::submitQuoteNotif, statsCollector_.get(), &bs::SecurityStatsCollector::onQuoteSubmitted);

   ui_->widgetQuoteRequests->init(logger_, quoteProvider_, assetManager, statsCollector_,
                                  appSettings, celerClient_);
   ui_->pageRFQReply->init(logger, authAddressManager, assetManager, quoteProvider_, appSettings, signingContainer_, armory_, mdProvider);

   connect(ui_->widgetQuoteRequests, &QuoteRequestsWidget::Selected, ui_->pageRFQReply, &RFQDealerReply::setQuoteReqNotification);
   connect(ui_->pageRFQReply, &RFQDealerReply::submitQuoteNotif, quoteProvider_.get()
      , &QuoteProvider::SubmitQuoteNotif, Qt::QueuedConnection);
   connect(ui_->pageRFQReply, &RFQDealerReply::submitQuoteNotif, ui_->widgetQuoteRequests, &QuoteRequestsWidget::onQuoteReqNotifReplied);
   connect(ui_->pageRFQReply, &RFQDealerReply::submitQuoteNotif, this, &RFQReplyWidget::onReplied);
   connect(ui_->pageRFQReply, &RFQDealerReply::pullQuoteNotif, quoteProvider_.get(), &QuoteProvider::CancelQuoteNotif);

   connect(mdProvider.get(), &MarketDataProvider::MDUpdate, ui_->widgetQuoteRequests, &QuoteRequestsWidget::onSecurityMDUpdated);
   connect(mdProvider.get(), &MarketDataProvider::MDUpdate, ui_->pageRFQReply, &RFQDealerReply::onMDUpdate);

   connect(quoteProvider_.get(), &QuoteProvider::orderUpdated, this, &RFQReplyWidget::onOrder);
   connect(quoteProvider_.get(), &QuoteProvider::quoteCancelled, ui_->widgetQuoteRequests, &QuoteRequestsWidget::onQuoteReqCancelled);
   connect(quoteProvider_.get(), &QuoteProvider::bestQuotePrice, ui_->widgetQuoteRequests, &QuoteRequestsWidget::onBestQuotePrice, Qt::QueuedConnection);
   connect(quoteProvider_.get(), &QuoteProvider::bestQuotePrice, ui_->pageRFQReply, &RFQDealerReply::onBestQuotePrice, Qt::QueuedConnection);

   connect(quoteProvider_.get(), &QuoteProvider::quoteRejected, ui_->widgetQuoteRequests, &QuoteRequestsWidget::onQuoteRejected);

   connect(quoteProvider_.get(), &QuoteProvider::quoteNotifCancelled, ui_->widgetQuoteRequests
      , &QuoteRequestsWidget::onQuoteNotifCancelled);
   connect(quoteProvider_.get(), &QuoteProvider::signTxRequested, this, &RFQReplyWidget::onSignTxRequested);

   auto ordersModel = new OrderListModel(quoteProvider_, assetManager, this);
   ui_->treeViewOrders->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
   ui_->treeViewOrders->setModel(ordersModel);
   ui_->treeViewOrders->initWithModel(ordersModel);

   connect(celerClient_.get(), &CelerClient::OnConnectedToServer, ui_->pageRFQReply, &RFQDealerReply::onCelerConnected);
   connect(celerClient_.get(), &CelerClient::OnConnectionClosed, ui_->pageRFQReply, &RFQDealerReply::onCelerDisconnected);
}

void RFQReplyWidget::onReplied(bs::network::QuoteNotification qn)
{
   if (qn.assetType == bs::network::Asset::SpotFX) {
      return;
   }

   const auto &txData = ui_->pageRFQReply->getTransactionData(qn.quoteRequestId);
   if (qn.assetType == bs::network::Asset::SpotXBT) {
      sentXbtTransactionData_[qn.settlementId] = txData;
   } else if (qn.assetType == bs::network::Asset::PrivateMarket) {
      sentCCReplies_[qn.quoteRequestId] = SentCCReply{qn.receiptAddress, txData, qn.reqAuthKey};
   }
}

void RFQReplyWidget::onOrder(const bs::network::Order &order)
{
   if (order.assetType == bs::network::Asset::SpotFX) {
      return;
   }

   if (order.status == bs::network::Order::Pending) {
      if (order.assetType == bs::network::Asset::PrivateMarket) {
         const auto &quoteReqId = quoteProvider_->getQuoteReqId(order.quoteId);
         if (quoteReqId.empty()) {
            logger_->error("[RFQReplyWidget::onOrder] quoteReqId is empty for {}", order.quoteId);
            return;
         }
         const auto itCCSR = sentCCReplies_.find(quoteReqId);
         if (itCCSR == sentCCReplies_.end()) {
            logger_->error("[RFQReplyWidget::onOrder] missing previous CC reply for {}", quoteReqId);
            return;
         }
         const auto &sr = itCCSR->second;
         try {
            const auto settlContainer = std::make_shared<DealerCCSettlementContainer>(logger_, order, quoteReqId
               , assetManager_->getCCLotSize(order.product), assetManager_->getCCGenesisAddr(order.product)
               , sr.recipientAddress, sr.txData, signingContainer_, armory_, ui_->pageRFQReply->autoSign());
            connect(settlContainer.get(), &DealerCCSettlementContainer::signTxRequest, this, &RFQReplyWidget::saveTxData);

            if (ui_->pageRFQReply->autoSign()) {
               connect(settlContainer.get(), &bs::SettlementContainer::readyToAccept, this, &RFQReplyWidget::onReadyToAutoSign);
               ui_->widgetQuoteRequests->addSettlementContainer(settlContainer);
               settlContainer->activate();
            } else {
               auto settlDlg = new DealerCCSettlementDialog(logger_, settlContainer,
                  sr.requestorAuthAddress, walletsManager_, signingContainer_
                  , celerClient_, appSettings_, this);
               showSettlementDialog(settlDlg);
            }
         } catch (const std::exception &e) {
            BSMessageBox box(BSMessageBox::critical, tr("Settlement error")
               , tr("Failed to start dealer's CC settlement")
               , QString::fromLatin1(e.what())
               , this);
            box.exec();
         }
      } else {
         auto iTransactionData = sentXbtTransactionData_.find(order.settlementId);
         if (iTransactionData == sentXbtTransactionData_.end()) {
            logger_->debug("[RFQReplyWidget::onOrder] haven't seen QuoteNotif with settlId={}", order.settlementId);
         } else {
            try {
               const auto settlContainer = std::make_shared<DealerXBTSettlementContainer>(logger_, order, walletsManager_
                  , quoteProvider_, iTransactionData->second, authAddressManager_->GetBSAddresses(), signingContainer_
                  , armory_, ui_->pageRFQReply->autoSign());

               if (ui_->pageRFQReply->autoSign()) {
                  connect(settlContainer.get(), &bs::SettlementContainer::readyToAccept, this, &RFQReplyWidget::onReadyToAutoSign);
                  ui_->widgetQuoteRequests->addSettlementContainer(settlContainer);
                  settlContainer->activate();
               } else {
                  auto *dsd = new DealerXBTSettlementDialog(logger_, settlContainer, assetManager_,
                     walletsManager_, signingContainer_, celerClient_, appSettings_, this);
                  showSettlementDialog(dsd);
               }
            } catch (const std::exception &e) {
               BSMessageBox box(BSMessageBox::critical, tr("Settlement error")
                  , tr("Failed to start dealer's settlement")
                  , QString::fromLatin1(e.what())
                  , this);
               box.exec();
            }
         }
      }
   } else {
      const auto &quoteReqId = quoteProvider_->getQuoteReqId(order.quoteId);
      if (!quoteReqId.empty()) {
         sentCCReplies_.erase(quoteReqId);
         quoteProvider_->delQuoteReqId(quoteReqId);
      }
      sentXbtTransactionData_.erase(order.settlementId);
   }
}

void RFQReplyWidget::onReadyToAutoSign()
{
   const auto settlContainer = qobject_cast<bs::SettlementContainer *>(sender());
   if (!settlContainer) {
      logger_->error("[RFQReplyWidget::onReadyToAutoSign] failed to cast sender");
      return;
   }
   if (!settlContainer->accept()) {
      logger_->warn("[RFQReplyWidget::onReadyToAutoSign] failed to accept");
      return;
   }
}

void RFQReplyWidget::onAutoSignActivated(const SecureBinaryData &password, const QString &hdWalletId, bool active)
{
   if (walletsManager_ == nullptr) {
      return;
   }

   auto hdWallet = walletsManager_->GetHDWalletById(hdWalletId.toStdString());
   if (!hdWallet) {
      logger_->warn("[RFQReplyWidget::onAutoSignActivated] failed to get HD wallet for id {} - falling back to main primary"
         , hdWalletId.toStdString());
      hdWallet = walletsManager_->GetPrimaryWallet();
   }
   signingContainer_->SetLimits(hdWallet, password, active);
}

void RFQReplyWidget::saveTxData(QString orderId, std::string txData)
{
   const auto it = ccTxByOrder_.find(orderId.toStdString());
   if (it != ccTxByOrder_.end()) {
      logger_->debug("[RFQReplyWidget::saveTxData] TX data already requested for order {}", orderId.toStdString());
      quoteProvider_->SignTxRequest(orderId, txData);
      ccTxByOrder_.erase(orderId.toStdString());
   }
   else {
      logger_->debug("[RFQReplyWidget::saveTxData] saving TX data[{}] for order {}", txData.length(), orderId.toStdString());
      ccTxByOrder_[orderId.toStdString()] = txData;
   }
}

void RFQReplyWidget::onSignTxRequested(QString orderId, QString reqId)
{
   Q_UNUSED(reqId);
   const auto it = ccTxByOrder_.find(orderId.toStdString());
   if (it == ccTxByOrder_.end()) {
      logger_->debug("[RFQReplyWidget::onSignTxRequested] no TX data for order {}, yet", orderId.toStdString());
      ccTxByOrder_[orderId.toStdString()] = std::string{};
      return;
   }
   quoteProvider_->SignTxRequest(orderId, it->second);
   ccTxByOrder_.erase(orderId.toStdString());
}


void RFQReplyWidget::showSettlementDialog(QDialog *dlg)
{
   dlg->setAttribute(Qt::WA_DeleteOnClose);

   dialogManager_->adjustDialogPosition(dlg);

   dlg->show();
}
