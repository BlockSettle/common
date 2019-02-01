#include "WalletBackupNewDialog.h"
#include "ui_WalletBackupNewDialog.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>

#include "HDWallet.h"
#include "BSMessageBox.h"
#include "PaperBackupWriter.h"
#include "SignContainer.h"
#include "WalletBackupFile.h"
#include "UiUtils.h"
#include "WalletWarningDialog.h"
#include "VerifyWalletBackupDialog.h"
#include "WalletKeysSubmitNewWidget.h"
#include "EnterWalletNewPassword.h"

WalletBackupNewDialog::WalletBackupNewDialog(const std::shared_ptr<bs::hd::Wallet> &wallet
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<spdlog::logger> &logger
   , QWidget *parent)
   : QDialog(parent)
   , ui_(new Ui::WalletBackupNewDialog)
   , wallet_(wallet)
   , signingContainer_(container)
   , outputDir_(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString())
   , logger_(logger)
   , appSettings_(appSettings)
{
   ui_->setupUi(this);

   //ui_->pushButtonBackup->setEnabled(false);
   ui_->labelFileName->clear();

   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &WalletBackupNewDialog::reject);
   connect(ui_->pushButtonBackup, &QPushButton::clicked, this, &WalletBackupNewDialog::onBackupClicked);
   connect(ui_->pushButtonSelectFile, &QPushButton::clicked, this, &WalletBackupNewDialog::onSelectFile);
   connect(ui_->radioButtonTextFile, &QRadioButton::clicked, this, &WalletBackupNewDialog::textFileClicked);
   connect(ui_->radioButtonPDF, &QRadioButton::clicked, this, &WalletBackupNewDialog::pdfFileClicked);

   if (signingContainer_ && !signingContainer_->isOffline()) {
      connect(signingContainer_.get(), &SignContainer::DecryptedRootKey, this, &WalletBackupNewDialog::onRootKeyReceived);
      connect(signingContainer_.get(), &SignContainer::QWalletInfo, this, &WalletBackupNewDialog::onWalletInfo);
      connect(signingContainer_.get(), &SignContainer::Error, this, &WalletBackupNewDialog::onContainerError);

      infoReqId_ = signingContainer_->GetInfo(wallet_);
   }

   outputFile_ = outputDir_ + "/backup_wallet_" + wallet->getName() + "_" + wallet->getWalletId();
   textFileClicked();
}

WalletBackupNewDialog::~WalletBackupNewDialog() = default;

bool WalletBackupNewDialog::isDigitalBackup() const
{
   return ui_->radioButtonTextFile->isChecked();
}

QString WalletBackupNewDialog::filePath() const
{
   const std::string ext = isDigitalBackup() ? ".wdb" : ".pdf";
   return selectedFile_.isEmpty() ? QString::fromStdString(outputFile_ + ext) : selectedFile_;
}

void WalletBackupNewDialog::onRootKeyReceived(unsigned int id, const SecureBinaryData &privKey
   , const SecureBinaryData &chainCode, std::string walletId)
{
   if (!privKeyReqId_ || (id != privKeyReqId_)) {
      return;
   }
   privKeyReqId_ = 0;
   EasyCoDec::Data easyData, edChainCode;
   try {
      easyData = bs::wallet::Seed(NetworkType::Invalid, privKey).toEasyCodeChecksum();
      if (!chainCode.isNull()) {
         edChainCode = bs::wallet::Seed(NetworkType::Invalid, chainCode).toEasyCodeChecksum();
      }
   }
   catch (const std::exception &e) {
      showError(tr("Failed to encode private key"), QLatin1String(e.what()));
      reject();
      return;
   }

   QFile f(filePath());
   if (f.exists()) {
      BSMessageBox qRewrite(BSMessageBox::question, tr("Wallet Backup"), tr("Backup already exists")
         , tr("File %1 already exists. Do you want to overwrite it?").arg(filePath()), this);
      if (qRewrite.exec() != QDialog::Accepted) {
         return;
      }
   }

   if (isDigitalBackup()) {
      if (!f.open(QIODevice::WriteOnly)) {
         showError(tr("Failed to save backup file"), tr("Unable to open digital file %1 for writing").arg(f.fileName()));
         reject();
         return;
      }
      WalletBackupFile backupData(wallet_, easyData, edChainCode);

      f.write(QByteArray::fromStdString(backupData.Serialize()));
      f.close();
   }
   else {
      try {
         WalletBackupPdfWriter pdfWriter(QString::fromStdString(wallet_->getWalletId()),
            QString::fromStdString(easyData.part1),
            QString::fromStdString(easyData.part2),
            QPixmap(QLatin1String(":/resources/logo_print-250px-300ppi.png")),
            UiUtils::getQRCode(QString::fromStdString(easyData.part1 + "\n" + easyData.part2)));

         if (!pdfWriter.write(filePath())) {
            throw std::runtime_error("write failure");
         }
      }
      catch (const std::exception &e) {
         showError(tr("Failed to output PDF"), QLatin1String(e.what()));
         reject();
         return;
      }
   }
   QDialog::accept();
}

void WalletBackupNewDialog::onWalletInfo(unsigned int id, const bs::hd::WalletInfo &walletInfo)
{
   if (!infoReqId_ || (id != infoReqId_)) {
      return;
   }
   infoReqId_ = 0;
   walletInfo_ = walletInfo;

   ui_->widgetSubmitKeys->init(AutheIDClient::BackupWallet, walletInfo
      , WalletKeyNewWidget::UseType::RequestAuthForDialog, appSettings_, logger_, tr("Backup keys"));
   ui_->widgetSubmitKeys->setFocus();

   QApplication::processEvents();
   adjustSize();
}

void WalletBackupNewDialog::showError(const QString &title, const QString &text)
{
   BSMessageBox(BSMessageBox::critical, title, text).exec();
}

void WalletBackupNewDialog::onContainerError(unsigned int id, std::string errMsg)
{
   if (id == infoReqId_) {
      infoReqId_ = 0;
      ui_->labelTypeDesc->setText(tr("Wallet info request failed: %1").arg(QString::fromStdString(errMsg)));
   }
   else if (id == privKeyReqId_) {
      privKeyReqId_ = 0;
      showError(tr("Private Key Error"), tr("Failed to get private key from signing process: %1")
         .arg(QString::fromStdString(errMsg)));
   }
}

void WalletBackupNewDialog::textFileClicked()
{
   if (!ui_->radioButtonTextFile->isChecked()) {
      ui_->labelTypeDesc->clear();
   }
   else {
      ui_->labelTypeDesc->setText(tr("Output decrypted private key to a text file"));
      if (selectedFile_.isEmpty()) {
         ui_->labelFileName->setText(QString::fromStdString(outputFile_ + ".wdb"));
      }
   }
}

void WalletBackupNewDialog::pdfFileClicked()
{
   if (!ui_->radioButtonPDF->isChecked()) {
      ui_->labelTypeDesc->clear();
   }
   else {
      ui_->labelTypeDesc->setText(tr("Output decrypted private key to a PDF file in printable format"));
      if (selectedFile_.isEmpty()) {
         ui_->labelFileName->setText(QString::fromStdString(outputFile_ + ".pdf"));
      }
   }
}

void WalletBackupNewDialog::onBackupClicked()
{
   if (walletInfo_.isEidAuthOnly()) {
      // Request eid auth to decrypt wallet
      EnterWalletNewPassword enterWalletOldPassword(AutheIDClient::BackupWallet, this);
      enterWalletOldPassword.init(walletInfo_, appSettings_, WalletKeyNewWidget::UseType::RequestAuthAsDialog, tr("Backup Wallet"), logger_);
      int result = enterWalletOldPassword.exec();
      if (result != QDialog::Accepted) {
         return;
      }

      privKeyReqId_ = signingContainer_->GetDecryptedRootKey(wallet_, enterWalletOldPassword.resultingKey());
   }
   else {
      privKeyReqId_ = signingContainer_->GetDecryptedRootKey(wallet_, ui_->widgetSubmitKeys->key());
   }
}

void WalletBackupNewDialog::onSelectFile()
{
   bool isText = ui_->radioButtonTextFile->isChecked();
   QFileDialog dlg;
   dlg.setFileMode(QFileDialog::AnyFile);
   selectedFile_ = dlg.getSaveFileName(this, tr("Select file for backup"), filePath()
      , isText ? QLatin1String("*.wdb") : QLatin1String("*.pdf"));
   if (!selectedFile_.isEmpty()) {
      QFileInfo fi(selectedFile_);
      outputDir_ = fi.path().toStdString();
      ui_->labelFileName->setText(selectedFile_);
   }
}

void WalletBackupNewDialog::reject()
{
   BSMessageBox confCancel(BSMessageBox::question, tr("Warning"), tr("Abort Backup Process?")
      , tr("BlockSettle strongly encourages you to take the necessary precautions to ensure you backup your"
         " private keys. Are you sure you wish to abort this process?"), this);
   confCancel.setConfirmButtonText(tr("Yes"));
   confCancel.setCancelButtonText(tr("No"));

   if (confCancel.exec() == QDialog::Accepted) {
      ui_->widgetSubmitKeys->cancel();
      QDialog::reject();
   }
}


bool WalletBackupAndNewVerify(const std::shared_ptr<bs::hd::Wallet> &wallet
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<spdlog::logger> &logger
   , QWidget *parent)
{
   if (!wallet) {
      return false;
   }
   WalletBackupNewDialog walletBackupDialog(wallet, container, appSettings, logger, parent);
   if (walletBackupDialog.exec() == QDialog::Accepted) {
      BSMessageBox(BSMessageBox::success, QObject::tr("Backup"), QObject::tr("%1 Backup successfully created")
         .arg(walletBackupDialog.isDigitalBackup() ? QObject::tr("Digital") : QObject::tr("Paper"))
            , walletBackupDialog.filePath(), parent).exec();
      if (!walletBackupDialog.isDigitalBackup()) {
         VerifyWalletBackupDialog(wallet, logger, parent).exec();
      }
      WalletWarningDialog(parent).exec();
      return true;
   }

   return false;
}
