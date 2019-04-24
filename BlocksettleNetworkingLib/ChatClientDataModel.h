#ifndef CHATCLIENTUSERSMODEL_H
#define CHATCLIENTUSERSMODEL_H

#include <memory>
#include <QAbstractItemModel>

#include "ChatClientTree/TreeObjects.h"

class ChatClientDataModel : public QAbstractItemModel
{
public:
   enum Role {
      ItemTypeRole = Qt::UserRole + 1,
      RoomTitleRole,
      RoomIdRole,
      ContactIdRole,
      ContactOnlineStatusRole,
      UserIdRole,
      UserOnlineStatusRole
   };

   ChatClientDataModel(QObject * parent = nullptr);

public:
   void clearModel();

   // QAbstractItemModel interface
public:
   QModelIndex index(int row, int column, const QModelIndex &parent) const override;
   QModelIndex parent(const QModelIndex &child) const override;
   int rowCount(const QModelIndex &parent) const override;
   int columnCount(const QModelIndex &parent) const override;
   QVariant data(const QModelIndex &index, int role) const override;
   Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
   void onItemChanged(TreeItem* item);
private:
   QVariant roomData(const TreeItem * item, int role) const;
   QVariant contactData(const TreeItem * item, int role) const;
   QVariant userData(const TreeItem * item, int role) const;
   QVariant categoryNodeData(const TreeItem *item, int role) const;
   QVariant categoryElementData(TreeItem *item, int role) const;

private:
   std::shared_ptr<RootItem> root_;
};













#endif // CHATCLIENTUSERSMODEL_H
