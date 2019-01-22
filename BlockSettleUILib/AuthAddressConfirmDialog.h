
#ifndef AUTH_ADDRESS_CONFIRMATION_DIALOG_H__
#define AUTH_ADDRESS_CONFIRMATION_DIALOG_H__

#include "AuthAddressManager.h"

#include <QDialog>
#include <QTimer>
#include <QString>

#include <chrono>

namespace Ui {
    class AuthAddressConfirmDialog;
};

class AuthAddressConfirmDialog : public QDialog
{
Q_OBJECT

public:
   AuthAddressConfirmDialog(const bs::Address& address, const std::shared_ptr<AuthAddressManager>& authManager
      ,QWidget* parent = nullptr );
   ~AuthAddressConfirmDialog() override = default;

private slots:
   void onUiTimerTick();
   void onCancelPressed();

   void onError(const QString &errorText);
   void onAuthAddrSubmitError(const QString &address, const QString &error);
   void onAuthConfirmSubmitError(const QString &address, const QString &error);
   void onAuthAddrSubmitSuccess(const QString &address);
   void onAuthAddressSubmitCancelled(const QString &address);
   void onSignFailed(const QString &text);

private:
   void CancelSubmission();

private:
   Ui::AuthAddressConfirmDialog* ui_;

   bs::Address                         address_;
   std::shared_ptr<AuthAddressManager> authManager_;

   QTimer                                 progressTimer_;
   std::chrono::steady_clock::time_point  startTime_;
};

#endif // AUTH_ADDRESS_CONFIRMATION_DIALOG_H__