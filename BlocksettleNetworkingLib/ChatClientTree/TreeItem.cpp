#include "TreeItem.h"
#include "TreeObjects.h"

bool TreeItem::insertItem(TreeItem *item) {
   bool supported = isSupported(item->getType());
   //assert(supported);
   if (supported) {
      addChild(item);
//      //Trying to find this child with same type as we whant to insert
//      std::list<TreeItem*>::iterator childIt = std::find_if(children_.begin(), children_.end(), [this, item] (TreeItem* child){
//         return compare(item, child);
//      });

//      if (childIt != children_.end()) {
//         //Copying the child items from the item that need to insert, if this item type already in out children list
//         (*childIt)->grabChildren(item);
//      } else {
//         //Or if not, just insert it to this children list

//      }
      return true;
   }
   return false;
}


