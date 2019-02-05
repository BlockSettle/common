#include "WalletPasswordVerifyNewDialog.h"

#include "EnterWalletNewPassword.h"
#include "BSMessageBox.h"
#include "ui_WalletPasswordVerifyNewDialog.h"

WalletPasswordVerifyNewDialog::WalletPasswordVerifyNewDialog(const std::shared_ptr<ApplicationSettings> &appSettings
   , QWidget *parent)
   : QDialog(parent)
   , ui_(new Ui::WalletPasswordVerifyNewDialog)
   , appSettings_(appSettings)
{
   ui_->setupUi(this);

   connect(ui_->pushButtonContinue, &QPushButton::clicked, this, &WalletPasswordVerifyNewDialog::onContinueClicked);
   connect(ui_->lineEditPassword, &QLineEdit::textEdited, this, [=](const QString &text) {
      ui_->pushButtonContinue->setDisabled(text.isEmpty());
   });

}

WalletPasswordVerifyNewDialog::~WalletPasswordVerifyNewDialog() = default;

void WalletPasswordVerifyNewDialog::init(const bs::hd::WalletInfo& walletInfo
                                         , const std::vector<bs::wallet::QPasswordData>& keys
                                         , const std::shared_ptr<spdlog::logger> &logger)
{
   walletInfo_ = walletInfo;
   keys_ = keys;
   logger_ = logger;

   const bs::wallet::PasswordData &key = keys.at(0);

   if (key.encType == bs::wallet::EncryptionType::Auth) {
      initAuth(QString::fromStdString(key.encKey.toBinStr()));
   }
   else {
      initPassword();
   }

   runPasswordCheck_ = true;
}

void WalletPasswordVerifyNewDialog::initPassword()
{
   ui_->pushButtonContinue->setEnabled(false);
   ui_->labelAuthHint->hide();
   adjustSize();
}

void WalletPasswordVerifyNewDialog::initAuth(const QString&)
{
   ui_->lineEditPassword->hide();
   ui_->labelPasswordHint->hide();
   ui_->groupPassword->hide();
   adjustSize();
}

void WalletPasswordVerifyNewDialog::onContinueClicked()
{
   const bs::wallet::PasswordData &key = keys_.at(0);

   if (key.encType == bs::wallet::EncryptionType::Password) {
      if (ui_->lineEditPassword->text().toStdString() != key.password.toBinStr()) {
         BSMessageBox errorMessage(BSMessageBox::critical, tr("Warning")
            , tr("Incorrect password"), tr("The password you have entered is incorrect. Please try again."), this);
         errorMessage.exec();
         return;
      }
   }
   
   if (key.encType == bs::wallet::EncryptionType::Auth) {
      EnterWalletNewPassword dialog(AutheIDClient::VerifyWalletKey, this);
      dialog.init(walletInfo_, appSettings_, WalletKeyNewWidget::UseType::RequestAuthAsDialog
                  , tr("Confirm Auth eID Signing"), logger_, tr("Auth eID"));
      int result = dialog.exec();
      if (!result) {
         return;
      }
   }

   accept();
}
