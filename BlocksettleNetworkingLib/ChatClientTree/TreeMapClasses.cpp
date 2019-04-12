#include "TreeMapClasses.h"
#include "TreeObjects.h"
#include <algorithm>

bool RootItem::insertRoomObject(std::shared_ptr<Chat::RoomData> data){
   emit beforeUpdate();
   TreeItem* candidate =  new ChatRoomNode(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   if (!res) {
      delete  candidate;
   }
   return  res;
}

bool RootItem::insertContactObject(std::shared_ptr<Chat::ContactRecordData> data){
   emit beforeUpdate();
   TreeItem* candidate = new ChatContactNode(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   return  res;
}

bool RootItem::insertGeneralUserObject(std::shared_ptr<Chat::UserData> data)
{
   emit beforeUpdate();
   TreeItem* candidate = new ChatUserNode(data);
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
   TreeItem* candidate = new ChatSearchNode(data);
   bool res = insertNode(candidate);
   emit afterUpdate();
   if (!res) {
      delete  candidate;
   }
   return  res;
}

bool RootItem::insertNode(TreeItem * item)
{
   auto it = std::find_if(children_.begin(), children_.end(), [item](TreeItem* child){
      return child->getType() == item->getTargetParentType();
   });

   if (it != children_.end()){
      return (*it)->insertItem(item);
   }
   return false;
}
