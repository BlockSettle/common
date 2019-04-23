#include "ChatClientUsersModel.h"
#include <algorithm>
#include <QColor>

ChatClientDataModel::ChatClientDataModel(std::shared_ptr<TreeItem> root, QObject * parent)
    : QAbstractItemModel(parent)
    , root_(root)
{
   connect(root_.get(), &TreeItem::beforeUpdate, this, &ChatClientDataModel::onBeforeUpdate);
   connect(root_.get(), &TreeItem::afterUpdate, this, &ChatClientDataModel::onAfterUpdate);
   connect(root_.get(), &TreeItem::beforeClean, this, &ChatClientDataModel::onBeforeClean);
   connect(root_.get(), &TreeItem::afterClean, this, &ChatClientDataModel::onAfterClean);
   connect(root_.get(), &TreeItem::itemChanged, this, &ChatClientDataModel::onItemChanged);


}

QModelIndex ChatClientDataModel::index(int row, int column, const QModelIndex &parent) const
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

QModelIndex ChatClientDataModel::parent(const QModelIndex &child) const
{
   if (!child.isValid())
      return QModelIndex();

   TreeItem *childItem = static_cast<TreeItem*>(child.internalPointer());
   TreeItem *parentItem = childItem->getParent();

   if (root_ && parentItem == root_.get())
      return QModelIndex();

   return createIndex(parentItem->selfIndex(), 0, parentItem);
}

int ChatClientDataModel::rowCount(const QModelIndex &parent) const
{
   TreeItem *parentItem;
   if (parent.column() > 0) {
      return 0;
   }

   if (!parent.isValid()) {
      parentItem = root_.get();
   } else {
      parentItem = static_cast<TreeItem*>(parent.internalPointer());
   }

   return static_cast<int>(parentItem->getChildren().size());
}

int ChatClientDataModel::columnCount(const QModelIndex &parent) const
{
   return 1;
}

QVariant ChatClientDataModel::data(const QModelIndex &index, int role) const
{
   if (!index.isValid()) //Applicable for RootNode
      return QVariant();

   TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

   switch (item->getType()) {
      case TreeItem::NodeType::CategoryNode:
         return  categoryNodeData(item, role);
      case TreeItem::NodeType::SearchElement:
      case TreeItem::NodeType::RoomsElement:
      case TreeItem::NodeType::ContactsElement:
      case TreeItem::NodeType::AllUsersElement:
         return categoryElementData(item, role);
      default:
         return QVariant();
   }

}

void ChatClientDataModel::onBeforeUpdate()
{
   emit layoutAboutToBeChanged();
}

void ChatClientDataModel::onAfterUpdate()
{
   emit layoutChanged();
}

void ChatClientDataModel::onBeforeClean()
{
   beginResetModel();
}

void ChatClientDataModel::onAfterClean()
{
   endResetModel();
}

void ChatClientDataModel::onItemChanged(TreeItem *item)
{
   QModelIndex index = createIndex(item->selfIndex(), 0, item);
   emit dataChanged(index, index);
}

QVariant ChatClientDataModel::categoryNodeData(const TreeItem* item, int role) const
{
   if (role != Qt::DisplayRole) {
      return QVariant();
   }

   switch(item->getAcceptType()){
      case TreeItem::NodeType::RoomsElement:
         return QLatin1String("Chat rooms");
      case TreeItem::NodeType::ContactsElement:
         return QLatin1String("Contacts");
      case TreeItem::NodeType::AllUsersElement:
         return QLatin1String("AllUsers");
      case TreeItem::NodeType::SearchElement:
         return QLatin1String("Search");
      default:
         return QLatin1String("<unknown>");
   }
}

QVariant ChatClientDataModel::categoryElementData(TreeItem * item, int role) const
{
   CategoryElement* element = static_cast<CategoryElement*>(item);
   switch(element->getType()){
      case TreeItem::NodeType::RoomsElement:{
         std::shared_ptr<Chat::RoomData> data = std::dynamic_pointer_cast<Chat::RoomData>(element->getDataObject());
         if (role == Qt::DisplayRole){
            return data->getTitle();
         } else if (role == Qt::TextColorRole) {
            return QColor(0x00c8f8);
         }
      } break;
      case TreeItem::NodeType::ContactsElement:{
         std::shared_ptr<Chat::ContactRecordData> data = std::dynamic_pointer_cast<Chat::ContactRecordData>(element->getDataObject());
         if (role == Qt::DisplayRole){
            return data->getContactId();
         } else if (role == Qt::TextColorRole){
            ChatContactElement* contact = static_cast<ChatContactElement*>(element);
            switch (data->getContactStatus()) {
               case Chat::ContactStatus::Accepted:
                  if (contact->getOnlineStatus() == ChatContactElement::OnlineStatus::Online){
                     return QColor(0x00c8f8);
                  }
                  return QColor(Qt::white);
               case Chat::ContactStatus::Rejected:
                  return QColor(Qt::red);
               case Chat::ContactStatus::Incoming:
                  return QColor(0xffa834);
               case Chat::ContactStatus::Outgoing:
                  return QColor(0xA0BC5D);

            }
         }
      } break;
      case TreeItem::NodeType::AllUsersElement:{
         std::shared_ptr<Chat::UserData> data = std::dynamic_pointer_cast<Chat::UserData>(element->getDataObject());
         if (role == Qt::DisplayRole){
            return data->getUserId();
         }
      } break;
      default:
         return QLatin1String("<unknown>");
   }
   return QVariant();
}

Qt::ItemFlags ChatClientDataModel::flags(const QModelIndex &index) const
{
   if (!index.isValid()) {
      return 0;
   }

   Qt::ItemFlags current_flags = QAbstractItemModel::flags(index);

   TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

   switch (item->getType()) {
      case TreeItem::NodeType::CategoryNode:
         if (current_flags.testFlag(Qt::ItemIsSelectable)){
            current_flags.setFlag(Qt::ItemIsSelectable, false);
         }
         break;
      case TreeItem::NodeType::SearchElement:
      case TreeItem::NodeType::RoomsElement:
      case TreeItem::NodeType::ContactsElement:
      case TreeItem::NodeType::AllUsersElement:
         if (!current_flags.testFlag(Qt::ItemIsEnabled)){
            current_flags.setFlag(Qt::ItemIsEnabled);
         }
         break;
      default:
         break;
   }


   return current_flags;
}
