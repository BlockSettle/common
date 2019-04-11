#ifndef TREEITEM_H
#define TREEITEM_H
#include <list>
#include <algorithm>


class TreeItem {
   friend class RootItem;
   public:

   enum class NodeType {
      RootNode,
      CategoryNode,

      SearchCategory,
      RoomsCategory,
      ContactsCategory,
      AllUsersCategory,

      ChatRoomNode,
      ChatUserNode,
      ChatDataNode,
   };

   NodeType getType() const { return type_; }
   NodeType getAcceptType() const { return acceptType_; }
   NodeType getTargetParentType() const { return targetParentType_; }
   TreeItem* getParent() const {return parent_; }

   virtual ~TreeItem(){
      for (auto child : children_) {
         delete child;
      }
   }

   virtual bool insertItem(TreeItem* item);
   bool compare(TreeItem* first, TreeItem* second) const;

   std::list<TreeItem*> getChildren(){ return children_;}

protected:

   void grabChildren(TreeItem* item){
      //Copy all children pointers to this children
      for (auto child: item->getChildren()){
         children_.push_back(child);
      }

      //Remove old parent and set new parent as this
      for (auto child: item->getChildren()){
         if (child->getParent()){
            child->getParent()->removeChild(item);
         }
         child->setParent(this);
      }

   }

   void setParent(TreeItem* parent) {
      parent_ = parent;
   }

   void addChild(TreeItem* item) {
      if (item->getParent()){
         item->getParent()->removeChild(item);
      }
      item->setParent(this);
      children_.push_back(item);
   }

   void removeChild(TreeItem* item) {
      children_.erase(std::remove(std::begin(children_), std::end(children_), item), std::end(children_));
   }

   protected:
   TreeItem(NodeType type, NodeType acceptType, NodeType parentType)
      : type_(type)
      , acceptType_(acceptType)
      , targetParentType_(parentType)
      , parent_(nullptr)
   {
      /*
      switch (type_) {
         case NodeType::RootNode:
            acceptType_ = NodeType::CategoryNode;
            targetParentType_ = NodeType::RootNode;
            break;
         case NodeType::CategoryNode:
            acceptType_ = NodeType::CategoryElement;
            targetParentType_ = NodeType::RootNode;
            break;
         default:
            break;
      }*/
   }



   bool isSupported(NodeType type) const {
      //Check if this type is supported by this item
      return acceptType_ == type;
      /*
      auto foundIt = std::find(acceptType_.begin(), acceptType_.end(), type);
      if (foundIt != acceptType_.end()){
         return true;
      } else {
         //Check if this type is supported by any of children
         for (const auto child : children_) {
            if (child->isSupported(type)) {
               return true;
            }
         }
      }
      return false;
   */
   }



   protected:
      NodeType type_;
      NodeType acceptType_;
      NodeType targetParentType_;

      TreeItem * parent_;
      std::list<TreeItem*> children_;
};

#endif // TREEITEM_H
