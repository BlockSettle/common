#ifndef __WALLET_PASSWORD_VERIFY_NEW_DIALOG_H__
#define __WALLET_PASSWORD_VERIFY_NEW_DIALOG_H__

#include <memory>
#include <QDialog>
#include "WalletEncryption.h"
#include "QWalletInfo.h"

namespace Ui {
   class WalletPasswordVerifyNewDialog;
}
class ApplicationSettings;

class WalletPasswordVerifyNewDialog : public QDialog
{
   Q_OBJECT

public:
   explicit WalletPasswordVerifyNewDialog(const std::shared_ptr<ApplicationSettings> &appSettings
      , QWidget *parent = nullptr);
   ~WalletPasswordVerifyNewDialog() override;

   // By default the dialog will show only Auth usage info page.
   // If init called then password/Auth check will be used too.
   void init(const bs::hd::WalletInfo &walletInfo
             , const std::vector<bs::wallet::QPasswordData> &keys
             , const std::shared_ptr<spdlog::logger> &logger);

private slots:
   void onContinueClicked();

private:
   void initPassword();
   void initAuth(const QString& authId);

   std::unique_ptr<Ui::WalletPasswordVerifyNewDialog> ui_;
   std::vector<bs::wallet::QPasswordData> keys_;
   bs::hd::WalletInfo walletInfo_;
   bool runPasswordCheck_ = false;
   const std::shared_ptr<ApplicationSettings> appSettings_;
   std::shared_ptr<spdlog::logger> logger_;
};

#endif // __WALLET_PASSWORD_VERIFY_NEW_DIALOG_H__6
