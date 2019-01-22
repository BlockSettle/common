#include "CreateTransactionDialog.h"

#include <stdexcept>
#include <thread>

#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QIntValidator>
#include <QFile>
#include <QFileDialog>
#include <QCloseEvent>

#include "Address.h"
#include "ArmoryConnection.h"
#include "CoinControlDialog.h"
#include "BSMessageBox.h"
#include "HDWallet.h"
#include "OfflineSigner.h"
#include "SignContainer.h"
#include "TransactionData.h"
#include "TransactionOutputsModel.h"
#include "UiUtils.h"
#include "UsedInputsModel.h"
#include "WalletsManager.h"
#include "XbtAmountValidator.h"

// Mirror of cached Armory wait times - NodeRPC::aggregateFeeEstimates()
const std::map<unsigned int, QString> feeLevels = {
   { 2, QObject::tr("20 minutes") },
   { 3, QObject::tr("30 minutes") },
   { 4, QObject::tr("40 minutes") },
   { 5, QObject::tr("50 minutes") },
   { 6, QObject::tr("1 hour") },
   { 10, QObject::tr("1 hour 40 minutes") },
   { 20, QObject::tr("3 hours 20 minutes") }
};

CreateTransactionDialog::CreateTransactionDialog(const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<WalletsManager>& walletManager
   , const std::shared_ptr<SignContainer> &container, bool loadFeeSuggestions
   , const std::shared_ptr<spdlog::logger>& logger, QWidget* parent)
   : QDialog(parent)
   , armory_(armory)
   , walletsManager_(walletManager)
   , signingContainer_(container)
   , loadFeeSuggestions_(loadFeeSuggestions)
   , logger_(logger)
{
   qRegisterMetaType<std::map<unsigned int, float>>();

   offlineSigner_ = std::make_shared<OfflineSigner>(logger, walletsManager_->OfflineTxDir());
   connect(offlineSigner_.get(), &SignContainer::TXSigned, this, &CreateTransactionDialog::onTXSigned);
}

CreateTransactionDialog::~CreateTransactionDialog() noexcept
{
   if (feeUpdatingThread_.joinable()) {
      feeUpdatingThread_.join();
   }
}

void CreateTransactionDialog::init()
{
   transactionData_ = std::make_shared<TransactionData>([this]() {
      QMetaObject::invokeMethod(this, [this] {
         onTransactionUpdated();
      });
   });

   xbtValidator_ = new XbtAmountValidator(this);
   lineEditAmount()->setValidator(xbtValidator_);

   populateWalletsList();
   if (loadFeeSuggestions_) {
      populateFeeList();
   }

   pushButtonMax()->setEnabled(false);
   checkBoxRBF()->setChecked(true);
   pushButtonCreate()->setEnabled(false);

   connect(pushButtonMax(), &QPushButton::clicked, this, &CreateTransactionDialog::onMaxPressed);
   connect(comboBoxFeeSuggestions(), SIGNAL(activated(int)), this, SLOT(feeSelectionChanged(int)));

   if (signingContainer_) {
      connect(signingContainer_.get(), &SignContainer::TXSigned, this, &CreateTransactionDialog::onTXSigned);
      connect(signingContainer_.get(), &SignContainer::disconnected, this, &CreateTransactionDialog::updateCreateButtonText);
      connect(signingContainer_.get(), &SignContainer::authenticated, this, &CreateTransactionDialog::onSignerAuthenticated);
      signer_ = signingContainer_;
   }
   updateCreateButtonText();
   lineEditAddress()->setFocus();
}

void CreateTransactionDialog::updateCreateButtonText()
{
   if (!signer_) {
      pushButtonCreate()->setEnabled(false);
      return;
   }
   if (HaveSignedImportedTransaction()) {
      pushButtonCreate()->setText(tr("Broadcast"));
      if (signer_->isOffline() || (signer_->opMode() == SignContainer::OpMode::Offline)) {
         pushButtonCreate()->setEnabled(false);
      }
      return;
   }
   if (signer_->isOffline() || (signer_->opMode() == SignContainer::OpMode::Offline)) {
      signer_ = offlineSigner_;
      pushButtonCreate()->setText(tr("Export"));
   } else {
      selectedWalletChanged(-1);
   }
}

void CreateTransactionDialog::onSignerAuthenticated()
{
   selectedWalletChanged(-1);
}

void CreateTransactionDialog::clear()
{
   transactionData_->clear();
   importedSignedTX_.clear();
   lineEditAddress()->clear();
   lineEditAmount()->clear();
   textEditComment()->clear();
}

void CreateTransactionDialog::reject()
{
   if (broadcasting_) {
      BSMessageBox confirmExit(BSMessageBox::question, tr("Abort transaction broadcasting")
         , tr("You are about to abort transaction sending")
         , tr("Are you sure you wish to abort the signing and sending process?"), this);
      //confirmExit.setExclamationIcon();
      if (confirmExit.exec() != QDialog::Accepted) {
         return;
      }

      if (txReq_.isValid()) {
         if (signer_) {
            signer_->CancelSignTx(txReq_.txId());
         }
      }
   }

   QDialog::reject();
}

void CreateTransactionDialog::closeEvent(QCloseEvent *e)
{
   reject();

   e->ignore();
}

int CreateTransactionDialog::SelectWallet(const std::string& walletId)
{
   return UiUtils::selectWalletInCombobox(comboBoxWallets(), walletId);
}

void CreateTransactionDialog::populateWalletsList()
{
   auto selectedWalletIndex = UiUtils::fillWalletsComboBox(comboBoxWallets(), walletsManager_);
   selectedWalletChanged(selectedWalletIndex);
}

void CreateTransactionDialog::populateFeeList()
{
   comboBoxFeeSuggestions()->addItem(tr("Loading Fee suggestions"));
   comboBoxFeeSuggestions()->setCurrentIndex(0);
   comboBoxFeeSuggestions()->setEnabled(false);

   connect(this, &CreateTransactionDialog::feeLoadingCompleted
      , this, &CreateTransactionDialog::onFeeSuggestionsLoaded
      , Qt::QueuedConnection);

   loadFees();
}

void CreateTransactionDialog::loadFees()
{
   struct Result {
      std::map<unsigned int, float> values;
      std::set<unsigned int>  levels;
   };
   auto result = new Result;

   for (const auto &feeLevel : feeLevels) {
      result->levels.insert(feeLevel.first);
   }
   for (const auto &feeLevel : feeLevels) {
      const auto &cbFee = [this, result, level=feeLevel.first](float fee) {
         result->levels.erase(level);
         result->values[level] = fee;
         if (result->levels.empty()) {
            emit feeLoadingCompleted(result->values);
            delete result;
         }
      };
      walletsManager_->estimatedFeePerByte(feeLevel.first, cbFee, this);
   }
}

void CreateTransactionDialog::onFeeSuggestionsLoaded(const std::map<unsigned int, float> &feeValues)
{
   comboBoxFeeSuggestions()->clear();

   for (const auto &feeVal : feeValues) {
      QString desc;
      const auto itLevel = feeLevels.find(feeVal.first);
      if (itLevel == feeLevels.end()) {
         desc = tr("%1 minutes").arg(10 * feeVal.first);
      }
      else {
         desc = itLevel->second;
      }
      comboBoxFeeSuggestions()->addItem(tr("%1 blocks (%2): %3 s/b").arg(feeVal.first).arg(desc).arg(feeVal.second)
         , feeVal.second);
   }

   comboBoxFeeSuggestions()->setEnabled(true);

   feeSelectionChanged(0);
}

void CreateTransactionDialog::feeSelectionChanged(int currentIndex)
{
   transactionData_->SetFeePerByte(comboBoxFeeSuggestions()->itemData(currentIndex).toFloat());
}

void CreateTransactionDialog::selectedWalletChanged(int, bool resetInputs, const std::function<void()> &cbInputsReset)
{
   if (!comboBoxWallets()->count()) {
      pushButtonCreate()->setText(tr("No wallets"));
      return;
   }
   const auto currentWallet = walletsManager_->GetWalletById(UiUtils::getSelectedWalletId(comboBoxWallets()));
   const auto rootWallet = walletsManager_->GetHDRootForLeaf(currentWallet->GetWalletId());
   if (signingContainer_->isWalletOffline(currentWallet->GetWalletId())
      || !rootWallet || signingContainer_->isWalletOffline(rootWallet->getWalletId())) {
      pushButtonCreate()->setText(tr("Export"));
      signer_ = offlineSigner_;
   } else {
      pushButtonCreate()->setText(tr("Broadcast"));
      signer_ = signingContainer_;
   }
   if ((transactionData_->GetWallet() != currentWallet) || resetInputs) {
      transactionData_->SetWallet(currentWallet, armory_->topBlock(), resetInputs, cbInputsReset);
   }
}

void CreateTransactionDialog::onTransactionUpdated()
{
   const auto &summary = transactionData_->GetTransactionSummary();

   labelBalance()->setText(UiUtils::displayAmount(summary.availableBalance));
   labelAmount()->setText(UiUtils::displayAmount(summary.selectedBalance));
   labelTxInputs()->setText(summary.isAutoSelected ? tr("Auto (%1)").arg(QString::number(summary.usedTransactions))
      : QString::number(summary.usedTransactions));
   labelEstimatedFee()->setText(UiUtils::displayAmount(summary.totalFee));
   labelTotalAmount()->setText(UiUtils::displayAmount(UiUtils::amountToBtc(summary.balanceToSpend) + UiUtils::amountToBtc(summary.totalFee)));
   if (labelTxSize()) {
      labelTxSize()->setText(QString::number(summary.txVirtSize));
   }

   if (feePerByteLabel() != nullptr) {
      feePerByteLabel()->setText(tr("%1 s/b").arg(summary.feePerByte));
   }

   if (changeLabel() != nullptr) {
      if (summary.hasChange) {
         changeLabel()->setText(UiUtils::displayAmount(summary.selectedBalance - UiUtils::amountToBtc(summary.balanceToSpend) - UiUtils::amountToBtc(summary.totalFee)));
      } else {
         changeLabel()->setText(UiUtils::displayAmount(0.0));
      }
   }

   pushButtonCreate()->setEnabled(transactionData_->IsTransactionValid());
}

void CreateTransactionDialog::onMaxPressed()
{
   auto maxValue = transactionData_->CalculateMaxAmount(lineEditAddress()->text().toStdString());
   lineEditAmount()->setText(UiUtils::displayAmount(maxValue));
}

void CreateTransactionDialog::onTXSigned(unsigned int id, BinaryData signedTX, std::string error,
   bool cancelledByUser)
{
   if (!pendingTXSignId_ || (pendingTXSignId_ != id)) {
      return;
   }

   pendingTXSignId_ = 0;
   QString detailedText;

   if (error.empty()) {
      if (signer_->isOffline()) {   // Offline signing
         BSMessageBox(BSMessageBox::info, tr("Offline Transaction")
            , tr("Request exported to:\n%1").arg(QString::fromStdString(signedTX.toBinStr()))
            , this).exec();
         accept();
         return;
      }

      if (armory_->broadcastZC(signedTX)) {
         if (!textEditComment()->document()->isEmpty()) {
            const auto &comment = textEditComment()->document()->toPlainText().toStdString();
            transactionData_->GetWallet()->SetTransactionComment(signedTX, comment);
         }
         accept();
         return;
      }

      detailedText = tr("Failed to communicate to armory to broadcast transaction. Maybe Armory is offline");
   }
   else {
      detailedText = QString::fromStdString(error);
   }

   if (!cancelledByUser) {
      MessageBoxBroadcastError(detailedText, this).exec();
   }

   stopBroadcasting();
}

void CreateTransactionDialog::startBroadcasting()
{
   broadcasting_ = true;
   pushButtonCreate()->setEnabled(false);
   pushButtonCreate()->setText(tr("Waiting for TX signing..."));
}

void CreateTransactionDialog::stopBroadcasting()
{
   broadcasting_ = false;
   pushButtonCreate()->setEnabled(true);
   updateCreateButtonText();
}

bool CreateTransactionDialog::BroadcastImportedTx()
{
   if (importedSignedTX_.isNull()) {
      return false;
   }
   startBroadcasting();
   if (armory_->broadcastZC(importedSignedTX_)) {
      if (!textEditComment()->document()->isEmpty()) {
         const auto &comment = textEditComment()->document()->toPlainText().toStdString();
         transactionData_->GetWallet()->SetTransactionComment(importedSignedTX_, comment);
      }
      return true;
   }
   importedSignedTX_.clear();
   stopBroadcasting();
   BSMessageBox(BSMessageBox::critical, tr("Transaction broadcast"), tr("Failed to broadcast imported transaction"), this).exec();
   return false;
}

bool CreateTransactionDialog::CreateTransaction()
{
   const auto changeAddress = getChangeAddress();
   QString text;
   QString detailedText;

   if (signer_->isOffline()) {
      const auto txDir = QFileDialog::getExistingDirectory(this, tr("Select Offline TX destination directory")
         , walletsManager_->OfflineTxDir());
      if (txDir.isNull()) {
         return true;
      }
      offlineSigner_->SetTargetDir(txDir);
      walletsManager_->SetOfflineTxDir(txDir);
   }

   startBroadcasting();
   try {
      signer_->SyncAddresses(transactionData_->createAddresses());

      txReq_ = transactionData_->CreateTXRequest(checkBoxRBF()->checkState() == Qt::Checked
         , changeAddress, originalFee_);
      txReq_.comment = textEditComment()->document()->toPlainText().toStdString();

      // We shouldn't hit this case since the request checks the incremental
      // relay fee requirement for RBF. But, in case we
      if(txReq_.fee <= originalFee_) {
         BSMessageBox(BSMessageBox::info, tr("Error"), tr("Fee is too low"),
            tr("Due to RBF requirements, the current fee (%1) will be " \
               "increased 1 satoshi above the original transaction fee (%2)")
            .arg(UiUtils::displayAmount(txReq_.fee))
            .arg(UiUtils::displayAmount(originalFee_))).exec();
         txReq_.fee = originalFee_ + 1;
      }

      const float newFeePerByte = (float)txReq_.fee / (float)txReq_.estimateTxVirtSize();
      if(newFeePerByte < originalFeePerByte_) {
         BSMessageBox(BSMessageBox::info, tr("Error"), tr("Fee per byte is too low"),
            tr("Due to RBF requirements, the current fee per byte (%1) will " \
               "be increased to the original transaction fee rate (%2)")
            .arg(newFeePerByte)
            .arg(originalFeePerByte_)).exec();
         txReq_.fee = std::ceil(txReq_.fee * (originalFeePerByte_ / newFeePerByte));
      }

      pendingTXSignId_ = signer_->SignTXRequest(txReq_, false,
         SignContainer::TXSignMode::Full, {}, true);
      if (!pendingTXSignId_) {
         throw std::logic_error("Signer failed to send request");
      }
      return true;
   }
   catch (const std::runtime_error &e) {
      text = tr("Failed to broadcast transaction");
      detailedText = QString::fromStdString(e.what());
      qDebug() << QLatin1String("[CreateTransactionDialog::CreateTransaction] exception: ") << QString::fromStdString(e.what());
   }
   catch (const std::logic_error &e) {
      text = tr("Failed to create transaction");
      detailedText = QString::fromStdString(e.what());
      qDebug() << QLatin1String("[CreateTransactionDialog::CreateTransaction] exception: ") << QString::fromStdString(e.what());
   }
   catch (const std::exception &e) {
      text = tr("Failed to create transaction");
      detailedText = QString::fromStdString(e.what());
      qDebug() << QLatin1String("[CreateTransactionDialog::CreateTransaction] exception: ") << QString::fromStdString(e.what());
   }

   stopBroadcasting();
   BSMessageBox(BSMessageBox::critical, text, text, detailedText).exec();
   showError(text, detailedText);
   return false;
}

std::vector<bs::wallet::TXSignRequest> CreateTransactionDialog::ImportTransactions()
{
   const auto reqFile = QFileDialog::getOpenFileName(this, tr("Select Transaction file"), offlineDir_
      , tr("TX files (*.bin)"));
   if (reqFile.isEmpty()) {
      return {};
   }
   const auto title = tr("Transaction file");
   QFile f(reqFile);
   if (!f.exists()) {
      showError(title, tr("File %1 doesn't exist").arg(reqFile));
      return {};
   }

   if (!f.open(QIODevice::ReadOnly)) {
      showError(title, tr("Failed to open %1 for reading").arg(reqFile));
      return {};
   }

   const auto data = f.readAll().toStdString();
   if (data.empty()) {
      showError(title, tr("File %1 contains no data").arg(reqFile));
      return {};
   }

   const auto &transactions = ParseOfflineTXFile(data);
   if (transactions.size() != 1) {
      showError(title, tr("Invalid file %1 format").arg(reqFile));
      return {};
   }

   clear();
   return transactions;
}

void CreateTransactionDialog::showError(const QString &text, const QString &detailedText)
{
   BSMessageBox errorMessage(BSMessageBox::critical, text, detailedText, this);
   errorMessage.exec();
}
