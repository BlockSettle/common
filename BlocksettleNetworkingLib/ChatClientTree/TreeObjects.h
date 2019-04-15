#ifndef TREEOBJECTS_H
#define TREEOBJECTS_H

#include "TreeMapClasses.h"

class ChatRoomElement : public CategoryElement {
public:
   ChatRoomElement(std::shared_ptr<Chat::RoomData> data)
      : CategoryElement(TreeItem::NodeType::RoomsElement, TreeItem::NodeType::ChatRoomNode, data){}

};

class ChatContactElement : public CategoryElement {
public:
   ChatContactElement(std::shared_ptr<Chat::ContactRecordData> data)
      : CategoryElement(TreeItem::NodeType::ContactsElement, TreeItem::NodeType::ChatContactNode, data){}

};

class ChatSearchElement : public CategoryElement {
public:
   ChatSearchElement(std::shared_ptr<Chat::UserData> data)
      : CategoryElement(TreeItem::NodeType::SearchElement, TreeItem::NodeType::ChatUserNode, data){}

};

class ChatUserElement : public CategoryElement {
public:
   ChatUserElement(std::shared_ptr<Chat::UserData> data)
      : CategoryElement(TreeItem::NodeType::AllUsersElement, TreeItem::NodeType::ChatUserNode, data){}

};

#endif // TREENODESIMPL_H
