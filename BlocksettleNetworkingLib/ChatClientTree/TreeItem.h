#ifndef TREEITEM_H
#define TREEITEM_H
#include <list>
#include <algorithm>
#include "../ChatProtocol/DataObjects.h"

class TreeItem {
   friend class RootItem;
   public:
   enum class ItemType {
      RootItem,
      CategoryItem,
      DisplayItem
   };

   enum class Category {
      SearchCategory,
      RoomsCategory,
      ContactsCategory,
      AllUsersCategory
   };

   enum class Display {
      ChatRoom,
      ChatUser
   };

   ItemType getType() const { return type_; }
   TreeItem* getParent() const {return parent_; }

   virtual ~TreeItem(){
      for (auto child : children_) {
         delete child;
      }
   }

   virtual bool insertItem(TreeItem* item) {
      bool supported = isSupported(item->getType());
      if (supported) {
         //Trying to find this child with same type as we whant to insert
         std::list<TreeItem*>::iterator childIt = std::find_if(children_.begin(), children_.end(), [item] (const TreeItem* child){
            return item->getType() == child->getType();
         });

         if (childIt != children_.end()) {
            //Copying the child items from the item that need to insert, if this item type already in out children list
            (*childIt)->grabChildren(item);
         } else {
            //Or if not, just insert it to this children list
            children_.push_back(item);
         }
         return true;
      }
      return false;
   }

   std::list<TreeItem*> getChildren(){ return children_;}

protected:

   void grabChildren(TreeItem* item){
      //Copy all children pointers to this children
      for (auto child: item->getChildren()){
         children_.push_back(child);
      }

      //Remove old parent and sen new parent as this
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
   TreeItem(ItemType type) : type_(type) {
      switch (type_) {
         case ItemType::RootItem:
            acceptTypes_.push_back(ItemType::CategoryItem);
            //acceptTypes_.push_back(ItemType::DisplayItem);
            break;
         case ItemType::CategoryItem:
            acceptTypes_.push_back(ItemType::DisplayItem);
            break;
         default:
            break;
      }
   }



   bool isSupported(ItemType type) const {
      //Check if this type is supported by this item
      auto foundIt = std::find(acceptTypes_.begin(), acceptTypes_.end(), type);
      if (foundIt != acceptTypes_.end()){
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
   }

   std::list<ItemType> getAcceptTypes() const { return acceptTypes_; }

   protected:
      ItemType type_;
      std::list<ItemType> acceptTypes_;
      TreeItem * parent_;
      std::list<TreeItem*> children_;
};

class RootItem : public TreeItem {
   public:
   RootItem() : TreeItem(TreeItem::ItemType::RootItem) {
   }



};

class CategoryItem : public TreeItem {
   public:
   CategoryItem(Category category, Display storingItems)
      : TreeItem(TreeItem::ItemType::CategoryItem)
      , category_(category)
      , storingItems_(storingItems) {
   }

   Category getCategory() const {return category_; }
   Display getStoringItems() { return storingItems_;}

   private:
   Category category_;
   Display storingItems_;
};

class DisplayItem : public TreeItem {
   public:
   DisplayItem(Category category, Display displayType, Chat::DataObject* object)
      : TreeItem(TreeItem::ItemType::DisplayItem)
      , category_(category)
      , displayType_(displayType)
      , dataObject_(object)
   {

   }

   Category getCategory() {return category_; }
   Display getDisplayType() {return displayType_;}
   Chat::DataObject* getDataObject() {return dataObject_;}
   private:
   Category category_;
   Display displayType_;
   Chat::DataObject* dataObject_;
};

#endif // TREEITEM_H
