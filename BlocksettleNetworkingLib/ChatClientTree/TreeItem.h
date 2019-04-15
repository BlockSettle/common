#ifndef TREEITEM_H
#define TREEITEM_H
#include <list>
#include <vector>
#include <algorithm>
#include <QObject>

class TreeItem : public QObject {

   Q_OBJECT

   friend class RootItem;
   public:

   enum class NodeType {
      //Root
      RootNode,

      //RootNode accept types
      CategoryNode,

      //CategoryNode accept types (subcategory)
      SearchElement,
      RoomsElement,
      ContactsElement,
      AllUsersElement,

      //Subcategory accept types
      ChatRoomNode,
      ChatContactNode,
      ChatUserNode
   };

   NodeType getType() const;
   NodeType getAcceptType() const;
   NodeType getTargetParentType() const;
   TreeItem* getParent() const;

   virtual ~TreeItem();

   virtual bool insertItem(TreeItem* item);
   int selfIndex() const;
   std::vector<TreeItem*> getChildren(){ return children_;}

signals:
   void beforeUpdate();
   void afterUpdate();
protected:
   void clearChildren();
   void grabChildren(TreeItem* item);
   void setParent(TreeItem* parent);
   void addChild(TreeItem* item);
   void removeChild(TreeItem* item);
protected:
   TreeItem(NodeType type, NodeType acceptType, NodeType parentType);
   bool isSupported(TreeItem* item) const;
protected:
      NodeType type_;
      NodeType acceptType_;
      NodeType targetParentType_;

      TreeItem * parent_;
      std::vector<TreeItem*> children_;
};

#endif // TREEITEM_H
