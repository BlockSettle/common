#ifndef TREEOBJECTS_H
#define TREEOBJECTS_H

#include "TreeItem.h"
#include "ChatProtocol/DataObjects.h"

class RootItem : public TreeItem {
   public:
   RootItem() : TreeItem(NodeType::RootNode, NodeType::CategoryNode, NodeType::RootNode) {
   }

   bool insertDataObject(std::shared_ptr<Chat::RoomData> data);
   bool insertDataObject(std::shared_ptr<Chat::ContactRecordData> data);
};

class CategoryItem : public TreeItem {
   public:
   CategoryItem(NodeType categoryType)
      : TreeItem(NodeType::CategoryNode, categoryType, NodeType::RootNode) {
   }
};

class CategoryElement : public TreeItem {
   protected:
   CategoryElement(NodeType elementType, NodeType acceptType, std::shared_ptr<Chat::DataObject> object)
      : TreeItem(elementType, acceptType, NodeType::CategoryNode)
      , dataObject_(object)
   {

   }

   std::shared_ptr<Chat::DataObject> getDataObject() const {return dataObject_;}
   private:
   std::shared_ptr<Chat::DataObject> dataObject_;
};


#endif // TREEOBJECTS_H
