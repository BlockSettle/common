#include "ChatMessageDelegate.h"
#include "ChatMessageWidget.h"
#include "ChatMessagesModel.h"
#include <QDateTime>
#include <QPainter>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLabel>
ChatMessageDelegate::ChatMessageDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
   widget = new QWidget();
   widget->setObjectName(tr("chatMessageWidget"));
   QVBoxLayout* vbox = new QVBoxLayout(widget);
   QHBoxLayout* hbox = new QHBoxLayout(widget);
   
   user = new QLabel(widget);
   datetime = new QLabel(widget);
   content = new QLabel(widget);
//   le1->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
//   le2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
//   te->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
   
   vbox->addLayout(hbox);
   hbox->addWidget(user);
   hbox->addWidget(datetime);
   
   vbox->addWidget(content);
   vbox->setSizeConstraint(QLayout::SizeConstraint::SetMinimumSize);
}

void ChatMessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
   QRect rect = option.rect;
   QPalette palette;
   ChatMessageWidget w;
   w.setDateTime(index.data(static_cast<int>(ChatMessagesModel::Role::DateTimeRole)).toDateTime());
   w.setUsername(index.data(static_cast<int>(ChatMessagesModel::Role::UserNameRole)).toString());
   w.setContent(index.data(static_cast<int>(ChatMessagesModel::Role::MessageDataRole)).toString());
   w.resize(option.rect.size());
   datetime->setText(index.data(static_cast<int>(ChatMessagesModel::Role::DateTimeRole)).toDateTime().toString());
   user->setText(index.data(static_cast<int>(ChatMessagesModel::Role::UserNameRole)).toString());
   content->setText(index.data(static_cast<int>(ChatMessagesModel::Role::MessageDataRole)).toString());
   //painter->end();
   widget->resize(option.rect.size());
   painter->setBrush(QBrush(Qt::blue, Qt::SolidPattern));
   if( QStyle::State_Selected == ( option.state & QStyle::State_Selected ) ) {
           palette.setBrush( QPalette::Window, QBrush( QColor( Qt::lightGray ) ) );
           widget->setStyle(qobject_cast<QStyle*>(option.styleObject));
           //w->setFocus(Qt::FocusReason::ActiveWindowFocusReason);
       } else
           palette.setBrush(QPalette::Window, QBrush( QColor( Qt::transparent ) ) );
       widget->setPalette( palette );
   
   painter->save();
   painter->translate(rect.topLeft());
   //w.render(painter);//, QPoint(option.rect.x(), option.rect.y()), QRegion(0, 0, option.rect.width(), option.rect.height()), QWidget::RenderFlag::DrawChildren);
   widget->render(painter);
   painter->restore();
   //QStyledItemDelegate::paint(painter, option, index);
}

QSize ChatMessageDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
   ChatMessageWidget w;
   w.setDateTime(index.data(static_cast<int>(ChatMessagesModel::Role::DateTimeRole)).toDateTime());
   w.setUsername(index.data(static_cast<int>(ChatMessagesModel::Role::UserNameRole)).toString());
   w.setContent(index.data(static_cast<int>(ChatMessagesModel::Role::MessageDataRole)).toString());
   w.update();
   datetime->setText(index.data(static_cast<int>(ChatMessagesModel::Role::DateTimeRole)).toDateTime().toString());
   user->setText(index.data(static_cast<int>(ChatMessagesModel::Role::UserNameRole)).toString());
   content->setText(index.data(static_cast<int>(ChatMessagesModel::Role::MessageDataRole)).toString());
   widget->update();
   return  widget->sizeHint();
}
