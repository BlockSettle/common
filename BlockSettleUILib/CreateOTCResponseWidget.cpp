#include "CreateOTCResponseWidget.h"

#include "ChatProtocol/ChatUtils.h"
#include "ui_CreateOTCResponseWidget.h"

CreateOTCResponseWidget::CreateOTCResponseWidget(QWidget* parent)
   : QWidget{parent}
   , ui_{new Ui::CreateOTCResponseWidget{}}
{
   ui_->setupUi(this);

   ui_->widgetPriceRange->SetRange(3000, 4000);

//   connect(ui_->pushButtonSubmit, &QPushButton::pressed, this, &CreateOTCResponseWidget::OnCreateResponse);
//   connect(ui_->pushButtonPull, &QPushButton::pressed, this, &CreateOTCResponseWidget::ResponseRejected);
}

CreateOTCResponseWidget::~CreateOTCResponseWidget() = default;
