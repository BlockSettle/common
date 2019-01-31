#ifndef __MANAGE_ENCRYPTION_NEW_DIALOG_H__
#define __MANAGE_ENCRYPTION_NEW_DIALOG_H__

#include <memory>
#include <QDialog>
#include "EncryptionUtils.h"
#include "MetaData.h"
#include "AutheIDClient.h"
#include "QWalletInfo.h"

namespace Ui {
    class ManageEncryptionDialog;
}
namespace bs {
   namespace hd {
      class Wallet;
   }
}

class WalletKeyNewWidget;
class ApplicationSettings;
class SignContainer;

class ManageEncryptionDialog : public QDialog
{
Q_OBJECT

public:
   enum class Pages {
      Basic,
      AddDevice,
   };

   enum class State {
      Idle,
      AddDeviceWaitOld,
      AddDeviceWaitNew,
   };

   ManageEncryptionDialog(const std::shared_ptr<spdlog::logger> &logger
      , std::shared_ptr<SignContainer> signingContainer
      , const std::shared_ptr<bs::hd::Wallet> &wallet
      , const bs::hd::WalletInfo &walletInfo
      , const std::shared_ptr<ApplicationSettings> &appSettings
      , QWidget* parent = nullptr);
   ~ManageEncryptionDialog() override;

private slots:
   void onContinueClicked();
   void onTabChanged(int index);

//   void onOldDeviceKeyChanged(int, SecureBinaryData);
//   void onOldDeviceFailed();

//   void onNewDeviceEncKeyChanged(int index, SecureBinaryData encKey);
//   void onNewDeviceKeyChanged(int index, SecureBinaryData password);
//   void onNewDeviceFailed();

   void onPasswordChanged(const std::string &walletId, bool ok);

protected:
   void accept() override;
   void reject() override;

private:
   void updateState();
   void continueBasic();
   void continueAddDevice();
   void changePassword();
   void resetKeys();
   void deleteDevice(const std::string &deviceId);

   std::unique_ptr<Ui::ManageEncryptionDialog> ui_;
   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<SignContainer> signingContainer_;
   std::shared_ptr<bs::hd::Wallet>  wallet_;
   //const bs::wallet::KeyRank oldKeyRank_;
   bs::wallet::KeyRank newKeyRank_;
   std::vector<bs::wallet::QPasswordData> oldPasswordData_;
   std::vector<bs::wallet::QPasswordData> newPasswordData_;
   // Init variables in resetKeys method so they always valid when we restart process
   bool addNew_;
   bool removeOld_;
   SecureBinaryData oldKey_;
   State state_ = State::Idle;
//   WalletKeyNewWidget *deviceKeyOld_ = nullptr;
//   WalletKeyNewWidget *deviceKeyNew_ = nullptr;
   bool deviceKeyOldValid_;
   bool deviceKeyNewValid_;
   bool isLatestChangeAddDevice_;
   std::shared_ptr<ApplicationSettings> appSettings_;
   bs::hd::WalletInfo walletInfo_;
};

#endif // __MANAGE_ENCRYPTION_NEW_DIALOG_H__
