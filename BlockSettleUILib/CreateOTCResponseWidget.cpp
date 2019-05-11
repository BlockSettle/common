#include "CreateOTCResponseWidget.h"

#include "ui_CreateOTCResponseWidget.h"

CreateOTCResponseWidget::CreateOTCResponseWidget(QWidget* parent)
   : QWidget{parent}
   , ui_{new Ui::CreateOTCResponseWidget{}}
{
   ui_->setupUi(this);

   ui_->widgetPriceRange->SetRange(3000, 4000);

   connect(ui_->pushButtonSubmit, &QPushButton::pressed, this, &CreateOTCResponseWidget::ResponseCreated);
}

CreateOTCResponseWidget::~CreateOTCResponseWidget() = default;

void CreateOTCResponseWidget::SetSide(const bs::network::Side::Type& side)
{
   if (side == bs::network::Side::Sell) {
      ui_->labelSide->setText(tr("Sell"));
   } else {
      ui_->labelSide->setText(tr("Buy"));
   }
}

void CreateOTCResponseWidget::SetRange(const bs::network::OTCRangeID& range)
{
   ui_->widgetAmountRange->setEnabled(true);

   switch (range) {
   case bs::network::OTCRangeID::Range1_5:
      ui_->widgetAmountRange->SetRange(1, 5);
      break;
   case bs::network::OTCRangeID::Range5_10:
      ui_->widgetAmountRange->SetRange(5, 10);
      break;
   case bs::network::OTCRangeID::Range10_50:
      ui_->widgetAmountRange->SetRange(10, 50);
      break;
   case bs::network::OTCRangeID::Range50_100:
      ui_->widgetAmountRange->SetRange(50, 100);
      break;
   case bs::network::OTCRangeID::Range100_250:
      ui_->widgetAmountRange->SetRange(100, 250);
      break;
   case bs::network::OTCRangeID::Range250plus:
   default:
      ui_->widgetAmountRange->SetRange(250, 1000);
      ui_->widgetAmountRange->setEnabled(false);
      break;
   }
}


bs::network::OTCPriceRange CreateOTCResponseWidget::GetResponsePriceRange() const
{
   bs::network::OTCPriceRange range;

   range.lower = ui_->widgetPriceRange->GetLowerValue();
   range.upper = ui_->widgetPriceRange->GetUpperValue();

   return range;
}

bs::network::OTCQuantityRange CreateOTCResponseWidget::GetResponseQuantityRange() const
{
   bs::network::OTCQuantityRange range;

   range.lower = ui_->widgetAmountRange->GetLowerValue();
   range.upper = ui_->widgetAmountRange->GetUpperValue();

   return range;
}
