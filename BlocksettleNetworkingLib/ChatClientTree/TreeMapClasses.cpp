#include "TreeMapClasses.h"
#include "TreeObjects.h"
#include <algorithm>

bool RootItem::insertRoomObject(std::shared_ptr<Chat::RoomData> data){
   emit beforeUpdate();
   TreeItem* candidate =  new ChatRoomElement(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   if (!res) {
      delete  candidate;
   }
   return  res;
}

bool RootItem::insertContactObject(std::shared_ptr<Chat::ContactRecordData> data){
   emit beforeUpdate();
   TreeItem* candidate = new ChatContactElement(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   return  res;
}

bool RootItem::insertGeneralUserObject(std::shared_ptr<Chat::UserData> data)
{
   emit beforeUpdate();
   TreeItem* candidate = new ChatUserElement(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   if (!res) {
      delete  candidate;
   }
   return  res;
}

bool RootItem::insertSearchUserObject(std::shared_ptr<Chat::UserData> data)
{
   emit beforeUpdate();
   TreeItem* candidate = new ChatSearchElement(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   if (!res) {
      delete  candidate;
   }
   return  res;
}

bool RootItem::insertRoomMessage(std::shared_ptr<Chat::MessageData> message)
{
   TreeMessageNode * messageNode = new TreeMessageNode(TreeItem::NodeType::RoomsElement, message);
   bool res = insertMessageNode(messageNode);
   if (!res){
      delete messageNode;
   }
   return res;
}

bool RootItem::insertContactsMessage(std::shared_ptr<Chat::MessageData> message)
{
   TreeMessageNode * messageNode = new TreeMessageNode(TreeItem::NodeType::ContactsElement, message);
   bool res = insertMessageNode(messageNode);
   if (!res){
      delete messageNode;
   }
   return res;
}

void RootItem::clear()
{
   emit beforeClean();
   for (auto child : children_) {
      child->clearChildren();
   }
   currentUser_.clear();
   emit afterClean();
}

std::string RootItem::currentUser() const
{
   return currentUser_;
}

bool RootItem::insertMessageNode(TreeMessageNode * messageNode)
{
   //assert(targetElement >= NodeType::RoomsElement && targetElement <= NodeType::AllUsersElement);
      auto categoryIt = std::find_if(children_.begin(), children_.end(), [messageNode](TreeItem* child){
         return child->getAcceptType() == messageNode->getTargetParentType();
      });

      if (categoryIt != children_.end()) {

        TreeItem* target = (*categoryIt)->findSupportChild(messageNode);
         if (target) {
            return target->insertItem(messageNode);
         }
         return false;
      }
      return false;


}

bool RootItem::insertNode(TreeItem * item)
{
   TreeItem * supportChild = findSupportChild(item);
   if (supportChild) {
      return supportChild->insertItem(item);
   }
   return false;

//   auto it = std::find_if(children_.begin(), children_.end(), [item](TreeItem* child){
//      return child->getType() == item->getTargetParentType()
//             && child->getAcceptType() == item->getType();
//   });

//   if (it != children_.end()){
//      return (*it)->insertItem(item);
//   }
//   return false;
}


void RootItem::setCurrentUser(const std::string &currentUser)
{
   currentUser_ = currentUser;
}
