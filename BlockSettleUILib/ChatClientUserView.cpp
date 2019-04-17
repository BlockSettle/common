#include "ChatClientUserView.h"
#include "ChatClientTree/TreeObjects.h"

#include <QMenu>

//using ItemType = ChatUserListTreeViewModel::ItemType;
//using Role = ChatUserListTreeViewModel::Role;

class ChatUsersContextMenu : public QMenu
{
public:
   ChatUsersContextMenu(ChatItemActionsHandler * handler, ChatClientUserView * parent = nullptr) :
      QMenu(parent),
      handler_(handler),
      view_(parent)
   {
      connect(this, &QMenu::aboutToHide, this, &ChatUsersContextMenu::clearMenu);
   }

   ~ChatUsersContextMenu()
   {
      qDebug() << __func__;
   }

   QAction* execMenu(const QPoint & point)
   {
      currentIndex_ = view_->indexAt(point);

      clear();
      currentContact_.reset();
      //ItemType type = static_cast<ItemType>(currentIndex_.data(Role::ItemTypeRole).toInt());
      TreeItem * item = static_cast<TreeItem*>(currentIndex_.internalPointer());
      if (item->getType() == TreeItem::NodeType::ContactsElement) {
         auto citem = static_cast<ChatContactElement*>(item);
         currentContact_ = citem->getContactData();
         prepareContactMenu();
         return exec(view_->viewport()->mapToGlobal(point));
      }

      view_->selectionModel()->clearSelection();
      return nullptr;

   }

private slots:

   void clearMenu() {
      view_->selectionModel()->clearSelection();
   }

   void onAddToContacts(bool)
   {
      qDebug() << __func__;
      if (!handler_){
         return;
      }
      handler_->onActionAddToContacts(currentContact_);
   }

   void onRemoveFromContacts(bool)
   {
      qDebug() << __func__;
      if (!handler_){
         return;
      }
      handler_->onActionRemoveFromContacts(currentContact_);
   }

   void onAcceptFriendRequest(bool)
   {
      if (!handler_){
         return;
      }
      handler_->onActionAcceptContactRequest(currentContact_);
      const auto &text = currentIndex_.data(Qt::DisplayRole).toString();
      //emit view_->acceptFriendRequest(text);
   }

   void onDeclineFriendRequest(bool)
   {
      if (!handler_){
         return;
      }
      handler_->onActionRejectContactRequest(currentContact_);
      const auto &text = currentIndex_.data(Qt::DisplayRole).toString();
      //emit view_->declineFriendRequest(text);
   }

//   ChatUserData::State userState()
//   {
//      TreeItem * item = static_cast<TreeItem*>(currentIndex_.internalPointer());


//      return qvariant_cast<ChatUserData::State>(currentIndex_.data(Role::UserStateRole));
//   }

   void prepareContactMenu()
   {
      if (!currentContact_){
         return;
      }

      switch (currentContact_->getContactStatus()) {
//         case Chat::ContactStatus::
//            addAction(tr("Add friend"), this, &ChatUsersContextMenu::onAddToContacts);
//            break;
         case Chat::ContactStatus::Accepted:
            addAction(tr("Remove friend"), this, &ChatUsersContextMenu::onRemoveFromContacts);
            break;
         case Chat::ContactStatus::Incoming:
            addAction(tr("Accept friend request"), this, &ChatUsersContextMenu::onAcceptFriendRequest);
            addAction(tr("Decline friend request"), this, &ChatUsersContextMenu::onDeclineFriendRequest);
            break;
         case Chat::ContactStatus::Outgoing:
            addAction(tr("This request not accepted"));
         default:
            break;

      }
   }

   void prepareRoomMenu()
   {

   }

private:
   ChatItemActionsHandler* handler_;
   ChatClientUserView * view_;
   QModelIndex currentIndex_;
   std::shared_ptr<Chat::ContactRecordData> currentContact_;
};


ChatClientUserView::ChatClientUserView(QWidget *parent)
   : QTreeView (parent),
     watcher_(nullptr),
     handler_(nullptr),
     contextMenu_(nullptr)
{
   setContextMenuPolicy(Qt::CustomContextMenu);
   connect(this, &QAbstractItemView::customContextMenuRequested, this, &ChatClientUserView::onCustomContextMenu);
}

void ChatClientUserView::setWatcher(std::shared_ptr<ViewItemWatcher> watcher)
{
   watcher_ = watcher;
}

void ChatClientUserView::setActiveChatLabel(QLabel *label)
{
   label_ = label;
}

void ChatClientUserView::onCustomContextMenu(const QPoint & point)
{
   if (!contextMenu_) {
      if (handler_){
         contextMenu_ = new ChatUsersContextMenu(handler_.get(), this);
      }
   }
   if (contextMenu_){
      contextMenu_->execMenu(point);
   }
}

void ChatClientUserView::updateDependUI(CategoryElement *element)
{
   auto data = static_cast<CategoryElement*>(element)->getDataObject();
   switch (element->getType()) {
      case TreeItem::NodeType::RoomsElement:{
         std::shared_ptr<Chat::RoomData> room = std::dynamic_pointer_cast<Chat::RoomData>(data);
         if (label_){
            label_->setText(QObject::tr("CHAT #") + room->getId());
         }
      } break;
      case TreeItem::NodeType::ContactsElement:{
         std::shared_ptr<Chat::ContactRecordData> contact = std::dynamic_pointer_cast<Chat::ContactRecordData>(data);
         if (label_){
            label_->setText(QObject::tr("CHAT #") + contact->getContactId());
         }
      } break;
      case TreeItem::NodeType::AllUsersElement:{
         std::shared_ptr<Chat::UserData> room = std::dynamic_pointer_cast<Chat::UserData>(data);
         if (label_){
            label_->setText(QObject::tr("CHAT #") + room->getUserId());
         }
      } break;
      default:
         break;

   }
}

void ChatClientUserView::setHandler(std::shared_ptr<ChatItemActionsHandler> handler)
{
   handler_ = handler;
}

void ChatClientUserView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
   QTreeView::currentChanged(current, previous);
   TreeItem* item = static_cast<TreeItem*>(current.internalPointer());
   if (watcher_ && item) {
      switch (item->getType()) {
         case TreeItem::NodeType::RoomsElement:
         case TreeItem::NodeType::ContactsElement:
         case TreeItem::NodeType::AllUsersElement:{
            auto element = static_cast<CategoryElement*>(item);
            updateDependUI(element);
            watcher_->onElementSelected(element);
         } break;
         default:
            break;

      }
   }
}

void LoggerWatcher::onElementSelected(CategoryElement *element)
{
   qDebug() << "Item selected:\n" << QString::fromStdString(element->getDataObject()->toJsonString());
}
