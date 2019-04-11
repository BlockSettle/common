#include "TreeObjects.h"
#include "TreeNodesImpl.h"


bool RootItem::insertDataObject(std::shared_ptr<Chat::RoomData> data){
   return insertItem(new ChatRoomNode(data));
}

bool RootItem::insertDataObject(std::shared_ptr<Chat::ContactRecordData> data){
   return insertItem(new ChatContactNode(data));
}
