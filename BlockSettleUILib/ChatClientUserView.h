#ifndef CHATCLIENTUSERVIEW_H
#define CHATCLIENTUSERVIEW_H

#include <QTreeView>
#include <QLabel>
#include <QDebug>
#include "ChatHandleInterfaces.h"


class LoggerWatcher : public ViewItemWatcher {


   // ViewItemWatcher interface
public:
   void onElementSelected(CategoryElement *element) override;
};


class ChatUsersContextMenu;
class ChatClientUserView : public QTreeView
{
   Q_OBJECT
public:
   ChatClientUserView(QWidget * parent = nullptr);
   void setWatcher(std::shared_ptr<ViewItemWatcher> watcher);
   void setActiveChatLabel(QLabel * label);
   void setHandler(std::shared_ptr<ChatItemActionsHandler> handler);

public slots:
   void onCustomContextMenu(const QPoint &);
private:
   void updateDependUI(CategoryElement * element);
private:
   std::shared_ptr<ViewItemWatcher> watcher_;
   std::shared_ptr<ChatItemActionsHandler> handler_;
   QLabel * label_;
   ChatUsersContextMenu* contextMenu_;


   // QAbstractItemView interface
protected slots:
   void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
};



#endif // CHATCLIENTUSERVIEW_H
