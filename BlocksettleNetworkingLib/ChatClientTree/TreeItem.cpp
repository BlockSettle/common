#include "TreeItem.h"
#include <iostream>
#include <QDebug>

TreeItem::NodeType TreeItem::getType() const { return type_; }

TreeItem::NodeType TreeItem::getAcceptType() const { return acceptType_; }

TreeItem::NodeType TreeItem::getTargetParentType() const { return targetParentType_; }

TreeItem *TreeItem::getParent() const {return parent_; }

TreeItem::~TreeItem(){
   clearChildren();
   qDebug() << "TreeItem::~TreeItem()";
}

bool TreeItem::insertItem(TreeItem *item) {
   bool supported = isSupported(item);
   //assert(supported);
   if (supported) {
      addChild(item);
      return true;
   }
   return false;
}

int TreeItem::selfIndex() const
{
   if (parent_){
       auto thizIt = std::find(parent_->children_.begin(), parent_->children_.end(), this);
       if (thizIt != parent_->children_.end()){
          return static_cast<int>(std::distance(parent_->children_.begin(), thizIt));
       }
   }
   return 0;
}

void TreeItem::clearChildren()
{
   for (auto child : children_) {
      delete child;
   }
   children_.clear();
}

void TreeItem::grabChildren(TreeItem *item){
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

void TreeItem::setParent(TreeItem *parent) {
   parent_ = parent;
}

void TreeItem::addChild(TreeItem *item) {
   if (item->getParent()){
      item->getParent()->removeChild(item);
   }
   item->setParent(this);
   children_.push_back(item);
}

void TreeItem::removeChild(TreeItem *item) {
   emit
   children_.erase(std::remove(std::begin(children_), std::end(children_), item), std::end(children_));
}

TreeItem::TreeItem(TreeItem::NodeType type, TreeItem::NodeType acceptType, TreeItem::NodeType parentType)
   : QObject(nullptr)
   , type_(type)
   , acceptType_(acceptType)
   , targetParentType_(parentType)
   , parent_(nullptr)
{
}

bool TreeItem::isSupported(TreeItem *item) const {
   //Check if this type is supported by this item
   return acceptType_ == item->getType() && type_ == item->getTargetParentType();
}


