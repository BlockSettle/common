#include "ChatClientUsersModel.h"
#include <algorithm>
ChatClientUsersModel::ChatClientUsersModel(std::shared_ptr<TreeItem> root, QObject * parent)
    : QAbstractItemModel(parent)
    , root_(root)
{
   connect(root_.get(), &TreeItem::beforeUpdate, this, &ChatClientUsersModel::onBeforeUpdate);
   connect(root_.get(), &TreeItem::afterUpdate, this, &ChatClientUsersModel::onAfterUpdate);

}

QModelIndex ChatClientUsersModel::index(int row, int column, const QModelIndex &parent) const
{
   if (!hasIndex(row, column, parent)){
      return QModelIndex();
   }

   TreeItem* parentItem;

   if (!parent.isValid()){
      parentItem = root_.get();
   } else {
      parentItem = static_cast<TreeItem*>(parent.internalPointer());
   }
   //std::list<TreeItem*>::iterator it = parentItem->getChildren().begin();

   TreeItem * childItem = parentItem->getChildren().at(row);

   if (childItem){
      return createIndex(row, column, childItem);
   }

   return QModelIndex();
}

QModelIndex ChatClientUsersModel::parent(const QModelIndex &child) const
{
   if (!child.isValid())
           return QModelIndex();

       TreeItem *childItem = static_cast<TreeItem*>(child.internalPointer());
       TreeItem *parentItem = childItem->getParent();

       if (parentItem == root_.get())
           return QModelIndex();

       return createIndex(parentItem->selfIndex(), 0, parentItem);
}

int ChatClientUsersModel::rowCount(const QModelIndex &parent) const
{
   TreeItem *parentItem;
       if (parent.column() > 0)
           return 0;

       if (!parent.isValid())
           parentItem = root_.get();
       else
           parentItem = static_cast<TreeItem*>(parent.internalPointer());

       return static_cast<int>(parentItem->getChildren().size());
}

int ChatClientUsersModel::columnCount(const QModelIndex &parent) const
{
   return 1;
}

QVariant ChatClientUsersModel::data(const QModelIndex &index, int role) const
{
   if (!index.isValid()) //Applicable for RootNode
           return QVariant();

       TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

       switch (item->getType()) {
         case TreeItem::NodeType::CategoryNode:
             return  categoryNodeData(item, role);
         case TreeItem::NodeType::SearchCategory:
         case TreeItem::NodeType::RoomsCategory:
         case TreeItem::NodeType::ContactsCategory:
         case TreeItem::NodeType::AllUsersCategory:
             return categoryElementData(item, role);
          default:
             return QVariant();
       }

}

void ChatClientUsersModel::onBeforeUpdate()
{
   emit layoutAboutToBeChanged();
}

void ChatClientUsersModel::onAfterUpdate()
{
   emit layoutChanged();
}

QVariant ChatClientUsersModel::categoryNodeData(const TreeItem* item, int role) const
{
   if (role != Qt::DisplayRole)
       return QVariant();

   switch(item->getAcceptType()){
      case TreeItem::NodeType::RoomsCategory:
         return QLatin1String("Chat rooms");
      case TreeItem::NodeType::ContactsCategory:
         return QLatin1String("Contacts");
      case TreeItem::NodeType::AllUsersCategory:
         return QLatin1String("AllUsers");
      case TreeItem::NodeType::SearchCategory:
         return QLatin1String("Search");
      default:
         return QLatin1String("<unknown>");
   }
}

QVariant ChatClientUsersModel::categoryElementData(TreeItem * item, int role) const
{
   if (role != Qt::DisplayRole)
       return QVariant();

   CategoryElement* element = static_cast<CategoryElement*>(item);

   switch(element->getAcceptType()){
      case TreeItem::NodeType::ChatRoomNode:{
         std::shared_ptr<Chat::RoomData> data = std::dynamic_pointer_cast<Chat::RoomData>(element->getDataObject());
         return data->getTitle();
      }
      case TreeItem::NodeType::ChatContactNode:{
         std::shared_ptr<Chat::ContactRecordData> data = std::dynamic_pointer_cast<Chat::ContactRecordData>(element->getDataObject());
         return data->getContactId();
      }
      case TreeItem::NodeType::ChatUserNode:{
         std::shared_ptr<Chat::UserData> data = std::dynamic_pointer_cast<Chat::UserData>(element->getDataObject());
         return data->getUserId();
      }
      default:
         return QLatin1String("<unknown>");
   }
}
