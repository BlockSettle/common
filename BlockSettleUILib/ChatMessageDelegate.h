#ifndef CHATMESSAGEDELEGATE_H
#define CHATMESSAGEDELEGATE_H

#include <QStyledItemDelegate>
#include <QLabel>

class ChatMessageDelegate : public QStyledItemDelegate
{
   Q_OBJECT
public:
   explicit ChatMessageDelegate(QObject *parent = nullptr);
   
signals:
   
public slots:
   
   // QAbstractItemDelegate interface
public:
   void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
   
   // QAbstractItemDelegate interface
public:
   QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
private:
   QWidget* widget;
   QLabel* user;
   QLabel* datetime;
   QLabel* content;
};

#endif // CHATMESSAGEDELEGATE_H
