#ifndef __DIALOG_MANAGER_H__
#define __DIALOG_MANAGER_H__

#include <QDialog>
#include <QPoint>
#include <QList>
#include <QPointer>

class QWidget;
class DialogManager : public QObject
{
   Q_OBJECT

public:
   explicit DialogManager(const QWidget* mainWindow);
   ~DialogManager() noexcept = default;

   DialogManager(const DialogManager&) = delete;
   DialogManager& operator = (const DialogManager&) = delete;

   DialogManager(DialogManager&&) = delete;
   DialogManager& operator = (DialogManager&&) = delete;

   void adjustDialogPosition(QDialog *dialog);

private slots:
   void onDialogFinished();

private:
   const QPointer<const QWidget> mainWindow_ = nullptr;
   QList<QPointer<QDialog>>      activeDlgs_;
   static const int              offset = 10;
};

#endif // __DIALOG_MANAGER_H__
