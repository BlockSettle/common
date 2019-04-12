#ifndef CHATCLIENTUSERSMODEL_H
#define CHATCLIENTUSERSMODEL_H

#include <memory>
#include <QAbstractItemModel>

#include "ChatClientTree/TreeObjects.h"


class ChatClientUsersModel : public QAbstractItemModel
{
public:
   ChatClientUsersModel(std::shared_ptr<TreeItem> root, QObject * parent = nullptr);


   // QAbstractItemModel interface
public:
   QModelIndex index(int row, int column, const QModelIndex &parent) const override;
   QModelIndex parent(const QModelIndex &child) const override;
   int rowCount(const QModelIndex &parent) const override;
   int columnCount(const QModelIndex &parent) const override;
   QVariant data(const QModelIndex &index, int role) const override;
   private slots:
   void onBeforeUpdate();
   void onAfterUpdate();
private:
   QVariant categoryNodeData(const TreeItem *item, int role) const;
   QVariant categoryElementData(TreeItem *item, int role) const;

private:
   std::shared_ptr<TreeItem> root_;
};











#endif // CHATCLIENTUSERSMODEL_H
