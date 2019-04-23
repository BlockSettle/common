#ifndef CHATCLIENTUSERSMODEL_H
#define CHATCLIENTUSERSMODEL_H

#include <memory>
#include <QAbstractItemModel>

#include "ChatClientTree/TreeObjects.h"

class ChatClientDataModel : public QAbstractItemModel
{
public:
   ChatClientDataModel(std::shared_ptr<TreeItem> root, QObject * parent = nullptr);


   // QAbstractItemModel interface
public:
   QModelIndex index(int row, int column, const QModelIndex &parent) const override;
   QModelIndex parent(const QModelIndex &child) const override;
   int rowCount(const QModelIndex &parent) const override;
   int columnCount(const QModelIndex &parent) const override;
   QVariant data(const QModelIndex &index, int role) const override;
   Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
   void onBeforeUpdate();
   void onAfterUpdate();
   void onBeforeClean();
   void onAfterClean();
   void onItemChanged(TreeItem* item);
private:
   QVariant categoryNodeData(const TreeItem *item, int role) const;
   QVariant categoryElementData(TreeItem *item, int role) const;

private:
   std::shared_ptr<TreeItem> root_;
};













#endif // CHATCLIENTUSERSMODEL_H
