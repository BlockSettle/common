#include "BlockDetailsWidget.h"
#include "ui_BlockDetailsWidget.h"
#include <QIcon>


BlockDetailsWidget::BlockDetailsWidget(QWidget *parent) :
    QWidget(parent),
    ui_(new Ui::BlockDetailsWidget())
{
    ui_->setupUi(this);

    QIcon btcIcon(QLatin1String(":/FULL_LOGO"));

    ui_->icon->setPixmap(btcIcon.pixmap(80, 80));
}

BlockDetailsWidget::~BlockDetailsWidget() = default;
