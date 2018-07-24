#include "QuoteRequestsWidget.h"
#include "ui_QuoteRequestsWidget.h"
#include <spdlog/logger.h>

#include "AssetManager.h"
#include "CurrencyPair.h"
#include "NotificationCenter.h"
#include "QuoteProvider.h"
#include "SettlementContainer.h"
#include "UiUtils.h"
#include "TreeViewWithEnterKey.h"

#include <QStyle>
#include <QStyleOptionProgressBar>

//
// DoNotDrawSelectionDelegate
//

//! This delegate just clears selection bit and paints item as
//! unselected always.
class DoNotDrawSelectionDelegate final : public QStyledItemDelegate
{
public:
   explicit DoNotDrawSelectionDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

   void paint(QPainter *painter, const QStyleOptionViewItem &opt,
              const QModelIndex &index) const override
   {
      QStyleOptionViewItem changedOpt = opt;
      changedOpt.state &= ~(QStyle::State_Selected);

      QStyledItemDelegate::paint(painter, changedOpt, index);
   }
}; // class DoNotDrawSelectionDelegate


QuoteRequestsWidget::QuoteRequestsWidget(QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::QuoteRequestsWidget())
   , model_(nullptr)
   , sortModel_(nullptr)
{
   ui_->setupUi(this);
   ui_->treeViewQuoteRequests->setUniformRowHeights(true);

   connect(ui_->treeViewQuoteRequests, &QTreeView::clicked, this, &QuoteRequestsWidget::onQuoteReqNotifSelected);
   connect(ui_->treeViewQuoteRequests, &QTreeView::doubleClicked, this, &QuoteRequestsWidget::onQuoteReqNotifSelected);
}

void QuoteRequestsWidget::init(std::shared_ptr<spdlog::logger> logger, const std::shared_ptr<QuoteProvider> &quoteProvider
   , const std::shared_ptr<AssetManager>& assetManager, const std::shared_ptr<bs::SecurityStatsCollector> &statsCollector
   , const std::shared_ptr<ApplicationSettings> &appSettings)
{
   logger_ = logger;
   assetManager_ = assetManager;
   appSettings_ = appSettings;
   dropQN_ = appSettings->get<bool>(ApplicationSettings::dropQN);

   model_ = new QuoteRequestsModel(statsCollector, ui_->treeViewQuoteRequests);
   model_->SetAssetManager(assetManager);

   sortModel_ = new QuoteReqSortModel(assetManager, this);
   sortModel_->setSourceModel(model_);

   ui_->treeViewQuoteRequests->setModel(sortModel_);

   connect(ui_->treeViewQuoteRequests, &QTreeView::collapsed,
           this, &QuoteRequestsWidget::onCollapsed);
   connect(ui_->treeViewQuoteRequests, &QTreeView::expanded,
           this, &QuoteRequestsWidget::onExpanded);
   connect(ui_->treeViewQuoteRequests, &TreeViewWithEnterKey::enterKeyPressed,
           this, &QuoteRequestsWidget::onEnterKeyInQuoteRequestsPressed);
   connect(model_, &QuoteRequestsModel::quoteReqNotifStatusChanged, [this](const bs::network::QuoteReqNotification &qrn) {
      emit quoteReqNotifStatusChanged(qrn);
   });
   connect(model_, &QAbstractItemModel::rowsInserted, this, &QuoteRequestsWidget::onRowsInserted);
   connect(model_, &QAbstractItemModel::rowsRemoved, this, &QuoteRequestsWidget::onRowsRemoved);
   connect(sortModel_, &QSortFilterProxyModel::rowsInserted, this, &QuoteRequestsWidget::onRowsChanged);
   connect(sortModel_, &QSortFilterProxyModel::rowsRemoved, this, &QuoteRequestsWidget::onRowsChanged);

   connect(quoteProvider.get(), &QuoteProvider::quoteReqNotifReceived, this, &QuoteRequestsWidget::onQuoteRequest);
   connect(appSettings.get(), &ApplicationSettings::settingChanged, this, &QuoteRequestsWidget::onSettingChanged);
   connect(assetManager_.get(), &AssetManager::securitiesReceived, this, &QuoteRequestsWidget::onSecuritiesReceived);

   ui_->treeViewQuoteRequests->setItemDelegateForColumn(
      static_cast<int>(QuoteRequestsModel::Column::Status), new ProgressDelegate(ui_->treeViewQuoteRequests));

   auto *doNotDrawSelectionDelegate = new DoNotDrawSelectionDelegate(ui_->treeViewQuoteRequests);
   ui_->treeViewQuoteRequests->setItemDelegateForColumn(
      static_cast<int>(QuoteRequestsModel::Column::QuotedPx),
      doNotDrawSelectionDelegate);
   ui_->treeViewQuoteRequests->setItemDelegateForColumn(
      static_cast<int>(QuoteRequestsModel::Column::IndicPx),
      doNotDrawSelectionDelegate);
   ui_->treeViewQuoteRequests->setItemDelegateForColumn(
      static_cast<int>(QuoteRequestsModel::Column::BestPx),
      doNotDrawSelectionDelegate);
   ui_->treeViewQuoteRequests->setItemDelegateForColumn(
      static_cast<int>(QuoteRequestsModel::Column::Empty),
      doNotDrawSelectionDelegate);

   const auto opt = ui_->treeViewQuoteRequests->viewOptions();
   const int width = opt.fontMetrics.boundingRect(tr("No quote received")).width() + 10;
   ui_->treeViewQuoteRequests->header()->resizeSection(
      static_cast<int>(QuoteRequestsModel::Column::Status),
      width);

   ui_->treeViewQuoteRequests->setColumnWidth(0, 110);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::Product, opt.fontMetrics.boundingRect(tr("Product_")).width() + 10);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::Side, opt.fontMetrics.boundingRect(tr("Side_")).width() + 10);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::Quantity, opt.fontMetrics.boundingRect(tr("Quantity_")).width() + 10);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::Party, opt.fontMetrics.boundingRect(tr("Party_")).width() + 10);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::QuotedPx, opt.fontMetrics.boundingRect(tr("Quoted_Price_")).width() + 10);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::IndicPx, opt.fontMetrics.boundingRect(tr("Indicative_Px_")).width() + 10);
   ui_->treeViewQuoteRequests->setColumnWidth(static_cast<int>(QuoteRequestsModel::Column::BestPx, opt.fontMetrics.boundingRect(tr("Best_Quoted_Px_")).width() + 10);
}

void QuoteRequestsWidget::onQuoteReqNotifSelected(const QModelIndex& index)
{
   const auto quoteIndex = sortModel_->index(index.row(), 0, index.parent());
   std::string qId = sortModel_->data(quoteIndex,
      static_cast<int>(QuoteRequestsModel::Role::ReqId)).toString().toStdString();
   const bs::network::QuoteReqNotification &qrn = model_->getQuoteReqNotification(qId);

   double bidPx = model_->getPrice(qrn.security, QuoteRequestsModel::Role::BidPrice);
   double offerPx = model_->getPrice(qrn.security, QuoteRequestsModel::Role::OfferPrice);
   const double bestQPx = sortModel_->data(quoteIndex,
      static_cast<int>(QuoteRequestsModel::Role::BestQPrice)).toDouble();
   if (!qFuzzyIsNull(bestQPx)) {
      CurrencyPair cp(qrn.security);
      bool isBuy = (qrn.side == bs::network::Side::Buy) ^ (cp.NumCurrency() == qrn.product);
      const double quotedPx = sortModel_->data(quoteIndex,
         static_cast<int>(QuoteRequestsModel::Role::QuotedPrice)).toDouble();
      auto assetType = assetManager_->GetAssetTypeForSecurity(qrn.security);
      const auto pip = qFuzzyCompare(bestQPx, quotedPx) ? 0.0 : std::pow(10, -UiUtils::GetPricePrecisionForAssetType(assetType));
      if (isBuy) {
         bidPx = bestQPx + pip;
      }
      else {
         offerPx = bestQPx - pip;
      }
   }
   emit Selected(qrn, bidPx, offerPx);
}

void QuoteRequestsWidget::addSettlementContainer(const std::shared_ptr<bs::SettlementContainer> &container)
{
   if (model_) {
      model_->addSettlementContainer(container);
   }
}

TreeViewWithEnterKey* QuoteRequestsWidget::view() const
{
   return ui_->treeViewQuoteRequests;
}

void QuoteRequestsWidget::onQuoteReqNotifReplied(const bs::network::QuoteNotification &qn)
{
   if (model_) {
      model_->onQuoteReqNotifReplied(qn);
   }
}

void QuoteRequestsWidget::onQuoteNotifCancelled(const QString &reqId)
{
   if (model_) {
      model_->onQuoteNotifCancelled(reqId);
   }
}

void QuoteRequestsWidget::onQuoteReqCancelled(const QString &reqId, bool byUser)
{
   if (model_) {
      model_->onQuoteReqCancelled(reqId, byUser);
   }
}

void QuoteRequestsWidget::onQuoteRejected(const QString &reqId, const QString &reason)
{
   if (model_) {
      model_->onQuoteRejected(reqId, reason);
   }
}

void QuoteRequestsWidget::onBestQuotePrice(const QString reqId, double price, bool own)
{
   if (model_) {
      model_->onBestQuotePrice(reqId, price, own);
   }
}

void QuoteRequestsWidget::onSecurityMDUpdated(bs::network::Asset::Type assetType, const QString &security, bs::network::MDFields mdFields)
{
   Q_UNUSED(assetType);
   if (model_ && !mdFields.empty()) {
      model_->onSecurityMDUpdated(security, mdFields);
   }
}

void QuoteRequestsWidget::onQuoteRequest(const bs::network::QuoteReqNotification &qrn)
{
   if (dropQN_) {
      bool checkResult = true;
      if (qrn.side == bs::network::Side::Buy) {
         checkResult = assetManager_->checkBalance(qrn.product, qrn.quantity);
      }
      else {
         CurrencyPair cp(qrn.security);
         checkResult = assetManager_->checkBalance(cp.ContraCurrency(qrn.product), 0.01);
      }
      if (!checkResult) {
         return;
      }
   }
   if (model_ != nullptr) {
      model_->onQuoteReqNotifReceived(qrn);
   }
}

void QuoteRequestsWidget::onSecuritiesReceived()
{
   sortModel_->SetFilter(appSettings_->get<QStringList>(ApplicationSettings::Filter_MD_QN));
   expandIfNeeded();
}

void QuoteRequestsWidget::onSettingChanged(int setting, QVariant val)
{
   switch (static_cast<ApplicationSettings::Setting>(setting))
   {
   case ApplicationSettings::Filter_MD_QN:
      sortModel_->SetFilter(val.toStringList());
      expandIfNeeded();
      break;

   case ApplicationSettings::dropQN:
      dropQN_ = val.toBool();
      break;

   default:   break;
   }
}

void QuoteRequestsWidget::onRowsChanged()
{
   unsigned int cntChildren = 0;
   for (int row = 0; row < sortModel_->rowCount(); row++) {
      cntChildren += sortModel_->rowCount(sortModel_->index(row, 0));
   }
   NotificationCenter::notify(bs::ui::NotifyType::DealerQuotes, { cntChildren });
}

void QuoteRequestsWidget::onRowsInserted(const QModelIndex &parent, int first, int last)
{
	const auto opt = ui_->treeViewQuoteRequests->viewOptions();
	for (int row = first; row <= last; row++)
	{
		const auto &index = model_->index(row, 0, parent);
		if (index.data(static_cast<int>(QuoteRequestsModel::Role::ReqId)).isNull())
		{
			expandIfNeeded();
		}
		else
		{
			for (int i = 0; i < sortModel_->columnCount(); ++i)
			{
				const auto &index2 = model_->index(row, i, parent);
				auto str = index2.data(static_cast<int>(Qt::DisplayRole)).toString();
				if (i != static_cast<int>(QuoteRequestsModel::Column::Status))
				{
					if (!str.isEmpty())
					{
						const auto width = opt.fontMetrics.boundingRect(str).width() + 10;
						if (width > ui_->treeViewQuoteRequests->columnWidth (i))
							ui_->treeViewQuoteRequests->resizeColumnToContents(i);
					}
				}
			}
		}
	}
}

void QuoteRequestsWidget::onRowsRemoved(const QModelIndex &, int, int)
{
   const auto &indices = ui_->treeViewQuoteRequests->selectionModel()->selectedIndexes();
   if (indices.isEmpty()) {
      emit Selected(bs::network::QuoteReqNotification(), 0, 0);
   }
   else {
      onQuoteReqNotifSelected(indices.first());
   }
}

void QuoteRequestsWidget::onCollapsed(const QModelIndex &index)
{
   if (index.isValid())
      collapsed_.append(path(sortModel_->mapToSource(index)));
}

void QuoteRequestsWidget::onExpanded(const QModelIndex &index)
{
   if (index.isValid())
      collapsed_.removeOne(path(sortModel_->mapToSource(index)));
}

void QuoteRequestsWidget::onEnterKeyInQuoteRequestsPressed(const QModelIndex &index)
{
   onQuoteReqNotifSelected(index);
}

QString QuoteRequestsWidget::path(const QModelIndex &index) const
{
   QModelIndex idx = model_->index(index.row(), 0, index.parent());

   QString res = QString::fromLatin1("/") + idx.data().toString();

   while (idx.parent().isValid()) {
      idx = idx.parent();
      res.prepend(QString::fromLatin1("/") + idx.data().toString());
   }

   return res;
}

void QuoteRequestsWidget::expandIfNeeded(const QModelIndex &index)
{
   if (!collapsed_.contains(path(sortModel_->mapToSource(index))))
      ui_->treeViewQuoteRequests->expand(index);

   for (int i = 0; i < sortModel_->rowCount(index); ++i)
      expandIfNeeded(sortModel_->index(i, 0, index));
}

bs::SecurityStatsCollector::SecurityStatsCollector(const std::shared_ptr<ApplicationSettings> appSettings, ApplicationSettings::Setting param)
   : appSettings_(appSettings), param_(param)
{
   const auto map = appSettings_->get<QVariantMap>(param);
   for (auto it = map.begin(); it != map.end(); ++it) {
      counters_[it.key().toStdString()] = it.value().toUInt();
   }
   connect(appSettings.get(), &ApplicationSettings::settingChanged, this, &bs::SecurityStatsCollector::onSettingChanged);

   gradeColors_ = { QColor(Qt::white), QColor(Qt::lightGray), QColor(Qt::gray), QColor(Qt::darkGray) };
   gradeBoundary_.resize(gradeColors_.size(), 0);

   timer_.setInterval(60 * 1000);  // once in a minute
   connect(&timer_, &QTimer::timeout, this, &bs::SecurityStatsCollector::saveState);
   timer_.start();
}

void bs::SecurityStatsCollector::saveState()
{
   if (!modified_) {
      return;
   }
   QVariantMap map;
   for (const auto counter : counters_) {
      map[QString::fromStdString(counter.first)] = counter.second;
   }
   appSettings_->set(param_, map);
   modified_ = false;
}

void bs::SecurityStatsCollector::onSettingChanged(int setting, QVariant val)
{
   if ((static_cast<ApplicationSettings::Setting>(setting) == param_) && val.toMap().empty()) {
      resetCounters();
   }
}

void bs::SecurityStatsCollector::onQuoteSubmitted(const bs::network::QuoteNotification &qn)
{
   counters_[qn.security]++;
   modified_ = true;
   recalculate();
}

unsigned int bs::SecurityStatsCollector::getGradeFor(const std::string &security) const
{
   const auto itSec = counters_.find(security);
   if (itSec == counters_.end()) {
      return gradeBoundary_.size() - 1;
   }
   for (size_t i = 0; i < gradeBoundary_.size(); i++) {
      if (itSec->second <= gradeBoundary_[i]) {
         return gradeBoundary_.size() - 1 - i;
      }
   }
   return 0;
}

QColor bs::SecurityStatsCollector::getColorFor(const std::string &security) const
{
   return gradeColors_[getGradeFor(security)];
}

void bs::SecurityStatsCollector::resetCounters()
{
   counters_.clear();
   modified_ = true;
   recalculate();
}

void bs::SecurityStatsCollector::recalculate()
{
   std::vector<unsigned int> counts;
   for (const auto &counter : counters_) {
      counts.push_back(counter.second);
   }
   std::sort(counts.begin(), counts.end());

   if (counts.size() < gradeBoundary_.size()) {
      for (unsigned int i = 0; i < counts.size(); i++) {
         gradeBoundary_[gradeBoundary_.size() - counts.size() + i] = counts[i];
      }
   }
   else {
      const unsigned int step = counts.size() / gradeBoundary_.size();
      for (unsigned int i = 0; i < gradeBoundary_.size(); i++) {
         gradeBoundary_[i] = counts[qMin<unsigned int>((i + 1) * step, counts.size() - 1)];
      }
   }
}


QColor bs::SettlementStatsCollector::getColorFor(const std::string &) const
{
   return QColor(Qt::cyan);
}

unsigned int bs::SettlementStatsCollector::getGradeFor(const std::string &) const
{
   return container_->timeLeftMs();
}


bool QuoteReqSortModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
   const auto leftGrade = sourceModel()->data(left,
      static_cast<int>(QuoteRequestsModel::Role::Grade));
   const auto rightGrade = sourceModel()->data(right,
      static_cast<int>(QuoteRequestsModel::Role::Grade));
   if (leftGrade != rightGrade) {
      return (leftGrade < rightGrade);
   }
   const auto leftTL = sourceModel()->data(left,
      static_cast<int>(QuoteRequestsModel::Role::TimeLeft));
   const auto rightTL = sourceModel()->data(right,
      static_cast<int>(QuoteRequestsModel::Role::TimeLeft));
   return (leftTL < rightTL);
}

bool QuoteReqSortModel::filterAcceptsRow(int sourceRow, const QModelIndex &srcParent) const
{
   if (visible_.empty()) {
      return true;
   }

   const auto index = sourceModel()->index(sourceRow, 0, srcParent);
   if (!sourceModel()->data(index,
      static_cast<int>(QuoteRequestsModel::Role::AllowFiltering)).toBool()) {
         return true;
   }
   const auto security = sourceModel()->data(index).toString();
   if (visible_.find(security) != visible_.end()) {
      return false;
   }
   return true;
}

void QuoteReqSortModel::SetFilter(const QStringList &visible)
{
   visible_.clear();
   for (const auto &item : visible) {
      visible_.insert(item);
   }

   invalidateFilter();
}

#include <QProgressBar>
#include <QPainter>
void ProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
   if (index.data(static_cast<int>(QuoteRequestsModel::Role::ShowProgress)).toBool()) {
      QStyleOptionProgressBar pOpt;
      pOpt.maximum = index.data(static_cast<int>(QuoteRequestsModel::Role::Timeout)).toInt();
      pOpt.minimum = 0;
      pOpt.progress = index.data(static_cast<int>(QuoteRequestsModel::Role::TimeLeft)).toInt();
      pOpt.rect = opt.rect;

      QApplication::style()->drawControl(QStyle::CE_ProgressBar, &pOpt, painter, &pbar_);
   } else {
      QStyleOptionViewItem changedOpt = opt;
      changedOpt.state &= ~(QStyle::State_Selected);

      QStyledItemDelegate::paint(painter, changedOpt, index);
   }
}
