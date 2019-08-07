#include "PullOwnOTCRequestWidget.h"

#include "ChatProtocol/ChatUtils.h"
#include "ui_PullOwnOTCRequestWidget.h"

PullOwnOTCRequestWidget::PullOwnOTCRequestWidget(QWidget* parent)
   : QWidget(parent)
   , ui_{new Ui::PullOwnOTCRequestWidget()}
{
   ui_->setupUi(this);

   connect(ui_->pushButtonPull, &QPushButton::clicked, this, &PullOwnOTCRequestWidget::requestPulled);
}

PullOwnOTCRequestWidget::~PullOwnOTCRequestWidget() = default;
