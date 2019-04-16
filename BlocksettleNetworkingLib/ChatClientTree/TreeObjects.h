#ifndef TREEOBJECTS_H
#define TREEOBJECTS_H

#include "TreeMapClasses.h"

class ChatRoomElement : public CategoryElement {
public:
   ChatRoomElement(std::shared_ptr<Chat::RoomData> data)
      : CategoryElement(TreeItem::NodeType::RoomsElement, TreeItem::NodeType::MessageDataNode, data){}


   // TreeItem interface
protected:
   bool isSupported(TreeItem *item) const override;
};



class ChatContactElement : public CategoryElement {
public:
   ChatContactElement(std::shared_ptr<Chat::ContactRecordData> data)
      : CategoryElement(TreeItem::NodeType::ContactsElement, TreeItem::NodeType::MessageDataNode, data){}
   std::shared_ptr<Chat::ContactRecordData> getContactData();

   // TreeItem interface
protected:
   bool isSupported(TreeItem *item) const override;
};



class ChatSearchElement : public CategoryElement {
public:
   ChatSearchElement(std::shared_ptr<Chat::UserData> data)
      : CategoryElement(TreeItem::NodeType::SearchElement, TreeItem::NodeType::NoDataNode, data){}

};

class ChatUserElement : public CategoryElement {
public:
   ChatUserElement(std::shared_ptr<Chat::UserData> data)
      : CategoryElement(TreeItem::NodeType::AllUsersElement, TreeItem::NodeType::MessageDataNode, data){}

};

class TreeMessageNode : public TreeItem {
public:
   TreeMessageNode(TreeItem::NodeType messageParent, std::shared_ptr<Chat::MessageData> message)
      : TreeItem(NodeType::MessageDataNode, NodeType::NoDataNode, messageParent)
      , message_(message)
   {

   }
   std::shared_ptr<Chat::MessageData> getMessage() const {return message_;}
private:
   std::shared_ptr<Chat::MessageData> message_;
};
/*
class ChatUserMessageNode : public TreeItem {
public:
   ChatUserMessageNode(std::shared_ptr<Chat::MessageData> message)
      : TreeItem(NodeType::MessageDataNode, NodeType::NoDataNode, TreeItem::NodeType::ContactsElement)
      , message_(message)
   {

   }
   std::shared_ptr<Chat::MessageData> getMessage() const {return message_;}
private:
   std::shared_ptr<Chat::MessageData> message_;
};*/

#endif // TREENODESIMPL_H
