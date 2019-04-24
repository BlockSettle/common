#include "ChatClientDataModel.h"
#include <algorithm>


ChatClientDataModel::ChatClientDataModel(QObject * parent)
    : QAbstractItemModel(parent)
    , root_(std::make_shared<RootItem>())
{
   connect(root_.get(), &TreeItem::itemChanged, this, &ChatClientDataModel::onItemChanged);

   root_->insertItem(new CategoryItem(TreeItem::NodeType::RoomsElement));
   root_->insertItem(new CategoryItem(TreeItem::NodeType::ContactsElement));

}

void ChatClientDataModel::clearModel()
{
   beginResetModel();
   root_->clear();
   endResetModel();
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
   switch (role) {
      case ItemTypeRole:
         return QVariant::fromValue(item->getType());
      case RoomTitleRole:
      case RoomIdRole:
         return roomData(item, role);
      case ContactIdRole:
      case ContactOnlineStatusRole:
         return contactData(item, role);
      case UserIdRole:
      case UserOnlineStatusRole:
         return userData(item, role);
      default:
         return QVariant();

   }

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

void ChatClientDataModel::onItemChanged(TreeItem *item)
{
   QModelIndex index = createIndex(item->selfIndex(), 0, item);
   emit dataChanged(index, index);
}

QVariant ChatClientDataModel::roomData(const TreeItem *item, int role) const
{
   if (item->getType() == TreeItem::NodeType::RoomsElement) {
      const ChatRoomElement * room_element = static_cast<const ChatRoomElement*>(item);
      auto room = room_element->getRoomData();

      if (!room) {
         return QVariant();
      }

      switch (role) {
         case RoomTitleRole:
            return room->getTitle();
         case RoomIdRole:
            return room->getId();
         default:
            return QVariant();
      }
   }
   return QVariant();
}

QVariant ChatClientDataModel::contactData(const TreeItem *item, int role) const
{
   if (item->getType() == TreeItem::NodeType::ContactsElement) {
      const ChatContactElement * contact_element = static_cast<const ChatContactElement*>(item);
      auto contact = contact_element->getContactData();

      if (!contact) {
         return QVariant();
      }

      switch (role) {
         case ContactIdRole:
            return contact->getContactId();
         case ContactOnlineStatusRole:
            return QVariant::fromValue(contact_element->getOnlineStatus());
         default:
            return QVariant();
      }
   }
}

QVariant ChatClientDataModel::userData(const TreeItem *item, int role) const
{
   if (item->getType() == TreeItem::NodeType::AllUsersElement) {
      const ChatUserElement * user_element = static_cast<const ChatUserElement*>(item);
      auto user = user_element->getUserData();

      if (!user) {
         return QVariant();
      }

      switch (role) {
         case UserIdRole:
            return user->getUserId();
         case UserOnlineStatusRole:
            return QVariant::fromValue(user->getUserStatus());
         default:
            return QVariant();
      }
   }
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
         }
      } break;
      case TreeItem::NodeType::ContactsElement:{
         std::shared_ptr<Chat::ContactRecordData> data = std::dynamic_pointer_cast<Chat::ContactRecordData>(element->getDataObject());
         if (role == Qt::DisplayRole){
            return data->getContactId();
         } /*else if (role == Qt::TextColorRole){
            ChatContactElement* contact = static_cast<ChatContactElement*>(element);
            switch (data->getContactStatus()) {
               case Chat::ContactStatus::Accepted:
                  if (contact->getOnlineStatus() == ChatContactElement::OnlineStatus::Online){
                     return 0x00c8f8;
                  }
                  return 0xffffff;
               case Chat::ContactStatus::Rejected:
                  return 0xff0000;
               case Chat::ContactStatus::Incoming:
                  return 0xffa834;
               case Chat::ContactStatus::Outgoing:
                  return 0xA0BC5D;

            }
         }*/

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
