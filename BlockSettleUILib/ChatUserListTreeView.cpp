#include "ChatUserListTreeView.h"

#include <QPainter>
#include <QMenu>
#include <QDebug>

namespace {
   static const int DOT_RADIUS = 4;
   static const int DOT_SHIFT = 8 + DOT_RADIUS;
}
using ItemType = ChatUserListTreeViewModel::ItemType;
using Role = ChatUserListTreeViewModel::Role;
class BSContextMenu : public QMenu
{
public:
   BSContextMenu(ChatUserListTreeView* view)
   : QMenu(view)
   , view_(view)
   , addFriend_(new QAction(tr("Request friend"), this))
   , removeFriend_(new QAction(tr("Remove friend"), this))
   , acceptFriend_(new QAction(tr("Accept friend"), this))
   , declineFriend_(new QAction(tr("Decline friend"), this))
   , roomAction1_(new QAction(tr("Maybe room menu 1"), this))
   , roomAction2_(new QAction(tr("Maybe room menu 2"), this)){
      connect(this, &QMenu::aboutToHide, this, &BSContextMenu::clearMenu);
      connect(addFriend_, &QAction::triggered , this, &BSContextMenu::onAddToContacts);
      connect(removeFriend_, &QAction::triggered, this, &BSContextMenu::onRemoveFromContacts);
      connect(acceptFriend_, &QAction::triggered, this, &BSContextMenu::onAcceptFriendRequest);
      connect(declineFriend_, &QAction::triggered, this, &BSContextMenu::onDeclineFriendRequest);
   }
   ~BSContextMenu(){
      qDebug() << __func__;
   }

   QAction* execMenu(const QPoint & point){
      currentIndex_ = view_->indexAt(point);
      clear();
      prepareMenu();
      return exec(view_->viewport()->mapToGlobal(point));
   }
private:

   void onAddToContacts(bool) {
      qDebug() << __func__;
   }

   void onRemoveFromContacts(bool) {
      qDebug() << __func__;
   }

   void onAcceptFriendRequest(bool) {
      qDebug() << __func__;
   }
   void onDeclineFriendRequest(bool) {
      qDebug() << __func__;
   }

   void prepareMenu(){
      ItemType type = static_cast<ItemType>(currentIndex_.data(Role::ItemTypeRole).toInt());
      switch (type) {
         case ItemType::UserItem:
            return prepareUserMenu();
         case ItemType::RoomItem:
            return prepareRoomMenu();
            default:
               break;
      }
   }

   ChatUserData::State userState(){
      return qvariant_cast<ChatUserData::State>(currentIndex_.data(Role::UserStateRole));
   }

   void prepareUserMenu(){
      switch (userState()) {
         case ChatUserData::State::Unknown:
            addAction(tr("Remove friend"), this, &BSContextMenu::onAddToContacts);
            break;
         case ChatUserData::State::Friend:
            addAction(tr("Remove friend"), this, &BSContextMenu::onRemoveFromContacts);
            break;
         case ChatUserData::State::IncomingFriendRequest:
            addAction(tr("Accept friend request"), this, &BSContextMenu::onAcceptFriendRequest);
            addAction(tr("Decline friend request"), this, &BSContextMenu::onDeclineFriendRequest);
            break;

      }
   }
   void prepareRoomMenu(){
      addAction(tr("Maybe room menu 1"));
      addAction(tr("Maybe room menu 2"));
   }

private slots:
   void clearMenu() {
      //if call clear() here will be crash!
   }

private:
   ChatUserListTreeView* view_;
   QAction* addFriend_;
   QAction* removeFriend_;
   QAction* acceptFriend_;
   QAction* declineFriend_;
   QAction* roomAction1_;
   QAction* roomAction2_;
   QModelIndex currentIndex_;
};

ChatUserListTreeView::ChatUserListTreeView(QWidget *parent) : QTreeView(parent), internalStyle_(this)
{
   chatUserListModel_ = new ChatUserListTreeViewModel(this);
   setModel(chatUserListModel_);
   setItemDelegate(new ChatUserListTreeViewDelegate(internalStyle_, this));
 
   connect(this, &QAbstractItemView::clicked,
         this, &ChatUserListTreeView::onUserListItemClicked);

   contextMenu_ = new BSContextMenu(this);
   setContextMenuPolicy(Qt::CustomContextMenu);
   connect(this, &QAbstractItemView::customContextMenuRequested, this, &ChatUserListTreeView::onCustomContextMenu);
}

void ChatUserListTreeView::onChatUserDataListChanged(const ChatUserDataListPtr &chatUserDataList)
{
   chatUserListModel_->setChatUserDataList(chatUserDataList);
   expandAll();
}

void ChatUserListTreeView::onChatRoomDataListChanged(const Chat::ChatRoomDataListPtr &roomsDataList)
{
   chatUserListModel_->setChatRoomDataList(roomsDataList);
   expandAll();
}

void ChatUserListTreeView::onCustomContextMenu(const QPoint & point)
{
   contextMenu_->execMenu(point);
}

void ChatUserListTreeView::onUserListItemClicked(const QModelIndex &index)
{
   const auto &itemType =
            qvariant_cast<ChatUserListTreeViewModel::ItemType>(index.data(ChatUserListTreeViewModel::ItemTypeRole));

   clearSelection();

   if (itemType == ChatUserListTreeViewModel::ItemType::RoomItem) {
      const QString roomId = index.data(ChatUserListTreeViewModel::RoomIDRole).toString();
      emit roomClicked(roomId);

   }
   else if (itemType == ChatUserListTreeViewModel::ItemType::UserItem) {
      const QString userId = index.data(Qt::DisplayRole).toString();
      emit userClicked(userId);
   }
}

ChatUserListTreeViewModel *ChatUserListTreeView::chatUserListModel() const
{
   return chatUserListModel_;
}

ChatUserListTreeViewDelegate::ChatUserListTreeViewDelegate(const ChatUserListTreeViewStyle& style, QObject* parent)
: QStyledItemDelegate (parent), internalStyle_(style)
{
    
}

void ChatUserListTreeViewDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
   const auto &itemType =
            qvariant_cast<ChatUserListTreeViewModel::ItemType>(index.data(ChatUserListTreeViewModel::ItemTypeRole));

   QStyleOptionViewItem itemOption(option);

   if (itemType == ChatUserListTreeViewModel::ItemType::RoomItem) {
      itemOption.palette.setColor(QPalette::Text, internalStyle_.colorRoom());
      itemOption.palette.setColor(QPalette::HighlightedText, internalStyle_.colorRoom());
      QStyledItemDelegate::paint(painter, itemOption, index);
      
      // draw dot
      const bool haveMessage =
            qvariant_cast<bool>(index.data(ChatUserListTreeViewModel::HaveNewMessageRole));
      if (haveMessage) {
         auto text = index.data(Qt::DisplayRole).toString();
         QFontMetrics fm(itemOption.font, painter->device());
         auto textRect = fm.boundingRect(itemOption.rect, 0, text);
         auto textWidth = textRect.width();
         QPoint dotPoint(itemOption.rect.left() + textWidth + DOT_SHIFT, itemOption.rect.top() + itemOption.rect.height() / 2);
         painter->save();
         painter->setBrush(QBrush(internalStyle_.colorUserOnline(), Qt::SolidPattern));
         painter->drawEllipse(dotPoint, DOT_RADIUS, DOT_RADIUS);
         painter->restore();
      }

   } 
   else if (itemType == ChatUserListTreeViewModel::ItemType::UserItem) {
      // set default text color
      itemOption.palette.setColor(QPalette::Text, internalStyle_.colorUserDefault());
      itemOption.palette.setColor(QPalette::HighlightedText, internalStyle_.colorUserDefault());

      const auto &userState =
            qvariant_cast<ChatUserData::State>(index.data(ChatUserListTreeViewModel::UserStateRole));

      // text color for friend request
      if (userState == ChatUserData::State::IncomingFriendRequest) {
         itemOption.palette.setColor(QPalette::Text, internalStyle_.colorIncomingFriendRequest());
         itemOption.palette.setColor(QPalette::HighlightedText, internalStyle_.colorIncomingFriendRequest());
         return QStyledItemDelegate::paint(painter, itemOption, index);
      }

      const auto &userStatus =
            qvariant_cast<ChatUserData::ConnectionStatus>(index.data(ChatUserListTreeViewModel::UserConnectionStatusRole));

      // text color for user online status
      if (userStatus == ChatUserData::ConnectionStatus::Online) {
         itemOption.palette.setColor(QPalette::Text, internalStyle_.colorUserOnline());
         itemOption.palette.setColor(QPalette::HighlightedText, internalStyle_.colorUserOnline());
      }

      QStyledItemDelegate::paint(painter, itemOption, index);

      // draw dot
      const bool haveMessage =
            qvariant_cast<bool>(index.data(ChatUserListTreeViewModel::HaveNewMessageRole));
      if (haveMessage) {
         auto text = index.data(Qt::DisplayRole).toString();
         QFontMetrics fm(itemOption.font, painter->device());
         auto textRect = fm.boundingRect(itemOption.rect, 0, text);
         auto textWidth = textRect.width();
         QPoint dotPoint(itemOption.rect.left() + textWidth + DOT_SHIFT, itemOption.rect.top() + itemOption.rect.height() / 2);
         painter->save();
         painter->setBrush(QBrush(internalStyle_.colorUserOnline(), Qt::SolidPattern));
         painter->drawEllipse(dotPoint, DOT_RADIUS, DOT_RADIUS);
         painter->restore();
      }
   } 
   else {
      
      QStyledItemDelegate::paint(painter, itemOption, index);
   }
   
}
