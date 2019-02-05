#ifndef __ENTER_WALLET_NEW_PASSWORD_H__
#define __ENTER_WALLET_NEW_PASSWORD_H__

#include <memory>
#include <string>
#include <QDialog>
#include <QTimer>
#include "EncryptionUtils.h"
#include "MetaData.h"
#include "AutheIDClient.h"
#include "QWalletInfo.h"
#include "WalletKeyNewWidget.h"
#include "ui_EnterWalletNewPassword.h"

namespace Ui {
    class EnterWalletNewPassword;
}
class ApplicationSettings;

class EnterWalletNewPassword : public QDialog
{
Q_OBJECT

public:
   explicit EnterWalletNewPassword(AutheIDClient::RequestType requestType
                                   , QWidget* parent = nullptr);
   ~EnterWalletNewPassword() override;

//   enum class UseType {
//      RequestPassword,
//      RequestEid,
//      ChangeToPassword,  // not used
//      ChangeToEid
//   };

   void init(const bs::hd::WalletInfo &walletInfo
      , const std::shared_ptr<ApplicationSettings> &appSettings
      , WalletKeyNewWidget::UseType useType
      , const QString &prompt
      , const std::shared_ptr<spdlog::logger> &logger
      , const QString &title = QString());


//   void init(const bs::hd::WalletInfo &walletInfo
//      , const std::vector<bs::wallet::PasswordData> &keys
//      , const std::shared_ptr<ApplicationSettings> &appSettings
//      , const QString &prompt, const QString &title = QString());

   bs::wallet::QPasswordData passwordData(int keyIndex) const { return ui_->widgetSubmitKeys->passwordData(keyIndex); }
   SecureBinaryData resultingKey() const;
   std::vector<bs::wallet::QPasswordData> passwordData() const { return ui_->widgetSubmitKeys->passwordData(); }

private slots:
   void updateState();

protected:
   void reject() override;

private:
   std::unique_ptr<Ui::EnterWalletNewPassword> ui_;
   AutheIDClient::RequestType requestType_{};
   bool fixEidAuth_;
   std::shared_ptr<spdlog::logger> logger_;
   bs::hd::WalletInfo walletInfo_;
   std::shared_ptr<ApplicationSettings> appSettings_;
};

#endif // __ENTER_WALLET_NEW_PASSWORD_H__
