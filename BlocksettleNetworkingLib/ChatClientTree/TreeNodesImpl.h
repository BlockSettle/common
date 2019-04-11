#ifndef TREENODESIMPL_H
#define TREENODESIMPL_H

#include "TreeObjects.h"

class ChatRoomNode : public CategoryElement {
public:
   ChatRoomNode(std::shared_ptr<Chat::RoomData> data)
      : CategoryElement(TreeItem::NodeType::ChatRoomNode, TreeItem::NodeType::ChatDataNode, data){}

};

class ChatContactNode : public CategoryElement {
public:
   ChatContactNode(std::shared_ptr<Chat::ContactRecordData> data)
      : CategoryElement(TreeItem::NodeType::ChatUserNode, TreeItem::NodeType::ChatDataNode, data){}

};

#endif // TREENODESIMPL_H
