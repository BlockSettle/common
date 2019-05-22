#include "ChatClientProxyModel.h"
#include "ChatClientDataModel.h"
#include <QDebug>

using NodeType = ChatUIDefinitions::ChatTreeNodeType;
using Role = ChatClientDataModel::Role;
using OnlineStatus = ChatContactElement::OnlineStatus;
using ContactStatus = Chat::ContactStatus;

ChatClientProxyModel::ChatClientProxyModel(QObject *parent)
   : QSortFilterProxyModel(parent)
{
}

bool ChatClientProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
   // don't sort room chats
   if (left.data(Role::ItemTypeRole).value<NodeType>() == NodeType::RoomsElement) {
      return true;
   }

   // contacts with online status are placed at the top of the list
   if (left.data(Role::ItemTypeRole).value<NodeType>() == NodeType::ContactsElement &&
       right.data(Role::ItemTypeRole).value<NodeType>() == NodeType::ContactsElement) {

      auto leftContactStatus = left.data(Role::ContactStatusRole).value<ContactStatus>();
      auto leftOnlineStatus = left.data(Role::ContactOnlineStatusRole).value<OnlineStatus>();

      auto rightContactStatus = right.data(Role::ContactStatusRole).value<ContactStatus>();
      auto rightOnlineStatus = right.data(Role::ContactOnlineStatusRole).value<OnlineStatus>();

      if (leftOnlineStatus == OnlineStatus::Online && rightOnlineStatus == OnlineStatus::Offline) {
         return false;
      }
      return true;
   }
   return QSortFilterProxyModel::lessThan(left, right);
}
