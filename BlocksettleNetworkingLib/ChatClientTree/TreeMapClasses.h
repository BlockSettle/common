#ifndef TREEMAPCLASSES_H
#define TREEMAPCLASSES_H

#include "TreeItem.h"
#include "ChatProtocol/DataObjects.h"

class RootItem : public TreeItem {
   public:
   RootItem() : TreeItem(NodeType::RootNode, NodeType::CategoryNode, NodeType::RootNode) {
   }

   bool insertRoomObject(std::shared_ptr<Chat::RoomData> data);
   bool insertContactObject(std::shared_ptr<Chat::ContactRecordData> data);
   bool insertGeneralUserObject(std::shared_ptr<Chat::UserData> data);
   bool insertSearchUserObject(std::shared_ptr<Chat::UserData> data);
private:
   bool insertNode(TreeItem* item);
};

class CategoryItem : public TreeItem {
   public:
   CategoryItem(NodeType categoryType)
      : TreeItem(NodeType::CategoryNode, categoryType, NodeType::RootNode) {
   }
};

class CategoryElement : public TreeItem {
   protected:
   CategoryElement(NodeType categoryType, NodeType acceptType, std::shared_ptr<Chat::DataObject> object)
      : TreeItem(categoryType, acceptType, NodeType::CategoryNode)
      , dataObject_(object)
   {

   }
public:
   std::shared_ptr<Chat::DataObject> getDataObject() const {return dataObject_;}
   private:
   std::shared_ptr<Chat::DataObject> dataObject_;
};


#endif // TREEMAPCLASSES_H
