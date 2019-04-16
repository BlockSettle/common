#ifndef CHATCLIENTUSERVIEW_H
#define CHATCLIENTUSERVIEW_H

#include <QTreeView>
#include <QLabel>
#include <QDebug>
class CategoryElement;
class ViewItemWatcher {
public:
   virtual void onElementSelected(CategoryElement* element) = 0;
   virtual ~ViewItemWatcher() = default;
};

class LoggerWatcher : public ViewItemWatcher {


   // ViewItemWatcher interface
public:
   void onElementSelected(CategoryElement *element) override;
};



class ChatClientUserView : public QTreeView
{
public:
   ChatClientUserView(QWidget * parent = nullptr);
   void setWatcher(std::shared_ptr<ViewItemWatcher> watcher);
   void setActiveChatLabel(QLabel * label);
private:
   void updateDependUI(CategoryElement * element);
private:
   std::shared_ptr<ViewItemWatcher> watcher_;
   QLabel * label_;


   // QAbstractItemView interface
protected slots:
   void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
};



#endif // CHATCLIENTUSERVIEW_H
