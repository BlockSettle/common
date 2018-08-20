#include "CCTokenEntryDialog.h"
#include "ui_CCTokenEntryDialog.h"
#include "bs_communication.pb.h"

#include <QLineEdit>
#include <QPushButton>
#include <spdlog/spdlog.h>
#include "CCFileManager.h"
#include "HDLeaf.h"
#include "HDWallet.h"
#include "MessageBoxCritical.h"
#include "MessageBoxInfo.h"
#include "MessageBoxQuestion.h"
#include "SignContainer.h"
#include "WalletsManager.h"


CCTokenEntryDialog::CCTokenEntryDialog(const std::shared_ptr<WalletsManager> &walletsMgr
      , const std::shared_ptr<CCFileManager> &ccFileMgr
      , const std::shared_ptr<SignContainer> &container
      , QWidget *parent)
   : QDialog(parent)
   , ui_(new Ui::CCTokenEntryDialog())
   , ccFileMgr_(ccFileMgr)
   , signingContainer_(container)
   , walletsMgr_(walletsMgr)
   , freja_(spdlog::get(""))
{
   ui_->setupUi(this);

   connect(ui_->pushButtonOk, &QPushButton::clicked, this, &CCTokenEntryDialog::accept);
   connect(ui_->lineEditToken, &QLineEdit::textEdited, this, &CCTokenEntryDialog::tokenChanged);

   connect(ccFileMgr_.get(), &CCFileManager::CCAddressSubmitted, this, &CCTokenEntryDialog::onCCAddrSubmitted, Qt::QueuedConnection);

   connect(signingContainer_.get(), &SignContainer::HDLeafCreated, this, &CCTokenEntryDialog::onWalletCreated);
   connect(signingContainer_.get(), &SignContainer::Error, this, &CCTokenEntryDialog::onWalletFailed);

   switch (ccFileMgr_->GetOtpEncType()) {
   case bs::wallet::EncryptionType::Freja:
      ui_->groupBoxOtpPassword->hide();
      ui_->labelFreja->show();
      connect(&freja_, &FrejaSignOTP::succeeded, this, &CCTokenEntryDialog::onFrejaSucceeded);
      connect(&freja_, &FrejaSign::failed, this, &CCTokenEntryDialog::onFrejaFailed);
      connect(&freja_, &FrejaSign::statusUpdated, this, &CCTokenEntryDialog::onFrejaStatusUpdated);
      freja_.start(ccFileMgr_->GetOtpEncKey(), tr("Equity Token Submission"), ccFileMgr_->GetOtpId());
      break;

   case bs::wallet::EncryptionType::Password:
      ui_->labelFreja->hide();
      ui_->groupBoxOtpPassword->show();
      connect(ui_->lineEditOtpPassword, &QLineEdit::textEdited, this, &CCTokenEntryDialog::passwordChanged);
      break;

   case bs::wallet::EncryptionType::Unencrypted:
   default:
      ui_->labelFreja->hide();
      ui_->groupBoxOtpPassword->hide();
      passwordOk_ = true;
      break;
   }
   updateOkState();
}

CCTokenEntryDialog::~CCTokenEntryDialog()
{}

void CCTokenEntryDialog::tokenChanged()
{
   ui_->labelTokenHint->clear();
   ui_->pushButtonOk->setEnabled(false);
   const auto &strToken = ui_->lineEditToken->text().toStdString();
   if (strToken.empty()) {
      return;
   }
   try {
      const auto decoded = BtcUtils::base58toScrAddr(strToken).toBinStr();
      Blocksettle::Communication::CCSeedResponse response;
      if (!response.ParseFromString(decoded)) {
         throw std::invalid_argument("invalid internal token structure");
      }
      seed_ = response.bsseed();
      ccProduct_ = response.ccproduct();

      ccWallet_ = walletsMgr_->GetCCWallet(ccProduct_);
      if (!ccWallet_) {
         ui_->labelTokenHint->setText(tr("The Terminal will prompt you to create the relevant subwallet,"
            " if required").arg(QString::fromStdString(ccProduct_)));

         MessageBoxCCWalletQuestion qry(QString::fromStdString(ccProduct_), this);
         if (qry.exec() == QDialog::Accepted) {
            const auto priWallet = walletsMgr_->GetPrimaryWallet();
            if (!priWallet->getGroup(bs::hd::CoinType::BlockSettle_CC)) {
               priWallet->createGroup(bs::hd::CoinType::BlockSettle_CC);
            }
            bs::hd::Path path;
            path.append(bs::hd::purpose, true);
            path.append(bs::hd::BlockSettle_CC, true);
            path.append(ccProduct_, true);
            createWalletReqId_ = signingContainer_->CreateHDLeaf(priWallet, path);
         }
         else {
            reject();
         }
      }
      else {
         walletOk_ = true;
      }
   }
   catch (const std::exception &e) {
      ui_->labelTokenHint->setText(tr("Token you entered is not valid: %1").arg(QLatin1String(e.what())));
   }
   updateOkState();
}

void CCTokenEntryDialog::passwordChanged()
{
   passwordOk_ = !ui_->lineEditOtpPassword->text().isEmpty();
   otpPassword_ = ui_->lineEditOtpPassword->text().toStdString();
   updateOkState();
}

void CCTokenEntryDialog::updateOkState()
{
   ui_->pushButtonOk->setEnabled(walletOk_ && passwordOk_);
}

void CCTokenEntryDialog::onWalletCreated(unsigned int id, BinaryData pubKey, BinaryData chainCode, std::string walletId)
{
   if (!createWalletReqId_ || (createWalletReqId_ != id)) {
      return;
   }
   createWalletReqId_ = 0;
   const auto priWallet = walletsMgr_->GetPrimaryWallet();
   const auto group = priWallet->getGroup(bs::hd::BlockSettle_CC);
   const auto leafNode = std::make_shared<bs::hd::Node>(pubKey, chainCode, priWallet->networkType());
   ccWallet_ = group->createLeaf(bs::hd::Path::keyToElem(ccProduct_), leafNode);
   if (ccWallet_) {
      walletOk_ = true;
      ui_->labelTokenHint->setText(tr("Private Market subwallet for %1 created!").arg(QString::fromStdString(ccProduct_)));
   }
   else {
      ui_->labelTokenHint->setText(tr("Failed to create CC subwallet %1").arg(QString::fromStdString(ccProduct_)));
   }
   updateOkState();
}

void CCTokenEntryDialog::onWalletFailed(unsigned int id, std::string errMsg)
{
   if (!createWalletReqId_ || (createWalletReqId_ != id)) {
      return;
   }
   createWalletReqId_ = 0;
   ui_->labelTokenHint->setText(tr("Failed to create CC subwallet %1: %2")
      .arg(QString::fromStdString(ccProduct_)).arg(QString::fromStdString(errMsg)));
}

void CCTokenEntryDialog::accept()
{
   if (!ccWallet_) {
      reject();
      return;
   }
   const auto address = ccWallet_->GetNewExtAddress();
   signingContainer_->SyncAddresses({ { ccWallet_, address } });

   const auto &cbPasswordQuery = [this] { return otpPassword_; };
   if (!ccFileMgr_->SubmitAddressToPuB(address, seed_, cbPasswordQuery)) {
      reject();
      MessageBoxCritical(tr("CC Token submit failure")
         , tr("Failed to submit Private Market token to BlockSettle"), this).exec();
   }
   else {
      ui_->pushButtonOk->setEnabled(false);
   }
}

void CCTokenEntryDialog::reject()
{
   freja_.stop(true);
   QDialog::reject();
}

void CCTokenEntryDialog::onCCAddrSubmitted(const QString addr)
{
   QDialog::accept();
   MessageBoxInfo(tr("Successful Submission")
      , tr("The token has been submitted, please note that it might take a while before the"
         " transaction is broadcasted in the Terminal")).exec();
}

void CCTokenEntryDialog::onFrejaSucceeded(SecureBinaryData password)
{
   otpPassword_ = password;
   passwordOk_ = true;
   ui_->labelFreja->setText(tr("OTP signed with Freja"));
   updateOkState();
}

void CCTokenEntryDialog::onFrejaFailed(const QString &)
{
   passwordOk_ = false;
   QDialog::reject();
}

void CCTokenEntryDialog::onFrejaStatusUpdated(const QString &status)
{
   ui_->labelFreja->setText(tr("Freja status: %1").arg(status));
}
