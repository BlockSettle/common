#include "CreateTransactionDialogSimple.h"

#include "ui_CreateTransactionDialogSimple.h"

#include "Address.h"
#include "ArmoryConnection.h"
#include "CreateTransactionDialogAdvanced.h"
#include "OfflineSigner.h"
#include "SignContainer.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include "WalletsManager.h"
#include "XbtAmountValidator.h"

#include <QFileDialog>
#include <QDebug>

CreateTransactionDialogSimple::CreateTransactionDialogSimple(const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<WalletsManager>& walletManager
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<spdlog::logger>& logger, QWidget* parent)
 : CreateTransactionDialog(armory, walletManager, container, true, logger,
                           parent)
 , ui_(new Ui::CreateTransactionDialogSimple)
{
   ui_->setupUi(this);
   initUI();
}

CreateTransactionDialogSimple::~CreateTransactionDialogSimple() = default;

void CreateTransactionDialogSimple::initUI()
{
   CreateTransactionDialog::init();

   recipientId_ = transactionData_->RegisterNewRecipient();

   connect(ui_->comboBoxWallets, SIGNAL(currentIndexChanged(int)), this, SLOT(selectedWalletChanged(int)));

   connect(ui_->lineEditAddress, &QLineEdit::textEdited, this, &CreateTransactionDialogSimple::onAddressTextChanged);
   connect(ui_->lineEditAmount, &QLineEdit::textChanged, this, &CreateTransactionDialogSimple::onXBTAmountChanged);

   connect(ui_->pushButtonCreate, &QPushButton::clicked, this, &CreateTransactionDialogSimple::createTransaction);
   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
   connect(ui_->pushButtonImport, &QPushButton::clicked, this, &CreateTransactionDialogSimple::onImportPressed);

   connect(ui_->pushButtonShowAdvanced, &QPushButton::clicked, this, &CreateTransactionDialogSimple::showAdvanced);
}

QComboBox *CreateTransactionDialogSimple::comboBoxWallets() const
{
   return ui_->comboBoxWallets;
}

QComboBox *CreateTransactionDialogSimple::comboBoxFeeSuggestions() const
{
   return ui_->comboBoxFeeSuggestions;
}

QLineEdit *CreateTransactionDialogSimple::lineEditAddress() const
{
   return ui_->lineEditAddress;
}

QLineEdit *CreateTransactionDialogSimple::lineEditAmount() const
{
   return ui_->lineEditAmount;
}

QPushButton *CreateTransactionDialogSimple::pushButtonMax() const
{
   return ui_->pushButtonMax;
}

QTextEdit *CreateTransactionDialogSimple::textEditComment() const
{
   return ui_->textEditComment;
}

QCheckBox *CreateTransactionDialogSimple::checkBoxRBF() const
{
   return ui_->checkBoxRBF;
}

QLabel *CreateTransactionDialogSimple::labelBalance() const
{
   return ui_->labelBalance;
}

QLabel *CreateTransactionDialogSimple::labelAmount() const
{
   return ui_->labelInputAmount;
}

QLabel *CreateTransactionDialogSimple::labelTxInputs() const
{
   return ui_->labelTXInputs;
}

QLabel *CreateTransactionDialogSimple::labelEstimatedFee() const
{
   return ui_->labelFee;
}

QLabel *CreateTransactionDialogSimple::labelTotalAmount() const
{
   return ui_->labelTransationAmount;
}

QLabel *CreateTransactionDialogSimple::labelTxSize() const
{
   return ui_->labelTxSize;
}

QPushButton *CreateTransactionDialogSimple::pushButtonCreate() const
{
   return ui_->pushButtonCreate;
}

QPushButton *CreateTransactionDialogSimple::pushButtonCancel() const
{
   return ui_->pushButtonCancel;
}

QLabel* CreateTransactionDialogSimple::feePerByteLabel() const
{
   return ui_->labelFeePerByte;
}

QLabel* CreateTransactionDialogSimple::changeLabel() const
{
   return ui_->labelReturnAmount;
}

void CreateTransactionDialogSimple::onAddressTextChanged(const QString &addressString)
{
   try {
      bs::Address address{addressString.trimmed()};
      transactionData_->UpdateRecipientAddress(recipientId_, address);
      if (address.isValid()) {
         ui_->pushButtonMax->setEnabled(true);
         UiUtils::setWrongState(ui_->lineEditAddress, false);
         return;
      } else {
         UiUtils::setWrongState(ui_->lineEditAddress, true);
      }
   } catch(...) {
      UiUtils::setWrongState(ui_->lineEditAddress, true);
   }

   ui_->pushButtonMax->setEnabled(false);
   transactionData_->ResetRecipientAddress(recipientId_);
}

void CreateTransactionDialogSimple::onXBTAmountChanged(const QString &text)
{
   double value = UiUtils::parseAmountBtc(text);
   transactionData_->UpdateRecipientAmount(recipientId_, value);
}

void CreateTransactionDialogSimple::onMaxPressed()
{
   CreateTransactionDialog::onMaxPressed();
   transactionData_->UpdateRecipientAmount(recipientId_, UiUtils::parseAmountBtc(ui_->lineEditAmount->text()), true);
}

void CreateTransactionDialogSimple::showAdvanced()
{
   advancedDialogRequested_ = true;
   accept();
}

bs::Address CreateTransactionDialogSimple::getChangeAddress() const
{
   bs::Address result;
   if (transactionData_->GetTransactionSummary().hasChange) {
      result = transactionData_->GetWallet()->GetNewChangeAddress();
   }
   return result;
}

void CreateTransactionDialogSimple::createTransaction()
{
   if (!importedSignedTX_.isNull()) {
      if (!importedSignedTX_.isNull()) {
         if (BroadcastImportedTx()) {
            accept();
         }
         else {
            initUI();
         }
         return;
      }
   }

   if (!CreateTransaction()) {
      reject();
   }
}

bool CreateTransactionDialogSimple::userRequestedAdvancedDialog() const
{
   return advancedDialogRequested_;
}

std::shared_ptr<CreateTransactionDialogAdvanced> CreateTransactionDialogSimple::CreateAdvancedDialog()
{
   auto advancedDialog = std::make_shared<CreateTransactionDialogAdvanced>(armory_, walletsManager_
      , signingContainer_, true, logger_, parentWidget());

   if (!offlineTransactions_.empty()) {
      advancedDialog->SetImportedTransactions(offlineTransactions_);
   } else {
      // select wallet
      advancedDialog->SelectWallet(UiUtils::getSelectedWalletId(ui_->comboBoxWallets));

      // set inputs and amounts
      auto address = ui_->lineEditAddress->text().trimmed();
      if (!address.isEmpty()) {
         advancedDialog->preSetAddress(address);
      }

      auto valueText = ui_->lineEditAmount->text();
      if (!valueText.isEmpty()) {
         double value = UiUtils::parseAmountBtc(valueText);
         advancedDialog->preSetValue(value);
      }
   }

   advancedDialog->setOfflineDir(offlineDir_);

   return advancedDialog;
}

void CreateTransactionDialogSimple::onImportPressed()
{
   offlineTransactions_ = ImportTransactions();
   if (offlineTransactions_.empty()) {
      return;
   }

   showAdvanced();
}
