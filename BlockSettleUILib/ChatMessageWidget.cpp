#include "ChatMessageWidget.h"
#include "ui_ChatMessageWidget.h"
#include <QDateTime>

ChatMessageWidget::ChatMessageWidget(QWidget *parent) :
   QWidget(parent),
   ui(new Ui::ChatMessageWidget)
{
   ui->setupUi(this);
}

ChatMessageWidget::~ChatMessageWidget()
{
    delete ui;
}

void ChatMessageWidget::setDateTime(const QDateTime& dt)
{
   ui->dateTimeLabel->setText(dt.toString());
}

void ChatMessageWidget::setUsername(const QString& userName)
{
    ui->userLabel->setText(userName);
}

void ChatMessageWidget::setContent(const QString& content)
{
    ui->messageContent->setText(content);
}
