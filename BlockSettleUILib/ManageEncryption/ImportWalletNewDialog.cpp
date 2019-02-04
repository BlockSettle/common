#include "ImportWalletNewDialog.h"
#include "ui_ImportWalletNewDialog.h"

#include "ApplicationSettings.h"
#include "CreateWalletNewDialog.h"
#include "BSMessageBox.h"
#include "SignContainer.h"
#include "WalletImporter.h"
#include "WalletPasswordVerifyDialog.h"
#include "WalletsManager.h"
#include "UiUtils.h"
#include "QWalletInfo.h"

#include <spdlog/spdlog.h>


ImportWalletNewDialog::ImportWalletNewDialog(const std::shared_ptr<WalletsManager> &walletsManager
      , const std::shared_ptr<SignContainer> &container, const std::shared_ptr<AssetManager> &assetMgr
      , const std::shared_ptr<AuthAddressManager> &authMgr, const std::shared_ptr<ArmoryConnection> &armory
      , const EasyCoDec::Data& seedData
      , const EasyCoDec::Data& chainCodeData
      , const std::shared_ptr<ApplicationSettings> &appSettings
      , const std::shared_ptr<spdlog::logger> &logger
      , const QString& username
      , const std::string &walletName, const std::string &walletDesc
      , QWidget *parent)
   : QDialog(parent)
   , ui_(new Ui::ImportWalletNewDialog)
   , walletsMgr_(walletsManager)
   , appSettings_(appSettings)
   , logger_(logger)
   , armory_(armory)
   , walletSeed_(bs::wallet::Seed::fromEasyCodeChecksum(seedData, chainCodeData
      , appSettings->get<NetworkType>(ApplicationSettings::netType)))
{
   walletId_ = bs::hd::Node(walletSeed_).getId();

   ui_->setupUi(this);

   ui_->lineEditDescription->setValidator(new UiUtils::WalletDescriptionValidator(this));
   
   ui_->labelWalletId->setText(QString::fromStdString(walletId_));

   ui_->checkBoxPrimaryWallet->setEnabled(!walletsManager->HasPrimaryWallet());

   if (!walletsManager->HasPrimaryWallet()) {
      setWindowTitle(tr("Import Primary Wallet"));
      ui_->checkBoxPrimaryWallet->setChecked(true);
      ui_->lineEditWalletName->setText(tr("Primary wallet"));
   } else {
      setWindowTitle(tr("Import Wallet"));
      ui_->checkBoxPrimaryWallet->setChecked(false);
      ui_->lineEditWalletName->setText(tr("Wallet #%1").arg(walletsManager->GetWalletsCount() + 1));
   }

   if (!walletName.empty()) {
      ui_->lineEditWalletName->setText(QString::fromStdString(walletName));
   }
   ui_->lineEditDescription->setText(QString::fromStdString(walletDesc));

   const auto &cbr = [] (const std::string &walletId) -> unsigned int {
      return 0;
   };
   const auto &cbw = [appSettings] (const std::string &walletId, unsigned int idx) {
      appSettings->SetWalletScanIndex(walletId, idx);
   };
   walletImporter_ = std::make_shared<WalletImporter>(container, walletsManager, armory
      , assetMgr, authMgr, appSettings_->GetHomeDir(), cbr, cbw);

   connect(walletImporter_.get(), &WalletImporter::walletCreated, this, &ImportWalletNewDialog::onWalletCreated);
   connect(walletImporter_.get(), &WalletImporter::error, this, &ImportWalletNewDialog::onError);

   connect(ui_->lineEditWalletName, &QLineEdit::returnPressed, this, &ImportWalletNewDialog::onImportAccepted);
   connect(ui_->pushButtonImport, &QPushButton::clicked, this, &ImportWalletNewDialog::onImportAccepted);

   connect(ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::keyTypeChanged,
      this, &ImportWalletNewDialog::onKeyTypeChanged);
   connect(ui_->lineEditWalletName, &QLineEdit::textChanged,
      this, &ImportWalletNewDialog::updateAcceptButtonState);
   connect(ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::keyChanged,
      [this] { updateAcceptButtonState(); });

   //connect(ui_->widgetCreateKeys, &WalletKeysCreateNewWidget::keyCountChanged, [this] { adjustSize(); });

//   ui_->widgetCreateKeys->setFlags(WalletKeysCreateNewWidget::HideWidgetContol
//      | WalletKeysCreateNewWidget::HideAuthConnectButton);
   //ui_->widgetCreateKeys->init(AutheIDClient::ActivateWallet, walletId_, username, appSettings);

   bs::hd::WalletInfo walletInfo;
   walletInfo.setRootId(QString::fromStdString(walletId_));

   ui_->widgetCreateKeys->init(AutheIDClient::ActivateWallet
      , walletInfo, WalletKeyNewWidget::UseType::ChangeAuthForDialog, appSettings, logger);

   adjustSize();
   setMinimumSize(size());
}

ImportWalletNewDialog::~ImportWalletNewDialog() = default;

void ImportWalletNewDialog::onError(const QString &errMsg)
{
   BSMessageBox(BSMessageBox::critical, tr("Import wallet error"), errMsg).exec();
   reject();
}

void ImportWalletNewDialog::updateAcceptButtonState()
{
   ui_->pushButtonImport->setEnabled(ui_->widgetCreateKeys->isValid() &&
      !ui_->lineEditWalletName->text().isEmpty());
}

void ImportWalletNewDialog::onKeyTypeChanged(bool password)
{
   if (!password && !authNoticeWasShown_) {
      MessageBoxAuthNotice dlg(this);

      if (dlg.exec() == QDialog::Accepted) {
         authNoticeWasShown_ = true;
      }
   }
}

void ImportWalletNewDialog::onWalletCreated(const std::string &walletId)
{
   if (armory_->state() == ArmoryConnection::State::Ready) {
      emit walletsMgr_->walletImportStarted(walletId);
   }
   else {
      const auto &rootWallet = walletsMgr_->GetHDWalletById(walletId);
      if (rootWallet) {
         for (const auto &leaf : rootWallet->getLeaves()) {
            appSettings_->SetWalletScanIndex(leaf->GetWalletId(), 0);
         }
      }
   }
   accept();
}

void ImportWalletNewDialog::onImportAccepted()
{
   walletName_ = ui_->lineEditWalletName->text();
   const QString &walletDescription = ui_->lineEditDescription->text();
   std::vector<bs::wallet::PasswordData> keys;

   bool result = checkNewWalletValidity(walletsMgr_.get(), walletName_, walletId_
      , ui_->widgetCreateKeys, &keys, appSettings_, this);
   if (!result) {
      return;
   }

   try {
      importedAsPrimary_ = ui_->checkBoxPrimaryWallet->isChecked();

      ui_->pushButtonImport->setEnabled(false);

      walletImporter_->Import(walletName_.toStdString(), walletDescription.toStdString(), walletSeed_
         , importedAsPrimary_, keys, ui_->widgetCreateKeys->keyRank());
   }
   catch (...) {
      onError(tr("Invalid backup data"));
   }
}

bool abortWalletImportQuestionNewDialog(QWidget* parent)
{
   BSMessageBox messageBox(BSMessageBox::question, QObject::tr("Warning"), QObject::tr("Do you want to abort Wallet Import?")
      , QObject::tr("The Wallet will not be imported if you don't complete the procedure.\n\n"
         "Are you sure you want to abort the Wallet Import process?"), parent);
   messageBox.setConfirmButtonText(QObject::tr("Abort"));
   messageBox.setCancelButtonText(QObject::tr("Back"));

   int result = messageBox.exec();
   return (result == QDialog::Accepted);
}

void ImportWalletNewDialog::reject()
{
   bool result = abortWalletImportQuestionNewDialog(this);
   if (!result) {
      return;
   }

   ui_->widgetCreateKeys->cancel();
   QDialog::reject();
}
