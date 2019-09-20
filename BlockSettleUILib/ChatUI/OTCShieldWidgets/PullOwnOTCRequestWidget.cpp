#include "PullOwnOTCRequestWidget.h"

#include "ui_PullOwnOTCRequestWidget.h"

using namespace bs::network;

PullOwnOTCRequestWidget::PullOwnOTCRequestWidget(QWidget* parent)
   : OTCWindowsAdapterBase(parent)
   , ui_{new Ui::PullOwnOTCRequestWidget()}
{
   ui_->setupUi(this);

   connect(ui_->pushButtonPull, &QPushButton::clicked, this, &PullOwnOTCRequestWidget::requestPulled);
}

PullOwnOTCRequestWidget::~PullOwnOTCRequestWidget() = default;

void PullOwnOTCRequestWidget::setOffer(const bs::network::otc::Offer &offer)
{
   ui_->labelSide->setText(QString::fromStdString(otc::toString(offer.ourSide)));
   ui_->labelPrice->setText(QString::number(offer.price));
   ui_->labelQuantity->setText(QString::number(offer.amount));
   updateVisibility(false);
}

void PullOwnOTCRequestWidget::setRequest(const bs::network::otc::Request &request)
{
   ui_->labelSide->setText(QString::fromStdString(otc::toString(request.requestorSide)));
   ui_->labelRange->setText(QString::fromStdString(otc::toString(request.rangeType)));
   updateVisibility(true);
}

void PullOwnOTCRequestWidget::updateVisibility(bool isPublic)
{
   ui_->widgetQuantity->setVisible(!isPublic);
   ui_->widgetPrice->setVisible(!isPublic);
}
