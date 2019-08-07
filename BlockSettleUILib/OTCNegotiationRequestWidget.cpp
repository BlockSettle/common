#include "OTCNegotiationRequestWidget.h"

#include "ui_OTCNegotiationCommonWidget.h"

#include <QComboBox>
#include <QPushButton>

OTCNegotiationRequestWidget::OTCNegotiationRequestWidget(QWidget* parent)
   : QWidget{parent}
   , ui_{new Ui::OTCNegotiationCommonWidget{}}
{
   ui_->setupUi(this);

   ui_->headerLabel->setText(tr("OTC Request Negotiation"));

   ui_->spinBoxOffer->setAccelerated(true);
   ui_->spinBoxQuantity->setAccelerated(true);

   ui_->pushButtonCancel->setText(tr("Reject"));
   ui_->pushButtonAccept->setText(tr("Respond"));
}

OTCNegotiationRequestWidget::~OTCNegotiationRequestWidget() = default;
