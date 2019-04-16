#include "ChatClientUserView.h"
#include "ChatClientTree/TreeObjects.h"
ChatClientUserView::ChatClientUserView(QWidget *parent)
   : QTreeView (parent),
     watcher_(nullptr)
{

}

void ChatClientUserView::setWatcher(std::shared_ptr<ViewItemWatcher> watcher)
{
   watcher_ = watcher;
}

void ChatClientUserView::setActiveChatLabel(QLabel *label)
{
   label_ = label;
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
