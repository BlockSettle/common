#include "ChatClientUsersViewItemDelegate.h"
#include "ChatClientDataModel.h"

using NodeType = TreeItem::NodeType;
using Role = ChatClientDataModel::Role;

ChatClientUsersViewItemDelegate::ChatClientUsersViewItemDelegate(QObject *parent)
: QStyledItemDelegate(parent)
{

}

void ChatClientUsersViewItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   const NodeType nodeType =
            qvariant_cast<NodeType>(index.data(Role::ItemTypeRole));

   switch (nodeType) {
      case NodeType::CategoryNode:
         return paintCategoryNode(painter, option, index);
      case NodeType::RoomsElement:
         return paintRoomsElement(painter, option, index);
      case NodeType::ContactsElement:
         return paintContactsElement(painter, option, index);
      case NodeType::AllUsersElement:
      case NodeType::SearchElement:
         return paintUserElement(painter, option, index);
      default:
         return QStyledItemDelegate::paint(painter, option, index);
   }


}

void ChatClientUsersViewItemDelegate::paintCategoryNode(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   itemOption.palette.setColor(QPalette::Text, Qt::gray);
   itemOption.palette.setColor(QPalette::HighlightedText, Qt::gray);
   switch (index.data(Role::ItemAcceptTypeRole).value<NodeType>()){
      case TreeItem::NodeType::SearchElement:
         itemOption.text = QLatin1String("Search");
         break;
      case TreeItem::NodeType::RoomsElement:
         itemOption.text = QLatin1String("Chat rooms");
         break;
      case TreeItem::NodeType::ContactsElement:
         itemOption.text = QLatin1String("Contacts");
         break;
      case TreeItem::NodeType::AllUsersElement:
         itemOption.text = QLatin1String("Users");
         break;
      default:
         itemOption.text = QLatin1String("<unknown>");

   }

   QStyledItemDelegate::paint(painter, itemOption, index);

}

void ChatClientUsersViewItemDelegate::paintRoomsElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   //itemOption.palette.setColor(QPalette::Text, Qt::cyan);
   //itemOption.palette.setColor(QPalette::Highlight, Qt::cyan);
   if (index.data(Role::ItemTypeRole).value<NodeType>() == NodeType::RoomsElement){
      itemOption.text = index.data(Role::RoomIdRole).toString();
   } else {
      itemOption.text = QLatin1String("<unknown>");
   }
   QStyledItemDelegate::paint(painter, itemOption, index);

}

void ChatClientUsersViewItemDelegate::paintContactsElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   itemOption.palette.setColor(QPalette::Text, Qt::cyan);
   itemOption.palette.setColor(QPalette::Highlight, Qt::blue);
   if (index.data(Role::ItemTypeRole).value<NodeType>() == NodeType::ContactsElement){
      itemOption.text = index.data(Role::ContactIdRole).toString();
   } else {
      itemOption.text = QLatin1String("<unknown>");
   }
   QStyledItemDelegate::paint(painter, itemOption, index);
}

void ChatClientUsersViewItemDelegate::paintUserElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   itemOption.palette.setColor(QPalette::Text, Qt::cyan);
   itemOption.palette.setColor(QPalette::Highlight, Qt::blue);
   if (index.data(Role::ItemTypeRole).value<NodeType>() == NodeType::AllUsersElement){
      itemOption.text = index.data(Role::UserIdRole).toString();
   } else if (index.data(Role::ItemTypeRole).value<NodeType>() == NodeType::SearchElement) {
      itemOption.text = index.data(Role::UserIdRole).toString();
   } else {
      itemOption.text = QLatin1String("<unknown>");
   }
   QStyledItemDelegate::paint(painter, itemOption, index);
}
