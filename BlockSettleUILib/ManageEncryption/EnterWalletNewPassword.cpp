#include <QSpacerItem>
#include "EnterWalletNewPassword.h"
#include "WalletKeysSubmitNewWidget.h"
#include "ui_EnterWalletNewPassword.h"
#include <spdlog/spdlog.h>

using namespace bs::wallet;
using namespace bs::hd;

EnterWalletNewPassword::EnterWalletNewPassword(AutheIDClient::RequestType requestType
                                               , QWidget* parent)
   : QDialog(parent)
   , ui_(new Ui::EnterWalletNewPassword())
   , requestType_(requestType)
{
   ui_->setupUi(this);

   connect(ui_->pushButtonOk, &QPushButton::clicked, this, &EnterWalletNewPassword::accept);
   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &EnterWalletNewPassword::reject);

   connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::keyChanged, [this] { updateState(); });
}

EnterWalletNewPassword::~EnterWalletNewPassword() = default;

void EnterWalletNewPassword::init(const WalletInfo &walletInfo
                                  , const std::shared_ptr<ApplicationSettings> &appSettings
                                  , const QString &prompt
                                  , const std::shared_ptr<spdlog::logger> &logger
                                  , const QString &title)
{
   walletInfo_ = walletInfo;
   appSettings_ = appSettings;
   logger_ = logger;

   ui_->labelAction->setText(prompt);
   ui_->labelWalletId->setText(tr("Wallet ID: %1").arg(walletInfo.rootId()));
   ui_->labelWalletName->setText(tr("Wallet name: %1").arg(walletInfo.name()));

   if (!title.isEmpty()) {
      setWindowTitle(title);
   }

//   bool isAuthOnly = true;
//   for (auto encType : walletInfo_.encTypes()) {
//      if (encType != QEncryptionType::Auth) {
//         isAuthOnly = false;
//      }
//   }
//   if (isAuthOnly) {
//      ui_->widgetSubmitKeys->setFlags(WalletKeysSubmitNewWidget::HideAuthConnectButton
//         | WalletKeysSubmitNewWidget::HideAuthCombobox
//         | WalletKeysSubmitNewWidget::HideGroupboxCaption
//         | WalletKeysSubmitNewWidget::AuthProgressBarFixed
//         | WalletKeysSubmitNewWidget::AuthIdVisible
//         | WalletKeysSubmitNewWidget::HidePasswordWarning);

//      connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::keyChanged, this, &EnterWalletNewPassword::accept);
//      connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::failed, this, &EnterWalletNewPassword::reject);

//      ui_->pushButtonOk->hide();
//   }

   ui_->widgetSubmitKeys->init(requestType_, walletInfo_, appSettings_, logger_, prompt);
   ui_->widgetSubmitKeys->setFocus();
   ui_->widgetSubmitKeys->resume();

   updateState();

   adjustSize();
   setMinimumSize(size());
}

void EnterWalletNewPassword::setUseType(EnterWalletNewPassword::UseType useType)
{
   if (useType == UseType::ChangeToEid || useType == UseType::RequestEid) {
      connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::keyChanged, this, &EnterWalletNewPassword::accept);
      connect(ui_->widgetSubmitKeys, &WalletKeysSubmitNewWidget::failed, this, &EnterWalletNewPassword::reject);

      ui_->pushButtonOk->hide();
      ui_->spacerLeft->changeSize(1,1, QSizePolicy::Expanding, QSizePolicy::Preferred);
   }

   //      ui_->widgetSubmitKeys->setFlags(WalletKeysSubmitNewWidget::HideAuthConnectButton
   //                                      | WalletKeysSubmitNewWidget::HideAuthCombobox
   //                                      | WalletKeysSubmitNewWidget::HideGroupboxCaption
   //                                      | WalletKeysSubmitNewWidget::AuthProgressBarFixed
   //                                      | WalletKeysSubmitNewWidget::AuthIdVisible
   //                                      | WalletKeysSubmitNewWidget::HidePasswordWarning);

}

//void EnterWalletNewPassword::init(const std::string &walletId, bs::wallet::KeyRank keyRank
//   , const std::vector<bs::wallet::PasswordData> &keys
//   , const std::shared_ptr<ApplicationSettings> &appSettings
//   , const QString &prompt, const QString &title)
//{
//   std::vector<bs::wallet::EncryptionType> encTypes;
//   std::vector<SecureBinaryData> encKeys;
//   for (const bs::wallet::PasswordData& key : keys) {
//      encTypes.push_back(key.encType);
//      encKeys.push_back(key.encKey);
//   }

//   init(walletId, keyRank, encTypes, encKeys, appSettings, prompt, title);
//}

void EnterWalletNewPassword::updateState()
{
   ui_->pushButtonOk->setEnabled(ui_->widgetSubmitKeys->isValid());
}

void EnterWalletNewPassword::reject()
{
   ui_->widgetSubmitKeys->cancel();
   QDialog::reject();
}

QPasswordData EnterWalletNewPassword::passwordData(int keyIndex) const
{
   return ui_->widgetSubmitKeys->passwordData(keyIndex);
}

SecureBinaryData EnterWalletNewPassword::resultingKey() const
{
    return ui_->widgetSubmitKeys->key();
}
