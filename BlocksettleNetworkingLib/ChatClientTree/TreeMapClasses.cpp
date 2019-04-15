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

void RootItem::clear()
{
   emit beforeUpdate();
   for (auto child : children_)
      child->clearChildren();
   emit afterUpdate();
}

bool RootItem::insertNode(TreeItem * item)
{
   auto it = std::find_if(children_.begin(), children_.end(), [item](TreeItem* child){
      return child->getType() == item->getTargetParentType()
             && child->getAcceptType() == item->getType();
   });

   if (it != children_.end()){
      return (*it)->insertItem(item);
   }
   return false;
}
