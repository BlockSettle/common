#include "ManageEncryptionDialog.h"
#include "ui_ManageEncryptionDialog.h"

#include <spdlog/spdlog.h>
#include <QToolButton>
#include "ApplicationSettings.h"
#include "EnterWalletNewPassword.h"
#include "HDWallet.h"
#include "BSMessageBox.h"
#include "SignContainer.h"
#include "WalletKeyNewWidget.h"
#include "WalletKeysDeleteDevice.h"
#include "WalletKeysSubmitNewWidget.h"
#include "WalletKeysCreateNewWidget.h"


ManageEncryptionDialog::ManageEncryptionDialog(const std::shared_ptr<spdlog::logger> &logger
      , std::shared_ptr<SignContainer> signingContainer
      , const std::shared_ptr<bs::hd::Wallet> &wallet
      , const bs::hd::WalletInfo &walletInfo
      , const std::shared_ptr<ApplicationSettings> &appSettings
      , QWidget* parent)
   : QDialog(parent)
   , ui_(new Ui::ManageEncryptionDialog())
   , logger_(logger)
   , signingContainer_(signingContainer)
   , wallet_(wallet)
   , walletInfo_(walletInfo)
   , appSettings_(appSettings)
{
   ui_->setupUi(this);
   ui_->labelWalletId->setText(walletInfo.rootId());
   ui_->labelWalletName->setText(walletInfo.name());


   //////////////

   resetKeys();

   connect(ui_->tabWidget, &QTabWidget::currentChanged, this, &ManageEncryptionDialog::onTabChanged);
   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &ManageEncryptionDialog::reject);
   connect(ui_->pushButtonOk, &QPushButton::clicked, this, &ManageEncryptionDialog::onContinueClicked);

   connect(signingContainer_.get(), &SignContainer::PasswordChanged, this, &ManageEncryptionDialog::onPasswordChanged);

//   deviceKeyOld_ = new WalletKeyNewWidget(AutheIDClient::ActivateWalletOldDevice
//      , wallet->getWalletId(), 0, false, appSettings->GetAuthKeys(), this);
//   deviceKeyOld_->setFixedType(true);
//   deviceKeyOld_->setEncryptionKeys(encKeys);
//   deviceKeyOld_->setHideAuthConnect(true);
//   deviceKeyOld_->setHideAuthCombobox(true);

//   deviceKeyNew_ = new WalletKeyNewWidget(AutheIDClient::ActivateWalletNewDevice
//      , wallet->getWalletId(), 0, false, appSettings->GetAuthKeys(), this);
//   deviceKeyNew_->setFixedType(true);
//   deviceKeyNew_->setEncryptionKeys(encKeys);
//   deviceKeyNew_->setHideAuthConnect(true);
//   deviceKeyNew_->setHideAuthCombobox(true);
   
//   QBoxLayout *deviceLayout = dynamic_cast<QBoxLayout*>(ui_->tabAddDevice->layout());
//   deviceLayout->insertWidget(deviceLayout->indexOf(ui_->labelDeviceOldInfo) + 1, deviceKeyOld_);
//   deviceLayout->insertWidget(deviceLayout->indexOf(ui_->labelDeviceNewInfo) + 1, deviceKeyNew_);

//   //connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::keyChanged, [this] { updateState(); });
//   //connect(ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::keyCountChanged, [this] { adjustSize(); });
//   //connect(ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::keyChanged, [this] { updateState(); });
//   connect(deviceKeyOld_, &WalletKeyNewWidget::keyChanged, this, &ManageEncryptionDialog::onOldDeviceKeyChanged);
//   connect(deviceKeyOld_, &WalletKeyNewWidget::failed, this, &ManageEncryptionDialog::onOldDeviceFailed);

//   connect(deviceKeyNew_, &WalletKeyNewWidget::encKeyChanged, this, &ManageEncryptionDialog::onNewDeviceEncKeyChanged);
//   connect(deviceKeyNew_, &WalletKeyNewWidget::keyChanged, this, &ManageEncryptionDialog::onNewDeviceKeyChanged);
//   connect(deviceKeyNew_, &WalletKeyNewWidget::failed, this, &ManageEncryptionDialog::onNewDeviceFailed);

   QString usernameAuthApp;
   int authCount = 0;
   for (const auto &encKey : walletInfo_.encKeys()) {   // assume we can have encKeys only for Auth type
      bs::wallet::QPasswordData passwordData{};
      passwordData.qSetEncType(bs::wallet::QEncryptionType::Auth);
      passwordData.qSetEncKey(encKey);

      oldPasswordData_.push_back(passwordData);
      auto deviceInfo = AutheIDClient::getDeviceInfo(encKey.toStdString());

      if (!deviceInfo.userId.empty()) {
         authCount += 1;

         usernameAuthApp = QString::fromStdString(deviceInfo.userId);

         QString deviceName;
         if (!deviceInfo.deviceName.empty()) {
            deviceName = QString::fromStdString(deviceInfo.deviceName);
         } else {
            deviceName = QString(tr("Device %1")).arg(authCount);
         }

         WalletKeysDeleteDevice *deviceWidget = new WalletKeysDeleteDevice(deviceName);

         ui_->verticalLayoutDeleteDevices->insertWidget(authCount - 1, deviceWidget);

         connect(deviceWidget, &WalletKeysDeleteDevice::deleteClicked, this, [this, deviceInfo] {
            deleteDevice(deviceInfo.deviceId);
         });
      }
   }

   for (const auto &encType : walletInfo_.encTypes()) {
      if (encType == bs::wallet::QEncryptionType::Auth) {    // already added encKeys for Auth type
         continue;
      }
      bs::wallet::QPasswordData passwordData{};
      passwordData.qSetEncType(encType);
      oldPasswordData_.push_back(passwordData);
   }

   if (authCount > 0) {
      ui_->groupBoxCurrentEncryption->setTitle(tr("CURRENT ENCRYPTION"));
   }
   else {
      ui_->groupBoxCurrentEncryption->setTitle(tr("ENTER PASSWORD"));
   }

//   if (!usernameAuthApp.isEmpty()) {
//      deviceKeyOld_->init(appSettings, usernameAuthApp);
//      deviceKeyNew_->init(appSettings, usernameAuthApp);
//   }

   //////////////

   ui_->widgetSubmitKeys->setFlags(WalletKeysSubmitNewWidget::HideGroupboxCaption
      | WalletKeysSubmitNewWidget::SetPasswordLabelAsOld
      | WalletKeysSubmitNewWidget::HideAuthConnectButton
      | WalletKeysSubmitNewWidget::HidePasswordWarning);
   ui_->widgetSubmitKeys->suspend();
   ui_->widgetSubmitKeys->init(AutheIDClient::DeactivateWallet, walletInfo, WalletKeyNewWidget::UseType::RequestAuthForDialog, appSettings, logger_);

   ui_->widgetCreateKeys->setFlags(WalletKeysCreateNewWidget::HideGroupboxCaption
      | WalletKeysCreateNewWidget::SetPasswordLabelAsNew
      | WalletKeysCreateNewWidget::HideAuthConnectButton
      | WalletKeysCreateNewWidget::HideWidgetContol);
   ui_->widgetCreateKeys->init(AutheIDClient::ActivateWallet, walletInfo_, WalletKeyNewWidget::UseType::ChangeAuthForDialog, appSettings, logger_);

   ui_->widgetSubmitKeys->setFocus();

   ui_->tabWidget->setCurrentIndex(int(Pages::Basic));

   connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::returnPressed, ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::setFocus);
   connect(ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::returnPressed, this, &ManageEncryptionDialog::onContinueClicked);

   updateState();
}

ManageEncryptionDialog::~ManageEncryptionDialog() = default;

void ManageEncryptionDialog::accept()
{
   onContinueClicked();
}

void ManageEncryptionDialog::reject()
{
   //ui_->widgetSubmitKeys->cancel();
   //ui_->widgetCreateKeys->cancel();
//   deviceKeyOld_->cancel();
//   deviceKeyNew_->cancel();

   QDialog::reject();
}

void ManageEncryptionDialog::onContinueClicked()
{
   // Is this accurate? Shouldn't we wait until the change is confirmed?
   resetKeys();

   if (ui_->tabWidget->currentIndex() == int(Pages::Basic)) {
      continueBasic(); // Password
   } else {
      continueAddDevice(); // Auth eID
   }
}

// Change the wallet's password.
void ManageEncryptionDialog::continueBasic()
{
   std::vector<bs::wallet::QPasswordData> newKeys = ui_->widgetCreateKeys->keys();

   bool isOldAuth = !oldPasswordData_.empty() && oldPasswordData_[0].encType == bs::wallet::EncryptionType::Auth;
   bool isNewAuth = !newKeys.empty() && newKeys[0].encType == bs::wallet::EncryptionType::Auth;

   if (!ui_->widgetSubmitKeys->isValid() && !isOldAuth) {
      BSMessageBox messageBox(BSMessageBox::critical, tr("Invalid password"),
                                    tr("Please check old password."),
                                    this);
      messageBox.exec();
      return;
   }

   if (!ui_->widgetCreateKeys->isValid() && !isNewAuth) {
      BSMessageBox messageBox(BSMessageBox::critical, tr("Invalid password"),
                                    tr("Please check new password, and make " \
                                       "sure the length is at least six (6) " \
                                       "characters long."),
                                    this);
      messageBox.exec();
      return;
   }

   if (!ui_->widgetCreateKeys->isValid() && isNewAuth) {
      BSMessageBox messageBox(BSMessageBox::critical, tr("Invalid new Auth eID"),
                                    tr("Please use valid Auth eID."),
                                    this);
      messageBox.exec();
      return;
   }

   if (isOldAuth && isNewAuth) {
      bool sameAuthId = true;
      for (const auto &oldPassData : oldPasswordData_) {
         auto deviceInfo = AutheIDClient::getDeviceInfo(oldPassData.encKey.toBinStr());
         if (deviceInfo.userId != newKeys[0].encKey.toBinStr()) {
            sameAuthId = false;
         }
      }
      if (sameAuthId) {
         BSMessageBox messageBox(BSMessageBox::critical, tr("Invalid new Auth eID")
            , tr("Please use different Auth eID. Same Auth eID is already used."), this);
         messageBox.exec();
         return;
      }
   }

   bool showAuthUsageInfo = true;

   if (isOldAuth)
   {
      showAuthUsageInfo = false;

      if (oldPasswordData_[0].password.isNull()) {
         EnterWalletNewPassword enterWalletPassword(AutheIDClient::DeactivateWallet, this);
         enterWalletPassword.init(walletInfo_, appSettings_, WalletKeyNewWidget::UseType::RequestAuthAsDialog, tr("Change Encryption"), logger_);
         int result = enterWalletPassword.exec();
         if (result != QDialog::Accepted) {
            return;
         }

         oldKey_ = enterWalletPassword.resultingKey();
      }
   }
   else {
      oldKey_ = ui_->widgetSubmitKeys->key();
   }

   if (isNewAuth) {
      if (showAuthUsageInfo) {
         MessageBoxAuthNotice authNotice(this);
         int result = authNotice.exec();
         if (result != QDialog::Accepted) {
            return;
         }
      }

      EnterWalletNewPassword enterWalletPassword(AutheIDClient::ActivateWallet, this);
      // overwrite encKeys
      bs::hd::WalletInfo wi = walletInfo_;
      wi.setEncTypes(QList<bs::wallet::QEncryptionType>() << ui_->widgetCreateKeys->passwordData(0).qEncType());
      wi.setEncKeys(QList<QString>() << ui_->widgetCreateKeys->passwordData(0).qEncKey());

      enterWalletPassword.init(wi, appSettings_, WalletKeyNewWidget::UseType::ChangeToEidAsDialog, tr("Activate Auth eID signing"), logger_);
      int result = enterWalletPassword.exec();
      if (result != QDialog::Accepted) {
         return;
      }

      newKeys[0] = enterWalletPassword.passwordData(0);
   }
   else {
      newKeys[0] = ui_->widgetCreateKeys->passwordData(0);
   }

   newPasswordData_ = newKeys;
   newKeyRank_ = ui_->widgetCreateKeys->keyRank();
   changePassword();
}

// Add a new Auth eID device to the wallet.
void ManageEncryptionDialog::continueAddDevice()
{
   if (state_ != State::Idle) {
      return;
   }

   if (walletInfo_.keyRank().first != 1) {
      BSMessageBox messageBox(BSMessageBox::critical, tr("Add Device error")
         , tr("Only 1-of-N AuthApp encryption supported"), this);
      messageBox.exec();
      return;
   }

   if (oldPasswordData_.empty() || oldPasswordData_[0].encType != bs::wallet::EncryptionType::Auth) {
      BSMessageBox messageBox(BSMessageBox::critical, tr("Add Device")
         , tr("Auth eID encryption"), tr("Auth eID is not enabled"), this);
      messageBox.exec();
      return;
   }

   std::vector<bs::wallet::QPasswordData> newKeys = ui_->widgetCreateKeys->keys();

   // Request eid auth to decrypt wallet
//   if (oldPasswordData_[0].password.isNull()) {

//   }

   {
      EnterWalletNewPassword enterWalletOldPassword(AutheIDClient::ActivateWalletOldDevice, this);
      enterWalletOldPassword.init(walletInfo_, appSettings_, WalletKeyNewWidget::UseType::RequestAuthAsDialog, tr("Change Encryption"), logger_);
      int result = enterWalletOldPassword.exec();
      if (result != QDialog::Accepted) {
         return;
      }
      oldKey_ = enterWalletOldPassword.resultingKey();
   }


   // Request eid activate new device
   {
      EnterWalletNewPassword enterWalletNewPassword(AutheIDClient::ActivateWalletNewDevice, this);
      // overwrite encKeys
      bs::hd::WalletInfo wi = walletInfo_;
      //wi.setEncTypes(QList<bs::wallet::QEncryptionType>() << ui_->widgetCreateKeys->passwordData(0).qEncType());
      //wi.setEncKeys(QList<QString>() << ui_->widgetCreateKeys->passwordData(0).qEncKey());

      enterWalletNewPassword.init(walletInfo_, appSettings_, WalletKeyNewWidget::UseType::ChangeToEidAsDialog, tr("Activate Auth eID signing"), logger_);
      int result = enterWalletNewPassword.exec();
      if (result != QDialog::Accepted) {
         return;
      }
      newKeys[0] = enterWalletNewPassword.passwordData(0);
   }



   // Add device
   bs::wallet::KeyRank encryptionRank = walletInfo_.keyRank();
   encryptionRank.second++;

   // TODO add to bs::hd::wallet overload for QPasswordData
   bool result = wallet_->changePassword({ newKeys[0] }, encryptionRank
      , oldKey_, true, false, false);

   if (!result) {
      BSMessageBox messageBox(BSMessageBox::critical, tr("Add Device")
         , tr("Failed to add new device"), this);
      messageBox.exec();
      return;
   }
   else {
      BSMessageBox messageBox(BSMessageBox::success, tr("Add Device")
         , tr("Device successfully added"), this);
      messageBox.exec();
   }


//   if (!deviceKeyOldValid_) {
//      deviceKeyOld_->start();
//      state_ = State::AddDeviceWaitOld;
//   } else {
//      deviceKeyNew_->start();
//      state_ = State::AddDeviceWaitNew;
//   }

   updateState();
}

void ManageEncryptionDialog::changePassword()
{
   std::vector<bs::wallet::PasswordData> pwData;
   pwData.assign(newPasswordData_.cbegin(), newPasswordData_.cend());
   if (wallet_->isWatchingOnly()) {
      signingContainer_->ChangePassword(wallet_, pwData, newKeyRank_, oldKey_
         , addNew_, removeOld_, false);
   }
   else {
      bool result = wallet_->changePassword(pwData, newKeyRank_, oldKey_
         , addNew_, removeOld_, false);
      onPasswordChanged(wallet_->getWalletId(), result);
   }
}

void ManageEncryptionDialog::resetKeys()
{
   oldKey_.clear();
   deviceKeyOldValid_ = false;
   deviceKeyNewValid_ = false;
   isLatestChangeAddDevice_ = false;
   addNew_ = false;
   removeOld_ = false;
}

//void ManageEncryptionDialog::onOldDeviceKeyChanged(int, SecureBinaryData password)
//{
//   deviceKeyOldValid_ = true;
//   state_ = State::AddDeviceWaitNew;
//   //deviceKeyNew_->start();
//   oldKey_ = password;
//   updateState();
//}

//void ManageEncryptionDialog::onOldDeviceFailed()
//{
//   state_ = State::Idle;
//   updateState();

//   BSMessageBox(BSMessageBox::critical, tr("Wallet Encryption")
//      , tr("A problem occured requesting old device key")
//      , this).exec();
//}

//void ManageEncryptionDialog::onNewDeviceEncKeyChanged(int index, SecureBinaryData encKey)
//{
//   bs::wallet::PasswordData newPassword{};
//   newPassword.encType = bs::wallet::EncryptionType::Auth;
//   newPassword.encKey = encKey;
//   newPasswordData_.clear();
//   newPasswordData_.push_back(newPassword);
//}

//void ManageEncryptionDialog::onNewDeviceKeyChanged(int index, SecureBinaryData password)
//{
////   if (newPasswordData_.empty()) {
////      logger_->error("Internal error: newPasswordData_.empty()");
////      return;
////   }

////   newPasswordData_.back().password = password;

////   newKeyRank_ = oldKeyRank_;
////   newKeyRank_.second += 1;

////   isLatestChangeAddDevice_ = true;
////   addNew_ = true;
////   changePassword();
//}

//void ManageEncryptionDialog::onNewDeviceFailed()
//{
//   state_ = State::Idle;
//   updateState();

//   BSMessageBox(BSMessageBox::critical, tr("Wallet Encryption")
//      , tr("A problem occured requesting new device key")
//      , this).exec();
//}

void ManageEncryptionDialog::onPasswordChanged(const string &walletId, bool ok)
{
   if (walletId != wallet_->getWalletId()) {
      logger_->error("ManageEncryptionDialog::onPasswordChanged: unknown walletId {}, expected: {}"
         , walletId, wallet_->getWalletId());
      return;
   }

   if (!ok) {
      logger_->error("ChangeWalletPassword failed for {}", walletId);

      if (isLatestChangeAddDevice_) {
         BSMessageBox(BSMessageBox::critical, tr("Wallet Encryption")
            , tr("Device adding failed")
            , this).exec();
      } else {
         BSMessageBox(BSMessageBox::critical, tr("Wallet Encryption")
            , tr("A problem occured when changing wallet password")
            , this).exec();
      }

      state_ = State::Idle;
      updateState();
      return;
   }

   if (isLatestChangeAddDevice_) {
      BSMessageBox(BSMessageBox::success, tr("Wallet Encryption")
         , tr("Device successfully added")
         , this).exec();
   } else {
      BSMessageBox(BSMessageBox::success, tr("Wallet Encryption")
         , tr("Wallet encryption successfully changed")
         , this).exec();
   }

   QDialog::accept();
}

void ManageEncryptionDialog::deleteDevice(const string &deviceId)
{
//   newPasswordData_.clear();
//   for (const auto &passwordData : oldPasswordData_) {
//      auto deviceInfo = AutheIDClient::getDeviceInfo(passwordData.encKey.toBinStr());
//      if (deviceInfo.deviceId != deviceId) {
//         newPasswordData_.push_back(passwordData);
//      }
//   }
//   newKeyRank_ = oldKeyRank_;
//   newKeyRank_.second -= 1;

//   if (newKeyRank_.first != 1) {
//      // Something went wrong. Only 1-on-N scheme is supported
//      logger_->critical("ManageEncryptionDialog: newKeyRank.first != 1");
//      return;
//   }

//   if (newKeyRank_.second != newPasswordData_.size()) {
//      // Something went wrong
//      logger_->critical("internal error: oldKeyRank_.second != newPasswordData.size()");
//      return;
//   }

//   if (newPasswordData_.size() == oldPasswordData_.size()) {
//      // Something went wrong
//      logger_->critical("internal error: newPasswordData.size() == oldPasswordData_.size()");
//      return;
//   }

//   if (newKeyRank_.second == 0) {
//      BSMessageBox(BSMessageBox::critical, tr("Error")
//         , tr("Cannot remove last device. Please switch to password encryption instead."), this).exec();
//      return;
//   }

//   EnterWalletNewPassword enterWalletPassword(AutheIDClient::DeactivateWalletDevice, this);
//   enterWalletPassword.init(wallet_->getWalletId(), newKeyRank_
//      , newPasswordData_, appSettings_, tr("Deactivate device"));
//   int result = enterWalletPassword.exec();
//   if (result != QDialog::Accepted) {
//      return;
//   }

//   oldKey_ = enterWalletPassword.getPassword();
//   removeOld_ = true;

//   changePassword();
}

void ManageEncryptionDialog::onTabChanged(int index)
{
   state_ = State::Idle;
   updateState();
}

void ManageEncryptionDialog::updateState()
{
   Pages currentPage = Pages(ui_->tabWidget->currentIndex());

   if (currentPage == Pages::Basic) {
      ui_->pushButtonOk->setText(tr("Continue"));
   } else {
      ui_->pushButtonOk->setText(tr("Add Device"));
   }

   ui_->labelAddDeviceInfo->setVisible(state_ == State::Idle);
   ui_->labelDeviceOldInfo->setVisible(state_ == State::AddDeviceWaitOld || state_ == State::AddDeviceWaitNew);
   ui_->labelAddDeviceSuccess->setVisible(state_ == State::AddDeviceWaitNew);
   ui_->labelDeviceNewInfo->setVisible(state_ == State::AddDeviceWaitNew);
   ui_->lineAddDevice->setVisible(state_ == State::AddDeviceWaitNew);
   //deviceKeyOld_->setVisible(state_ == State::AddDeviceWaitOld);
   //deviceKeyNew_->setVisible(state_ == State::AddDeviceWaitNew);
}
