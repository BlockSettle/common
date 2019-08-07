#include "CreateOTCRequestWidget.h"

#include "OtcTypes.h"
#include "ui_CreateOTCRequestWidget.h"

#include <QComboBox>
#include <QPushButton>

CreateOTCRequestWidget::CreateOTCRequestWidget(QWidget* parent)
   : QWidget{parent}
   , ui_{new Ui::CreateOTCRequestWidget{}}
{
   ui_->setupUi(this);

   for (int i = 0; i < int(bs::network::otc::RangeType::Count); ++i) {
      auto range = bs::network::otc::RangeType(i);
      ui_->comboBoxRange->addItem(QString::fromStdString(bs::network::otc::toString(range)), i);
   }

   connect(ui_->pushButtonBuy, &QPushButton::clicked, this, &CreateOTCRequestWidget::onBuyClicked);
   connect(ui_->pushButtonSell, &QPushButton::clicked, this, &CreateOTCRequestWidget::onSellClicked);
   connect(ui_->pushButtonSubmit, &QPushButton::pressed, this, &CreateOTCRequestWidget::requestCreated);

   onSellClicked();
}

bs::network::otc::Offer CreateOTCRequestWidget::offer() const
{
   bs::network::otc::Offer result;
   result.ourSide = ui_->pushButtonSell->isChecked() ? bs::network::otc::Side::Sell : bs::network::otc::Side::Buy;
   result.price = 1;
   result.amount = 1;
   return result;
}

CreateOTCRequestWidget::~CreateOTCRequestWidget() = default;

void CreateOTCRequestWidget::onSellClicked()
{
   ui_->pushButtonSell->setChecked(true);
   ui_->pushButtonBuy->setChecked(false);
}

void CreateOTCRequestWidget::onBuyClicked()
{
   ui_->pushButtonSell->setChecked(false);
   ui_->pushButtonBuy->setChecked(true);
}
