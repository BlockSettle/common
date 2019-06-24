#ifndef EDITCONTACTDIALOG_H
#define EDITCONTACTDIALOG_H

#include <QDialog>
#include <QDateTime>

#include <memory>

namespace Ui {
class EditContactDialog;
}

class EditContactDialog : public QDialog
{
   Q_OBJECT

public:
   explicit EditContactDialog(
         const QString &contactId
         , const QString &displayName = QString()
         , const QDateTime &joinDate = QDateTime()
         , const QString &idKey = QString()
         , QWidget *parent = nullptr);
   ~EditContactDialog() noexcept override;

   QString contactId() const;
   QString displayName() const;
   QDateTime joinDate() const;
   QString idKey() const;

public slots:
   void accept() override;
   void reject() override;

private:
   void refillFields();

private:
   std::unique_ptr<Ui::EditContactDialog> ui_;
   QString contactId_;
   QString displayName_;
   QDateTime joinDate_;
   QString idKey_;
};

#endif // EDITCONTACTDIALOG_H
