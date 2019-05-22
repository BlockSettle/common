#ifndef CHATCLIENTPROXYMODEL_H
#define CHATCLIENTPROXYMODEL_H

#include <QSortFilterProxyModel>

class ChatClientProxyModel : public QSortFilterProxyModel
{
public:
   ChatClientProxyModel(QObject *parent = 0);

protected:
   bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};
#endif // CHATCLIENTPROXYMODEL_H
