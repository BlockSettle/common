#include "ChatClientUsersViewItemDelegate.h"
#include "ChatClientDataModel.h"
#include <QPainter>
#include <QLineEdit>

static const int kDotSize = 8;
static const QString kDotPathname = QLatin1String(":/ICON_DOT");

using NodeType = TreeItem::NodeType;
using Role = ChatClientDataModel::Role;
using OnlineStatus = ChatContactElement::OnlineStatus;
using ContactStatus = Chat::ContactStatus;

ChatClientUsersViewItemDelegate::ChatClientUsersViewItemDelegate(QObject *parent)
   : QStyledItemDelegate (parent)
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
      case NodeType::OTCReceivedResponsesElement:
         return paintOTCReceivedResponsesElement(painter, option, index);
      case NodeType::OTCSentResponsesElement:
         return paintOTCSentResponsesElement(painter, option, index);
      default:
         return QStyledItemDelegate::paint(painter, option, index);
   }
}

void ChatClientUsersViewItemDelegate::paintCategoryNode(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   itemOption.palette.setColor(QPalette::Text, itemStyle_.colorCategoryItem());
   switch (index.data(Role::ItemAcceptTypeRole).value<NodeType>()){
      case TreeItem::NodeType::SearchElement:
         itemOption.text = tr("Search");
         break;
      case TreeItem::NodeType::RoomsElement:
         itemOption.text = tr("Chat rooms");
         break;
      case TreeItem::NodeType::ContactsElement:
         itemOption.text = tr("Contacts");
         break;
      case TreeItem::NodeType::AllUsersElement:
         itemOption.text = tr("Users");
         break;
      case TreeItem::NodeType::OTCReceivedResponsesElement:
         itemOption.text = tr("Received Responses");
         break;
      case TreeItem::NodeType::OTCSentResponsesElement:
         itemOption.text = tr("Sent Responses");
         break;
      default:
         itemOption.text = tr("<unknown>");

   }

   QStyledItemDelegate::paint(painter, itemOption, index);

}

void ChatClientUsersViewItemDelegate::paintRoomsElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   if (index.data(Role::ItemTypeRole).value<NodeType>() != NodeType::RoomsElement){
      itemOption.text = QLatin1String("<unknown>");
      return QStyledItemDelegate::paint(painter, itemOption, index);
   }

   itemOption.palette.setColor(QPalette::Text, itemStyle_.colorRoom());
   bool newMessage = index.data(Role::ChatNewMessageRole).toBool();
   itemOption.text = index.data(Role::RoomTitleRole).toString();
   QStyledItemDelegate::paint(painter, itemOption, index);

   // draw dot
   if (newMessage) {
      QFontMetrics fm(itemOption.font, painter->device());
      auto textRect = fm.boundingRect(itemOption.rect, 0, itemOption.text);
      const QPixmap pixmap(kDotPathname);
      const QRect r(itemOption.rect.left() + textRect.width() + kDotSize,
                    itemOption.rect.top() + itemOption.rect.height() / 2 - kDotSize / 2 + 1,
                    kDotSize, kDotSize);
      painter->drawPixmap(r, pixmap, pixmap.rect());
   }
}

void ChatClientUsersViewItemDelegate::paintContactsElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   if (index.data(Role::ItemTypeRole).value<NodeType>() != NodeType::ContactsElement){
      itemOption.text = QLatin1String("<unknown>");
      return QStyledItemDelegate::paint(painter, itemOption, index);
   }

   ContactStatus contactStatus = index.data(Role::ContactStatusRole).value<ContactStatus>();
   OnlineStatus onlineStatus = index.data(Role::ContactOnlineStatusRole).value<OnlineStatus>();
   bool newMessage = index.data(Role::ChatNewMessageRole).toBool();
   itemOption.text = index.data(Role::ContactTitleRole).toString();

   switch (contactStatus) {
      case ContactStatus::Accepted:
         //If accepted need to paint online status in the next switch
         break;
      case ContactStatus::Incoming:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorContactIncoming());
         return QStyledItemDelegate::paint(painter, itemOption, index);
      case ContactStatus::Outgoing:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorContactOutgoing());
         return QStyledItemDelegate::paint(painter, itemOption, index);
      case ContactStatus::Rejected:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorContactRejected());
         return QStyledItemDelegate::paint(painter, itemOption, index);
   }

   switch (onlineStatus) {
      case OnlineStatus::Online:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorContactOnline());
         break;
      case OnlineStatus::Offline:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorContactOffline());
         break;
   }

   QStyledItemDelegate::paint(painter, itemOption, index);

   // draw dot
   if (newMessage) {
      QFontMetrics fm(itemOption.font, painter->device());
      auto textRect = fm.boundingRect(itemOption.rect, 0, itemOption.text);
      const QPixmap pixmap(kDotPathname);
      const QRect r(itemOption.rect.left() + textRect.width() + kDotSize,
                    itemOption.rect.top() + itemOption.rect.height() / 2 - kDotSize / 2 + 1,
                    kDotSize, kDotSize);
      painter->drawPixmap(r, pixmap, pixmap.rect());
   }
}

void ChatClientUsersViewItemDelegate::paintUserElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);
   if (index.data(Role::ItemTypeRole).value<NodeType>() != NodeType::AllUsersElement
       && index.data(Role::ItemTypeRole).value<NodeType>() != NodeType::SearchElement) {
      itemOption.text = QLatin1String("<unknown>");
      return QStyledItemDelegate::paint(painter, itemOption, index);
   }

   switch (index.data(Role::UserOnlineStatusRole).value<Chat::UserStatus>()) {
      case Chat::UserStatus::Online:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorUserOnline());
         break;
      case Chat::UserStatus::Offline:
         itemOption.palette.setColor(QPalette::Text, itemStyle_.colorUserOffline());
         break;
   }
   itemOption.text = index.data(Role::UserIdRole).toString();
   QStyledItemDelegate::paint(painter, itemOption, index);
}

void ChatClientUsersViewItemDelegate::paintOTCReceivedResponsesElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);

   itemOption.text = tr("Received OTC");
   QStyledItemDelegate::paint(painter, itemOption, index);
}

void ChatClientUsersViewItemDelegate::paintOTCSentResponsesElement(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QStyleOptionViewItem itemOption(option);

   itemOption.text = tr("Sent OTC");
   QStyledItemDelegate::paint(painter, itemOption, index);
}

QWidget *ChatClientUsersViewItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QWidget * editor = QStyledItemDelegate::createEditor(parent, option, index);
   editor->setProperty("contact_editor", true);
   return editor;
}
