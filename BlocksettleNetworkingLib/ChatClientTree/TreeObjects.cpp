#include "TreeObjects.h"



bool ChatRoomElement::isSupported(TreeItem *item) const
{
   bool byTypes = CategoryElement::isSupported(item);
   bool byData = false;
   if (byTypes) {

      const RootItem* root = nullptr;
      const TreeItem* try_root = recursiveRoot();
      if (try_root->getType() == TreeItem::NodeType::RootNode) {
         root = static_cast<const RootItem*>(try_root);
      }
      if (root) {
         std::string user = root->currentUser();
         auto room = std::dynamic_pointer_cast<Chat::RoomData>(getDataObject());
         TreeMessageNode * mNode = static_cast<TreeMessageNode*>(item);

//         bool forCurrentUser = (mNode->getMessage()->getSenderId().toStdString() == user
//                             || mNode->getMessage()->getReceiverId().toStdString() == user);
         bool forThisElement =    /*mNode->getMessage()->getSenderId() == room->getId()
                               || */mNode->getMessage()->getReceiverId() == room->getId();

         byData = forThisElement;
      }

   }

   return byTypes && byData;
}

std::shared_ptr<Chat::ContactRecordData> ChatContactElement::getContactData()
{
   return std::dynamic_pointer_cast<Chat::ContactRecordData>(getDataObject());
}

bool ChatContactElement::isSupported(TreeItem *item) const
{
   bool byTypes = CategoryElement::isSupported(item);
   bool byData = false;
   if (byTypes) {

      const RootItem* root = nullptr;
      const TreeItem* try_root = recursiveRoot();
      if (try_root->getType() == TreeItem::NodeType::RootNode) {
         root = static_cast<const RootItem*>(try_root);
      }
      if (root) {
         std::string user = root->currentUser();
         auto contact = std::dynamic_pointer_cast<Chat::ContactRecordData>(getDataObject());
         TreeMessageNode * mNode = static_cast<TreeMessageNode*>(item);

         bool forCurrentUser = (mNode->getMessage()->getSenderId().toStdString() == user
                             || mNode->getMessage()->getReceiverId().toStdString() == user);
         bool forThisElement = forCurrentUser &&
                               (  mNode->getMessage()->getSenderId() == contact->getContactId()
                                ||mNode->getMessage()->getReceiverId() == contact->getContactId());

         byData = forThisElement;
      }

   }

   return byTypes && byData;
}
