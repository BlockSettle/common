#include "ChartWidget.h"
#include "spdlog/logger.h"
#include "ui_ChartWidget.h"
#include "Colors.h"
#include "MarketDataProvider.h"
#include "MdhsClient.h"
#include "market_data_history.pb.h"
#include "trade_history.pb.h"

const qreal BASE_FACTOR = 1.0;

const QColor BACKGROUND_COLOR = QColor(28, 40, 53);
const QColor FOREGROUND_COLOR = QColor(Qt::white);
const QColor VOLUME_COLOR     = QColor(32, 159, 223);

ChartWidget::ChartWidget(QWidget* pParent)
   : QWidget(pParent)
   , ui_(new Ui::ChartWidget)
   , title_(nullptr)
   , info_(nullptr)
   , candlesticksChart_(nullptr)
   , volumeChart_(nullptr)
   , volumeAxisRect_(nullptr)
   , lastHigh_(0.0)
   , lastLow_(0.0)
   , lastClose_(0.0)
   , currentTimestamp_(0.0)
   , timerId_(0)
   , lastInterval_(-1)
   , dragY_(0)
   , isDraggingYAxis_(false) {
   ui_->setupUi(this);
   setAutoScaleBtnColor();
   connect(ui_->autoScaleBtn, &QPushButton::clicked, this, &ChartWidget::OnAutoScaleBtnClick);

   connect(ui_->resetBtn, &QPushButton::clicked, this, &ChartWidget::OnResetBtnClick);
   // setting up date range radio button group
   dateRange_.addButton(ui_->btn1h, Interval::OneHour);
   dateRange_.addButton(ui_->btn6h, Interval::SixHours);
   dateRange_.addButton(ui_->btn12h, Interval::TwelveHours);
   dateRange_.addButton(ui_->btn24h, Interval::TwentyFourHours);
   dateRange_.addButton(ui_->btn1w, Interval::OneWeek);
   dateRange_.addButton(ui_->btn1m, Interval::OneMonth);
   dateRange_.addButton(ui_->btn6m, Interval::SixMonths);
   dateRange_.addButton(ui_->btn1y, Interval::OneYear);
   connect(&dateRange_, qOverload<int>(&QButtonGroup::buttonClicked),
           this, &ChartWidget::OnDateRangeChanged);

   connect(ui_->cboInstruments, &QComboBox::currentTextChanged,
           this, &ChartWidget::OnInstrumentChanged);

   // sort model for instruments combo box
   cboModel_ = new QStandardItemModel(this);
   auto proxy = new QSortFilterProxyModel();
   proxy->setSourceModel(cboModel_);
   proxy->sort(0);
   ui_->cboInstruments->setModel(proxy);
}

void ChartWidget::init(const std::shared_ptr<ApplicationSettings>& appSettings
                       , const std::shared_ptr<MarketDataProvider>& mdProvider
                       , const std::shared_ptr<ConnectionManager>& connectionManager
                       , const std::shared_ptr<spdlog::logger>& logger)
{
   appSettings_ = appSettings;
   mdProvider_ = mdProvider;
   mdhsClient_ = std::make_shared<MdhsClient>(appSettings, connectionManager, logger);
   logger_ = logger;

   connect(mdhsClient_.get(), &MdhsClient::DataReceived, this, &ChartWidget::OnDataReceived);
   connect(mdProvider_.get(), &MarketDataProvider::MDUpdate, this, &ChartWidget::OnMdUpdated);

   // initialize charts
   InitializeCustomPlot();

   // initial select interval
   ui_->btn1h->click();
}

ChartWidget::~ChartWidget() {
   killTimer(timerId_);
   delete ui_;
}

// Populate combo box with existing instruments comeing from mdProvider
void ChartWidget::OnMdUpdated(bs::network::Asset::Type assetType, const QString &security, bs::network::MDFields mdFields) {
   if ((assetType == bs::network::Asset::Undefined) && security.isEmpty()) // Celer disconnected
   {
      isProductListInitialized_ = false;
      cboModel_->clear();
      title_->setText(QStringLiteral());
      return;
   }
   else if (!isProductListInitialized_)
   {
      isProductListInitialized_ = true;
      MarketDataHistoryRequest request;
      request.set_request_type(MarketDataHistoryMessageType::ProductsListType);
      mdhsClient_->SendRequest(request);
   }

   if (title_->text() == security)
   {
      for (const auto& field : mdFields)
      {
         if (field.type == bs::network::MDField::PriceLast)
         {
            if (field.value == lastClose_)
               return;
            else
               lastClose_ = field.value;

            if (lastClose_ > lastHigh_)
               lastHigh_ = lastClose_;

            if (lastClose_ < lastLow_)
               lastLow_ = lastClose_;
         }

         if (field.type == bs::network::MDField::MDTimestamp)
         {
            currentTimestamp_ = field.value;
            timerId_ = startTimer(getTimerInterval());
         }
      }
   }

   ModifyCandle();

}

void ChartWidget::UpdateChart(const int& interval) const
{
   auto product = ui_->cboInstruments->currentText();
   if (product.isEmpty())
      return;
   if (title_) {
      title_->setText(product);
   }
   if (!candlesticksChart_ || !volumeChart_) {
      return;
   }
   candlesticksChart_->data()->clear();
   volumeChart_->data()->clear();
   qreal width = 0.8 * IntervalWidth(interval) / 1000;
   candlesticksChart_->setWidth(width);
   volumeChart_->setWidth(width);
   const auto currentTimestamp = QDateTime::currentMSecsSinceEpoch();
   OhlcRequest ohlcRequest;
   ohlcRequest.set_product(product.toStdString());
   ohlcRequest.set_interval(static_cast<Interval>(interval));
   ohlcRequest.set_count(requestLimit);
   ohlcRequest.set_lesser_then(-1);

   MarketDataHistoryRequest request;
   request.set_request_type(MarketDataHistoryMessageType::OhlcHistoryType);
   request.set_request(ohlcRequest.SerializeAsString());
   mdhsClient_->SendRequest(request);
}

void ChartWidget::OnDataReceived(const std::string& data)
{
   if (data.empty())
   {
      logger_->error("Empty data received from mdhs.");
      return;
   }

   MarketDataHistoryResponse response;
   if (!response.ParseFromString(data))
   {
      logger_->error("can't parse response from mdhs: {}", data);
      return;
   }

   switch (response.response_type()) {
   case MarketDataHistoryMessageType::ProductsListType:
      ProcessProductsListResponse(response.response());
      break;
   case MarketDataHistoryMessageType::OhlcHistoryType:
      ProcessOhlcHistoryResponse(response.response());
      break;
   default:
      logger_->error("[ApiServerConnectionListener::OnDataReceived] undefined message type");
      break;
   }
}

void ChartWidget::ProcessProductsListResponse(const std::string& data)
{
   if (data.empty())
   {
      logger_->error("Empty data received from mdhs.");
      return;
   }

   ProductsListResponse response;
   if (!response.ParseFromString(data))
   {
      logger_->error("can't parse response from mdhs: {}", data);
      return;
   }

   for (const auto& product : response.products())
   {
      productTypesMapper[product.product()] = product.type();
      cboModel_->appendRow(new QStandardItem(QString::fromStdString(product.product())));
   }
}

void ChartWidget::ProcessOhlcHistoryResponse(const std::string& data)
{
   if (data.empty())
   {
      logger_->error("Empty data received from mdhs.");
      return;
   }

   OhlcResponse response;
   if (!response.ParseFromString(data))
   {
      logger_->error("can't parse response from mdhs: {}", data);
      return;
   }

   bool firstPortion = candlesticksChart_->data()->size() == 0;

   auto product = ui_->cboInstruments->currentText();
   auto interval = dateRange_.checkedId();

   if (product != QString::fromStdString(response.product()) || interval != response.interval())
      return;


   qreal maxTimestamp = -1.0;

   for (auto it : response.candles()) {

   }

   for (int i = 0; i < response.candles_size(); i++)
   {
      auto candle = response.candles(i);
      maxTimestamp = qMax(maxTimestamp, static_cast<qreal>(candle.timestamp()));

      bool isLast = (i == 0);

      if (lastCandle_.timestamp() - candle.timestamp() != IntervalWidth(interval) && candlesticksChart_->data()->size()) {
         for (int j = 0; j < (lastCandle_.timestamp() - candle.timestamp()) / IntervalWidth(interval) - 1; j++) {
            AddDataPoint(lastCandle_.close(), lastCandle_.close(), lastCandle_.close(), lastCandle_.close(), lastCandle_.timestamp() - IntervalWidth(interval) * (j + 1), 0);
         }
      }
      lastCandle_ = candle;


      AddDataPoint(candle.open(), candle.high(), candle.low(), candle.close(), candle.timestamp(), candle.volume());
      qDebug("Added: %s, open: %f, high: %f, low: %f, close: %f, volume: %f"
         , QDateTime::fromMSecsSinceEpoch(candle.timestamp()).toUTC().toString(Qt::ISODateWithMs).toStdString().c_str()
         , candle.open()
         , candle.high()
         , candle.low()
         , candle.close()
         , candle.volume());
      if (firstPortion && isLast) {
         lastHigh_ = candle.high();
         lastLow_ = candle.low();
         lastClose_ = candle.close();
      }
   }


   if (firstPortion) {
      firstTimestampInDb_ = response.first_stamp_in_db() / 1000;
      UpdatePlot(interval, maxTimestamp);
   }
   else {
      LoadAdditionalPoints(volumeAxisRect_->axis(QCPAxis::atBottom)->range());
      rescalePlot();
      ui_->customPlot->replot();
   }
}

void ChartWidget::setAutoScaleBtnColor() const
{
   QString color = QStringLiteral("background-color: transparent; border: none; color: %1").
   arg(autoScaling_ ? QStringLiteral("rgb(36,124,172)") : QStringLiteral("rgb(255, 255, 255)"));
   ui_->autoScaleBtn->setStyleSheet(color);
}

void ChartWidget::AddNewCandle()
{
   const auto currentTimestamp = QDateTime::currentMSecsSinceEpoch();
   OhlcCandle candle;
   candle.set_open(lastClose_);
   candle.set_close(lastClose_);
   candle.set_high(lastClose_);
   candle.set_low(lastClose_);
   candle.set_timestamp(currentTimestamp);
   candle.set_volume(0.0);

   AddDataPoint(candle.open(), candle.high(), candle.low(), candle.close(), candle.timestamp(), candle.volume());
   qDebug("Added: %s, open: %f, high: %f, low: %f, close: %f, volume: %f"
      , QDateTime::fromMSecsSinceEpoch(candle.timestamp()).toUTC().toString(Qt::ISODateWithMs).toStdString().c_str()
      , candle.open()
      , candle.high()
      , candle.low()
      , candle.close()
      , candle.volume());

   UpdatePlot(dateRange_.checkedId(), currentTimestamp);
}

void ChartWidget::ModifyCandle()
{
   const auto& lastCandle = candlesticksChart_->data()->at(candlesticksChart_->data()->size() - 1);
   QCPFinancialData candle(*lastCandle);

   candle.close = lastClose_;
   candle.high = lastHigh_;
   candle.low = lastLow_;

   candlesticksChart_->data()->remove(lastCandle->key);
   candlesticksChart_->data()->add(candle);
}

void ChartWidget::UpdatePlot(const int& interval, const qint64& timestamp)
{
   qreal size = IntervalWidth(interval, requestLimit);
   qreal upper = timestamp + IntervalWidth(interval) / 2;

   ui_->customPlot->xAxis->setRange(upper / 1000, size / 1000, Qt::AlignRight);
   rescaleCandlesYAxis();
   ui_->customPlot->yAxis2->setNumberPrecision(FractionSizeForProduct(productTypesMapper[title_->text().toStdString()]));

}

void ChartWidget::timerEvent(QTimerEvent* event)
{
   killTimer(timerId_);

   timerId_ = startTimer(getTimerInterval());

   AddNewCandle();
}

std::chrono::seconds ChartWidget::getTimerInterval() const
{
   auto currentTime = QDateTime::fromMSecsSinceEpoch(static_cast<qint64> (currentTimestamp_)).time();

   auto timerInterval = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::milliseconds ((int) IntervalWidth(dateRange_.checkedId())));
   if (currentTime.second() != std::chrono::seconds(0).count())
   {
      if (currentTime.minute() != std::chrono::minutes(0).count())
      {
         auto currentSeconds = currentTime.minute() * 60 + currentTime.second();
         auto diff = timerInterval.count() - currentSeconds;
         timerInterval = std::chrono::seconds(diff);
      }
   }

   qDebug() << "timer will start after " << timerInterval.count() << " seconds";

   return timerInterval;
}

bool ChartWidget::needLoadNewData(const QCPRange& range, const QSharedPointer<QCPFinancialDataContainer> data) const
{
   return data->size() &&
      (range.lower - data->constBegin()->key < IntervalWidth(dateRange_.checkedId()) / 1000 * loadDistance)
      && firstTimestampInDb_ + IntervalWidth(OneHour) < data->constBegin()->key;
}

void ChartWidget::LoadAdditionalPoints(const QCPRange& range)
{
   const auto data = candlesticksChart_->data();
   if (needLoadNewData(range, data)) {
      if (qFuzzyCompare(prevRequestStamp, data->constBegin()->key)) {
         return;
      }
      OhlcRequest ohlcRequest;
      auto product = ui_->cboInstruments->currentText();
      ohlcRequest.set_product(product.toStdString());
      ohlcRequest.set_interval(static_cast<Interval>(dateRange_.checkedId()));
      ohlcRequest.set_count(requestLimit);
      ohlcRequest.set_lesser_then(data->constBegin()->key * 1000);

      prevRequestStamp = data->constBegin()->key;

      MarketDataHistoryRequest request;
      request.set_request_type(MarketDataHistoryMessageType::OhlcHistoryType);
      request.set_request(ohlcRequest.SerializeAsString());
      mdhsClient_->SendRequest(request);
   }
}

void ChartWidget::pickTicketDateFormat(const QCPRange& range) const
{
   const float rangeCoeff = 0.8;
   if (range.size() < 3 * 24 * 60 * 60 * rangeCoeff) {
      dateTimeTicker->setDateTimeFormat(QStringLiteral("dd MMM\nHH:mm"));
   }
   else if (range.size() < 365 * 24 * 60 * 60 * rangeCoeff) {
      dateTimeTicker->setDateTimeFormat(QStringLiteral("dd MMM"));
   }
   else {
      dateTimeTicker->setDateTimeFormat(QStringLiteral("MMM yyyy"));
   }
}

void ChartWidget::AddDataPoint(const qreal& open, const qreal& high, const qreal& low, const qreal& close, const qreal& timestamp, const qreal& volume) const
{
   if (candlesticksChart_) {
      candlesticksChart_->data()->add(QCPFinancialData(timestamp / 1000, open, high, low, close));
   }
   if (volumeChart_) {
      volumeChart_->data()->add(QCPBarsData(timestamp / 1000, volume));
   }
}

qreal ChartWidget::IntervalWidth(int interval, int count) const
{
   if (interval == -1) {
      return 1.0;
   }
   qreal hour = 3600000;
   switch (static_cast<Interval>(interval)) {
   case Interval::OneYear:
      return hour * 8760 * count;
   case Interval::SixMonths:
      return hour * 4320 * count;
   case Interval::OneMonth:
      return hour * 720 * count;
   case Interval::OneWeek:
      return hour * 168 * count;
   case Interval::TwentyFourHours:
      return hour * 24 * count;
   case Interval::TwelveHours:
      return hour * 12 * count;
   case Interval::SixHours:
      return hour * 6 * count;
   case Interval::OneHour:
      return hour * count;
   default:
      return hour * count;
   }
}

int ChartWidget::FractionSizeForProduct(TradeHistoryTradeType type)
{
   switch (type)
   {
   case FXTradeType:
      return 4;
   case XBTTradeType:
      return 2;
   case PMTradeType:
      return 6;
   default:
      return -1;
   }
}

// Handles changes of date range.
void ChartWidget::OnDateRangeChanged(int interval) {
   if (lastInterval_ != interval)
   {
      lastInterval_ = interval;
      UpdateChart(interval);
   }
}

void ChartWidget::OnInstrumentChanged(const QString &text) {
   if (title_ != nullptr)
   {
      if (text != title_->text())
      {
         UpdateChart(dateRange_.checkedId());
      }
   }
}

void ChartWidget::OnPlotMouseMove(QMouseEvent *event)
{
   if (info_ == nullptr)
      return;

   if (auto plottable = ui_->customPlot->plottableAt(event->localPos()))
   {
      double x = event->localPos().x();
      double width = 0.8 * IntervalWidth(dateRange_.checkedId()) / 1000;
      double timestamp = ui_->customPlot->xAxis->pixelToCoord(x) + width / 2;
      auto ohlcValue = *candlesticksChart_->data()->findBegin(timestamp);
      auto volumeValue = *volumeChart_->data()->findBegin(timestamp);
      /*auto date = QDateTime::fromMSecsSinceEpoch(timestamp * 1000).toUTC();
      qDebug() << "Position:" << event->pos() << event->localPos()
               << "Item at:"  << QString::number(timestamp, 'f') << date
               << ohlcValue.key << ohlcValue.open << ohlcValue.high
               << ohlcValue.low << ohlcValue.close << volumeValue.value;*/
      info_->setText(tr("O: %1   H: %2   L: %3   C: %4   Volume: %5")
                     .arg(ohlcValue.open, 0, 'g', -1)
                     .arg(ohlcValue.high, 0, 'g', -1)
                     .arg(ohlcValue.low, 0, 'g', -1)
                     .arg(ohlcValue.close, 0, 'g', -1)
                     .arg(volumeValue.value, 0, 'g', -1));
	  ui_->customPlot->replot();
   } else 
   {
      info_->setText({});
      ui_->customPlot->replot();
   }

   if (isDraggingYAxis_)
   {
      auto rightAxis = ui_->customPlot->yAxis2;
      auto currentYPos = event->pos().y();
      auto lower_bound = rightAxis->range().lower;
      auto upper_bound = rightAxis->range().upper;
      auto diff = upper_bound - lower_bound;
      auto directionCoeff = (currentYPos - lastDragCoord_.y() > 0) ? -1 : 1;
      lastDragCoord_.setY(currentYPos);
      double tempCoeff = 28.0; //change this to impact on xAxis scale speed, the lower coeff the faster scaling
      upper_bound -= diff / tempCoeff * directionCoeff;
      lower_bound += diff / tempCoeff * directionCoeff;
      rightAxis->setRange(lower_bound, upper_bound);
      ui_->customPlot->replot();
   }
   if (isDraggingXAxis_) {
      auto bottomAxis = volumeAxisRect_->axis(QCPAxis::atBottom);
      auto currentXPos = event->pos().x();
      auto lower_bound = volumeAxisRect_->axis(QCPAxis::atBottom)->range().lower;
      auto upper_bound = volumeAxisRect_->axis(QCPAxis::atBottom)->range().upper;
      auto diff = upper_bound - lower_bound;
      auto directionCoeff = (currentXPos - lastDragCoord_.x() > 0) ? -1 : 1;
      double scalingCoeff = qAbs(currentXPos - startDragCoordX_) / ui_->customPlot->size().width();
      lastDragCoord_.setX(currentXPos);
      double tempCoeff = 10.0; //change this to impact on xAxis scale speed, the lower coeff the faster scaling
      lower_bound += diff / tempCoeff * /*scalingCoeff * */ directionCoeff;
      bottomAxis->setRange(lower_bound, upper_bound);
      ui_->customPlot->replot();
   }
}

void ChartWidget::rescaleCandlesYAxis()
{
   bool foundRange = false;
   auto keyRange = candlesticksChart_->keyAxis()->range();
   keyRange.upper += IntervalWidth(dateRange_.checkedId()) / 1000 / 2;
   keyRange.lower -= IntervalWidth(dateRange_.checkedId()) / 1000 / 2;
   auto newRange = candlesticksChart_->getValueRange(foundRange, QCP::sdBoth, keyRange);
   if (foundRange) {
      const double margin = 0.15;
      if (!QCPRange::validRange(newRange)) // likely due to range being zero
      {
         double center = (newRange.lower + newRange.upper)*0.5; // upper and lower should be equal anyway, but just to make sure, incase validRange returned false for other reason
         newRange.lower = center - candlesticksChart_->valueAxis()->range().size() * margin / 2.0;
         newRange.upper = center + candlesticksChart_->valueAxis()->range().size() * margin / 2.0;
      } else {
         auto old = candlesticksChart_->valueAxis()->range();
         if (old != newRange) {
            newRange.lower -= newRange.size() * margin;
            newRange.upper += newRange.size() * margin;
         }
      }
      candlesticksChart_->valueAxis()->setRange(newRange);
   }
}

void ChartWidget::rescaleVolumesYAxis() const
{
   auto lower_bound = volumeAxisRect_->axis(QCPAxis::atBottom)->range().lower;
   auto upper_bound = volumeAxisRect_->axis(QCPAxis::atBottom)->range().upper;
   double maxVolume = volumeChart_->data()->constBegin()->value;
   for (const auto& it : *volumeChart_->data()) {
      if (it.key >= lower_bound && it.key <= upper_bound) {
         maxVolume = qMax(maxVolume, it.value);
      }
   }
   if (!qFuzzyCompare(maxVolume, volumeAxisRect_->axis(QCPAxis::atBottom)->range().upper)) {
      volumeAxisRect_->axis(QCPAxis::atRight)->setRange(0, maxVolume);
      volumeAxisRect_->axis(QCPAxis::atRight)->ticker()->setTickOrigin(maxVolume);
      ui_->customPlot->replot();
   }
}

void ChartWidget::rescalePlot()
{
   if (autoScaling_) {
      rescaleCandlesYAxis();
   }
   rescaleVolumesYAxis();

}

void ChartWidget::OnMousePressed(QMouseEvent* event)
{
   auto select = ui_->customPlot->yAxis2->selectTest(event->pos(), false);
   isDraggingYAxis_ = select != -1.0;
   if (isDraggingYAxis_) {
      if (autoScaling_) {
         ui_->autoScaleBtn->animateClick();
      }
      ui_->customPlot->setInteraction(QCP::iRangeDrag, false);
   }

   auto selectXPoint = volumeAxisRect_->axis(QCPAxis::atBottom)->selectTest(event->pos(), false);
   isDraggingXAxis_ = selectXPoint != -1.0;
   if (isDraggingXAxis_) {
      ui_->customPlot->setInteraction(QCP::iRangeDrag, false);
      volumeAxisRect_->axis(QCPAxis::atBottom)->axisRect()->setRangeDrag(volumeAxisRect_->axis(QCPAxis::atBottom)->orientation());
      startDragCoordX_ = event->pos().x();
   }

   if (isDraggingXAxis_ || isDraggingYAxis_) {
      lastDragCoord_ = event->pos();
   }

   if (ui_->customPlot->axisRect()->rect().contains(event->pos()) || volumeAxisRect_->rect().contains(event->pos())) {
      isDraggingMainPlot_ = true;
   }
}

void ChartWidget::OnMouseReleased(QMouseEvent* event)
{
   isDraggingYAxis_ = false;
   isDraggingXAxis_ = false;
   isDraggingMainPlot_ = false;
   ui_->customPlot->setInteraction(QCP::iRangeDrag, true);
}

void ChartWidget::OnAutoScaleBtnClick()
{
   if (autoScaling_ = !autoScaling_)
   {
      rescalePlot();
   }
   setAutoScaleBtnColor();
}

void ChartWidget::OnResetBtnClick()
{
   if (candlesticksChart_->data()->size()) {
      auto new_upper = candlesticksChart_->data()->at(candlesticksChart_->data()->size() - 1)->key + IntervalWidth(dateRange_.checkedId()) / 1000 / 2;
      volumeAxisRect_->axis(QCPAxis::atBottom)->setRange(new_upper - IntervalWidth(dateRange_.checkedId(), requestLimit) / 1000, new_upper);
   }
   if (!autoScaling_) {
      autoScaling_ = true;
      rescalePlot();
      setAutoScaleBtnColor();
   }
}

bool ChartWidget::isBeyondUpperLimit(QCPRange newRange, int interval)
{
   return (newRange.size() > IntervalWidth(interval, candleCountOnScreenLimit) / 1000) && !isDraggingMainPlot_;
}

bool ChartWidget::isBeyondLowerLimit(QCPRange newRange, int interval)
{
   return newRange.size() < IntervalWidth(interval, candleViewLimit) / 1000;
}

void ChartWidget::OnVolumeAxisRangeChanged(QCPRange newRange, QCPRange oneRange)
{
   auto interval = dateRange_.checkedId() == -1 ? 0 : dateRange_.checkedId();

   if (isBeyondUpperLimit(newRange, interval)) {
      volumeAxisRect_->axis(QCPAxis::atBottom)->setRange(oneRange.upper - IntervalWidth(interval, candleCountOnScreenLimit) / 1000, oneRange.upper);
      ui_->customPlot->xAxis->setRange(volumeAxisRect_->axis(QCPAxis::atBottom)->range());
   }
   else {
      if (isBeyondLowerLimit(newRange, interval)) {
         volumeAxisRect_->axis(QCPAxis::atBottom)->setRange(oneRange.upper - IntervalWidth(interval, candleViewLimit) / 1000 - 1.0, oneRange.upper);
         ui_->customPlot->xAxis->setRange(volumeAxisRect_->axis(QCPAxis::atBottom)->range());
      }
      else {
         ui_->customPlot->xAxis->setRange(newRange);
      }
   }

   LoadAdditionalPoints(newRange);
   pickTicketDateFormat(newRange);
   rescalePlot();
}

void ChartWidget::InitializeCustomPlot()
{
   QBrush bgBrush(BACKGROUND_COLOR);
   ui_->customPlot->setBackground(bgBrush);

   //add title
   title_ = new QCPTextElement(ui_->customPlot);
   title_->setTextColor(FOREGROUND_COLOR);
   title_->setFont(QFont(QStringLiteral("sans"), 12));
   ui_->customPlot->plotLayout()->insertRow(0);
   ui_->customPlot->plotLayout()->addElement(0, 0, title_);
   //add info
   info_ = new QCPTextElement(ui_->customPlot);
   info_->setTextColor(FOREGROUND_COLOR);
   info_->setFont(QFont(QStringLiteral("sans"), 10));
   info_->setTextFlags(Qt::AlignLeft | Qt::AlignVCenter);
   ui_->customPlot->plotLayout()->insertRow(1);
   ui_->customPlot->plotLayout()->addElement(1, 0, info_);

   // create candlestick chart:
   candlesticksChart_ = new QCPFinancial(ui_->customPlot->xAxis, ui_->customPlot->yAxis2);
   candlesticksChart_->setName(tr("Candlestick"));
   candlesticksChart_->setChartStyle(QCPFinancial::csCandlestick);
   candlesticksChart_->setTwoColored(true);
   candlesticksChart_->setBrushPositive(c_greenColor);
   candlesticksChart_->setBrushNegative(c_redColor);
   candlesticksChart_->setPenPositive(QPen(c_greenColor));
   candlesticksChart_->setPenNegative(QPen(c_redColor));

   ui_->customPlot->axisRect()->axis(QCPAxis::atLeft)->setVisible(false);
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setVisible(true);
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setBasePen(QPen(FOREGROUND_COLOR));
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setTickPen(QPen(FOREGROUND_COLOR));
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setSubTickPen(QPen(FOREGROUND_COLOR));
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setTickLabelColor(FOREGROUND_COLOR);
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setTickLength(0, 8);
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setSubTickLength(0, 4);
   ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->setNumberFormat(QStringLiteral("f"));
   ui_->customPlot->axisRect()->axis(QCPAxis::atBottom)->grid()->setPen(Qt::NoPen);

   // create bottom axis rect for volume bar chart:
   volumeAxisRect_ = new QCPAxisRect(ui_->customPlot);
   ui_->customPlot->plotLayout()->addElement(3, 0, volumeAxisRect_);
   volumeAxisRect_->setMaximumSize(QSize(QWIDGETSIZE_MAX, 100));
   volumeAxisRect_->axis(QCPAxis::atBottom)->setLayer(QStringLiteral("axes"));
   volumeAxisRect_->axis(QCPAxis::atBottom)->grid()->setLayer(QStringLiteral("grid"));
   // bring bottom and main axis rect closer together:
   ui_->customPlot->plotLayout()->setRowSpacing(0);
   volumeAxisRect_->setAutoMargins(QCP::msLeft|QCP::msRight|QCP::msBottom);
   volumeAxisRect_->setMargins(QMargins(0, 0, 0, 0));
   // create two bar plottables, for positive (green) and negative (red) volume bars:
   ui_->customPlot->setAutoAddPlottableToLegend(false);

   volumeChart_ = new QCPBars(volumeAxisRect_->axis(QCPAxis::atBottom), volumeAxisRect_->axis(QCPAxis::atRight));
   volumeChart_->setPen(QPen(VOLUME_COLOR));
   volumeChart_->setBrush(VOLUME_COLOR);

   volumeAxisRect_->axis(QCPAxis::atLeft)->setVisible(false);
   volumeAxisRect_->axis(QCPAxis::atRight)->setVisible(true);
   volumeAxisRect_->axis(QCPAxis::atRight)->setBasePen(QPen(FOREGROUND_COLOR));
   volumeAxisRect_->axis(QCPAxis::atRight)->setTickPen(QPen(FOREGROUND_COLOR));
   volumeAxisRect_->axis(QCPAxis::atRight)->setSubTickPen(QPen(FOREGROUND_COLOR));
   volumeAxisRect_->axis(QCPAxis::atRight)->setTickLabelColor(FOREGROUND_COLOR);
   volumeAxisRect_->axis(QCPAxis::atRight)->setTickLength(0, 8);
   volumeAxisRect_->axis(QCPAxis::atRight)->setSubTickLength(0, 4);
   volumeAxisRect_->axis(QCPAxis::atRight)->ticker()->setTickCount(1);
   volumeAxisRect_->axis(QCPAxis::atRight)->setTickLabelFont(ui_->customPlot->axisRect()->axis(QCPAxis::atRight)->labelFont());

   volumeAxisRect_->axis(QCPAxis::atBottom)->setBasePen(QPen(FOREGROUND_COLOR));
   volumeAxisRect_->axis(QCPAxis::atBottom)->setTickPen(QPen(FOREGROUND_COLOR));
   volumeAxisRect_->axis(QCPAxis::atBottom)->setSubTickPen(QPen(FOREGROUND_COLOR));
   volumeAxisRect_->axis(QCPAxis::atBottom)->setTickLength(0, 8);
   volumeAxisRect_->axis(QCPAxis::atBottom)->setSubTickLength(0, 4);
   volumeAxisRect_->axis(QCPAxis::atBottom)->setTickLabelColor(FOREGROUND_COLOR);
   volumeAxisRect_->axis(QCPAxis::atBottom)->grid()->setPen(Qt::NoPen);

   // interconnect x axis ranges of main and bottom axis rects:
   connect(ui_->customPlot->xAxis, SIGNAL(rangeChanged(QCPRange))
           , volumeAxisRect_->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));


   connect(volumeAxisRect_->axis(QCPAxis::atBottom),
      qOverload<const QCPRange&, const QCPRange&>(&QCPAxis::rangeChanged),
      this,
      &ChartWidget::OnVolumeAxisRangeChanged);

   // configure axes of both main and bottom axis rect:
   dateTimeTicker->setDateTimeSpec(Qt::UTC);
   dateTimeTicker->setDateTimeFormat(QStringLiteral("dd/MM/yy\nHH:mm"));
   dateTimeTicker->setTickCount(17);
   volumeAxisRect_->axis(QCPAxis::atBottom)->setTicker(dateTimeTicker);
   //volumeAxisRect_->axis(QCPAxis::atBottom)->setTickLabelRotation(10);
   volumeAxisRect_->axis(QCPAxis::atBottom)->setTickLabelFont(QFont(QStringLiteral("Arial"), 9));
   ui_->customPlot->xAxis->setBasePen(Qt::NoPen);
   ui_->customPlot->xAxis->setTickLabels(false);
   ui_->customPlot->xAxis->setTicks(false); // only want vertical grid in main axis rect, so hide xAxis backbone, ticks, and labels
   ui_->customPlot->xAxis->setTicker(dateTimeTicker);
   ui_->customPlot->rescaleAxes();
   ui_->customPlot->xAxis->scaleRange(1.025, ui_->customPlot->xAxis->range().center());
   ui_->customPlot->yAxis->scaleRange(1.1, ui_->customPlot->yAxis->range().center());

   // make axis rects' left side line up:
   QCPMarginGroup *group = new QCPMarginGroup(ui_->customPlot);
   ui_->customPlot->axisRect()->setMarginGroup(QCP::msLeft|QCP::msRight, group);
   volumeAxisRect_->setMarginGroup(QCP::msLeft|QCP::msRight, group);

   //make draggable horizontally
   ui_->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

   connect(ui_->customPlot, &QCustomPlot::mouseMove, this, &ChartWidget::OnPlotMouseMove);
   connect(ui_->customPlot, &QCustomPlot::mousePress, this, &ChartWidget::OnMousePressed);
   connect(ui_->customPlot, &QCustomPlot::mouseRelease, this, &ChartWidget::OnMouseReleased);

   // make zoomable
}
