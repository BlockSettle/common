#ifndef TREEOBJECTS_H
#define TREEOBJECTS_H

#include "TreeMapClasses.h"

class ChatRoomNode : public CategoryElement {
public:
   ChatRoomNode(std::shared_ptr<Chat::RoomData> data)
      : CategoryElement(TreeItem::NodeType::RoomsCategory, TreeItem::NodeType::ChatRoomNode, data){}

};

class ChatContactNode : public CategoryElement {
public:
   ChatContactNode(std::shared_ptr<Chat::ContactRecordData> data)
      : CategoryElement(TreeItem::NodeType::ContactsCategory, TreeItem::NodeType::ChatContactNode, data){}

};

class ChatSearchNode : public CategoryElement {
public:
   ChatSearchNode(std::shared_ptr<Chat::UserData> data)
      : CategoryElement(TreeItem::NodeType::SearchCategory, TreeItem::NodeType::ChatUserNode, data){}

};

class ChatUserNode : public CategoryElement {
public:
   ChatUserNode(std::shared_ptr<Chat::UserData> data)
      : CategoryElement(TreeItem::NodeType::AllUsersCategory, TreeItem::NodeType::ChatUserNode, data){}

};

#endif // TREENODESIMPL_H
