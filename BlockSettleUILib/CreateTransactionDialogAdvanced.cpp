#include "CreateTransactionDialogAdvanced.h"
#include "ui_CreateTransactionDialogAdvanced.h"

#include "Address.h"
#include "ArmoryConnection.h"
#include "BSMessageBox.h"
#include "CoinControlDialog.h"
#include "OfflineSigner.h"
#include "SelectAddressDialog.h"
#include "SelectedTransactionInputs.h"
#include "SignContainer.h"
#include "TransactionData.h"
#include "TransactionOutputsModel.h"
#include "UiUtils.h"
#include "UsedInputsModel.h"
#include "Wallets/SyncWalletsManager.h"
#include "XbtAmountValidator.h"

#include <QEvent>
#include <QKeyEvent>
#include <QFile>
#include <QFileDialog>
#include <QPushButton>

#include <stdexcept>

static const size_t kP2WPKHOutputSize = 35;
static const float kDustFeePerByte = 3.0;


CreateTransactionDialogAdvanced::CreateTransactionDialogAdvanced(const std::shared_ptr<ArmoryConnection> &armory
      , const std::shared_ptr<bs::sync::WalletsManager>& walletManager
      , const std::shared_ptr<SignContainer> &container
      , bool loadFeeSuggestions
      , const std::shared_ptr<spdlog::logger>& logger, const std::shared_ptr<TransactionData> &txData
      , QWidget* parent)
   : CreateTransactionDialog(armory, walletManager, container, loadFeeSuggestions, logger, parent)
 , ui_(new Ui::CreateTransactionDialogAdvanced)
{
   transactionData_ = txData;
   selectedChangeAddress_ = bs::Address{};

   ui_->setupUi(this);
   initUI();
}

CreateTransactionDialogAdvanced::~CreateTransactionDialogAdvanced() = default;

std::shared_ptr<CreateTransactionDialogAdvanced> CreateTransactionDialogAdvanced::CreateForRBF(
        const std::shared_ptr<ArmoryConnection> &armory
      , const std::shared_ptr<bs::sync::WalletsManager>& walletManager
      , const std::shared_ptr<SignContainer>& container
      , const std::shared_ptr<spdlog::logger>& logger
      , const Tx &tx
      , const std::shared_ptr<bs::sync::Wallet>& wallet
      , QWidget* parent)
{
   auto dlg = std::make_shared<CreateTransactionDialogAdvanced>(armory
      , walletManager, container, false, logger, nullptr, parent);

   dlg->setWindowTitle(tr("Replace-By-Fee"));

   dlg->ui_->checkBoxRBF->setChecked(true);
   dlg->ui_->checkBoxRBF->setEnabled(false);
   dlg->ui_->pushButtonImport->setEnabled(false);

   dlg->setRBFinputs(tx, wallet);
   return dlg;
}

std::shared_ptr<CreateTransactionDialogAdvanced> CreateTransactionDialogAdvanced::CreateForCPFP(
        const std::shared_ptr<ArmoryConnection> &armory
      , const std::shared_ptr<bs::sync::WalletsManager>& walletManager
      , const std::shared_ptr<SignContainer>& container
      , const std::shared_ptr<bs::sync::Wallet>& wallet
      , const std::shared_ptr<spdlog::logger>& logger
      , const Tx &tx
      , QWidget* parent)
{
   auto dlg = std::make_shared<CreateTransactionDialogAdvanced>(armory
      , walletManager, container, false, logger, nullptr, parent);

   dlg->setWindowTitle(tr("Child-Pays-For-Parent"));
   dlg->ui_->pushButtonImport->setEnabled(false);

   dlg->setCPFPinputs(tx, wallet);
   return dlg;
}

void CreateTransactionDialogAdvanced::setCPFPinputs(const Tx &tx, const std::shared_ptr<bs::sync::Wallet> &wallet)
{
   std::set<BinaryData> txHashSet;
   std::map<BinaryData, std::set<uint32_t>> txOutIndices;
   for (size_t i = 0; i < tx.getNumTxIn(); i++) {
      const auto txin = tx.getTxInCopy(i);
      const auto outpoint = txin.getOutPoint();
      txHashSet.insert(outpoint.getTxHash());
      txOutIndices[outpoint.getTxHash()].insert(outpoint.getTxOutIndex());
   }
   allowAutoSelInputs_ = false;

   const auto &cbTXs = [this, tx, wallet, txOutIndices](std::vector<Tx> txs) {
      auto selInputs = transactionData_->GetSelectedInputs();
      selInputs->SetUseAutoSel(false);
      int64_t origFee = 0;
      unsigned int cntOutputs = 0;
      for (const auto &prevTx : txs) {
         const auto &txHash = prevTx.getThisHash();
         const auto &itTxOut = txOutIndices.find(txHash);
         if (itTxOut == txOutIndices.end()) {
            continue;
         }
         for (const auto &txOutIdx : itTxOut->second) {
            if (prevTx.isInitialized()) {
               TxOut prevOut = prevTx.getTxOutCopy(txOutIdx);
               origFee += prevOut.getValue();
            }
         }
      }

      for (size_t i = 0; i < tx.getNumTxOut(); i++) {
         auto out = tx.getTxOutCopy(i);
         const auto addr = bs::Address::fromTxOut(out);
         if (wallet->containsAddress(addr)) {
            if (selInputs->SetUTXOSelection(tx.getThisHash(),
               out.getIndex())) {
               cntOutputs++;
            }
         }
         origFee -= out.getValue();
      }

      if (!cntOutputs) {
         if (logger_ != nullptr) {
            logger_->error("[{}] No input(s) found for TX {}.", __func__
               , tx.getThisHash().toHexStr(true));
         }
         return;
      }
      if (origFee < 0) {
         if (logger_ != nullptr) {
            logger_->error("[{}] Negative TX balance ({}) for TX {}."
               , __func__, origFee
               , tx.getThisHash().toHexStr(true));
         }
         return;
      }

      const auto &cbFee = [this, tx, origFee, selInputs](float fee) {
         const auto txSize = tx.getTxWeight();
         const float feePerByte = (float)origFee / txSize;
         originalFee_ = origFee;
         originalFeePerByte_ = feePerByte;

         // CPFP has no enforced rules for fees. We use the following
         // algorithm for determining the fee/byte. If the current 2-block
         // fee is less than the fee used by the parent, stick to the current
         // 2-block fee. If not, add the difference to the 2-block fee and
         // use the result for the child fee. Simple but it should work. A
         // little tinkering may be worthwhile later.
         const float feeDiff = fee - originalFee_;
         float newFPB = fee;
         if (std::signbit(feeDiff) == false) { // Is the diff positive?
            newFPB += feeDiff;
         }

         // SetMinimumFee() may need to be re-thought. RBF is the only
         // scenario where we really need to enforce a minimum fee in concert
         // with the minimum fee/byte. For now, the minimum fee will be set
         // to 0, with the fee/byte enforced elsewhere. Attempting to enforce
         // a value that won't always be accurate is a bad idea.
         SetMinimumFee(0, newFPB);
         onTransactionUpdated();
         populateFeeList();
         SetInputs(selInputs->GetSelectedTransactions());
      };
      walletsManager_->estimatedFeePerByte(2, cbFee, this);
   };

   SetFixedWallet(wallet->walletId(), [this, txHashSet, cbTXs] {
      armory_->getTXsByHash(txHashSet, cbTXs);
   });
}

void CreateTransactionDialogAdvanced::setRBFinputs(const Tx &tx, const std::shared_ptr<bs::sync::Wallet> &wallet)
{
   isRBF_ = true;

   std::set<BinaryData> txHashSet;
   std::map<BinaryData, std::set<uint32_t>> txOutIndices;
   for (size_t i = 0; i < tx.getNumTxIn(); i++) {
      const auto txin = tx.getTxInCopy(i);
      const auto outpoint = txin.getOutPoint();
      txHashSet.insert(outpoint.getTxHash());
      txOutIndices[outpoint.getTxHash()].insert(outpoint.getTxOutIndex());
   }

   const auto &cbTXs = [this, tx, wallet, txOutIndices](std::vector<Tx> txs) {
      int64_t totalVal = 0;
      for (const auto &prevTx : txs) {
         const auto &txHash = prevTx.getThisHash();
         const auto &itTxOut = txOutIndices.find(txHash);
         if (itTxOut == txOutIndices.end()) {
            continue;
         }
         for (const auto &txOutIdx : itTxOut->second) {
            if (prevTx.isInitialized()) {
               TxOut prevOut = prevTx.getTxOutCopy(txOutIdx);
               totalVal += prevOut.getValue();
            }
            if (!transactionData_->GetSelectedInputs()->SetUTXOSelection(txHash, txOutIdx)) {
               if (logger_ != nullptr) {
                  logger_->error("[{}] No input(s) found for TX {}."
                                 , __func__, txHash.toHexStr(true));
               }
               continue;
            }
         }
      }

      QString  changeAddress;
      double   changeAmount = 0;
      std::vector<std::pair<bs::Address, double>> ownOutputs;

      // set outputs
      for (size_t i = 0; i < tx.getNumTxOut(); i++) {
         TxOut out = tx.getTxOutCopy(i);
         const auto addr = bs::Address::fromTxOut(out);
         const auto amount = UiUtils::amountToBtc(out.getValue());

         if (wallet->containsAddress(addr)) {
            ownOutputs.push_back({addr, amount});
         }
         totalVal -= out.getValue();
      }

      // Assume change address is the last internal address in the
      // list of outputs belonging to the wallet
      for (const auto &output : ownOutputs) {
         const auto path = bs::hd::Path::fromString(wallet->getAddressIndex(output.first));
         if (path.length() == 2) {
            if (path.get(-2) == 1) {   // internal HD address
               changeAddress = QString::fromStdString(output.first.display());
               changeAmount = output.second;
            }
         }
         else  {   // not an HD wallet/address
            changeAddress = QString::fromStdString(output.first.display());
            changeAmount = output.second;
         }
      }

      // Error check.
      if (totalVal < 0) {
         if (logger_ != nullptr) {
            logger_->error("[{}] Negative TX balance ({}) for TX {}."
                           , __func__, totalVal
                           , tx.getThisHash().toHexStr(true));
         }
         return;
      }

      // If we did find a change address, set it in place in this TX.
      else if (!changeAddress.isEmpty()) {
         // If the original TX didn't use up the original inputs, force the
         // original change address to be used. It may be desirable to change
         // this eventually.
         SetFixedChangeAddress(changeAddress);
      }

      // RBF minimum amounts are a little tricky. The rules/policies are:
      //
      // - RULE: Calculate based not on the absolute TX size, but on the virtual
      //   size, which is ceil(TX weight / 4) (e.g., 32.2 -> 33). For reference,
      //   TX weight = Total_TX_Size + (3 * Base_TX_Size).
      //   (Base_TX_Size = TX size w/o witness data)
      // - RULE: The new fee/KB must meet or exceed the old one. (If replacing
      //   multiple TXs, Core seems to calculate based on the sum of fees and
      //   TX sizes for the old TXs.)
      // - RULE: The new fee must be at least 1 satoshi higher than the sum of
      //   the fees of the replaced TXs.
      // - POLICY: The new fee must be bumped by, at a minimum, the incremental
      //   relay fee (IRL) * the new TX's virtual size. The fee can be adjusted
      //   in Core by the incrementalrelayfee config option. By default, the fee
      //   is 1000 sat/KB (1 sat/B), which is what we will assume is being used.
      //   (This may need to be a terminal config option later.) So, if the virt
      //   size is 146, and the original fee is 1 sat/b (146 satoshis), the next
      //   fee must be at least 2 sat/b (292 satoshis), then 3 sat/b, etc. This
      //   assumes we don't change the TX in any way. Bumping to a virt size of
      //   300 would require the 1st RBF to be 446 satoshis, then 746, 1046, etc.
      //
      // It's impossible to calculate the minimum required fee, as the user can
      // do many different things. We'll just start by setting the minimum fee
      // to the amount required by the RBF/IRL policy, and keep the minimum
      // fee/byte where it is.
      originalFee_ = totalVal;
      const float feePerByte = (float)totalVal / (float)tx.getTxWeight();
      originalFeePerByte_ = feePerByte;
      const uint64_t newMinFee = originalFee_ + tx.getTxWeight();
      SetMinimumFee(newMinFee, originalFeePerByte_ + 1.0);
      advisedFeePerByte_ = originalFeePerByte_ + 1.0;
      populateFeeList();
      SetInputs(transactionData_->GetSelectedInputs()->GetSelectedTransactions());
   };

   const auto &cbRBFInputs = [this, wallet, txHashSet, cbTXs](ReturnMessage<std::vector<UTXO>> utxos) {
      try {
         auto inUTXOs = utxos.get();
         QMetaObject::invokeMethod(this, [this, wallet, txHashSet, inUTXOs, cbTXs] {
            setFixedWalletAndInputs(wallet, inUTXOs);

            armory_->getTXsByHash(txHashSet, cbTXs);
         });
      }
      catch (const std::exception &e) {
         if (logger_ != nullptr) {
            logger_->error("[CreateTransactionDialogAdvanced::setRBFinputs] " \
               "Return data error - {}", e.what());
         }
      }
   };
   wallet->getRBFTxOutList(cbRBFInputs);
}

void CreateTransactionDialogAdvanced::initUI()
{
   usedInputsModel_ = new UsedInputsModel(this);
   outputsModel_ = new TransactionOutputsModel(this);

   CreateTransactionDialog::init();

   ui_->treeViewInputs->setModel(usedInputsModel_);
   ui_->treeViewInputs->setColumnWidth(1, 50);
   ui_->treeViewInputs->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
   ui_->treeViewInputs->header()->setSectionResizeMode(1, QHeaderView::Fixed);
   ui_->treeViewInputs->header()->setSectionResizeMode(0, QHeaderView::Stretch);

   ui_->treeViewOutputs->setModel(outputsModel_);
   ui_->treeViewOutputs->setColumnWidth(2, 30);
   ui_->treeViewOutputs->header()->setSectionResizeMode(2, QHeaderView::Fixed);
   ui_->treeViewOutputs->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
   ui_->treeViewOutputs->header()->setSectionResizeMode(0, QHeaderView::Stretch);

   // QModelIndex isn't used. We should use it or lose it.
   connect(ui_->treeViewOutputs, &QTreeView::clicked, this, &CreateTransactionDialogAdvanced::onOutputsClicked);
   connect(outputsModel_, &TransactionOutputsModel::rowsRemoved, [this](const QModelIndex &parent, int first, int last) {
      onOutputRemoved();
   });

   currentAddress_.clear();
   currentValue_ = 0;
   if (qFuzzyIsNull(minFeePerByte_) || qFuzzyIsNull(minTotalFee_)) {
      SetMinimumFee(0, 1.0);
   }

   ui_->pushButtonAddOutput->setEnabled(false);
   ui_->line->hide();
   ui_->pushButtonSelectInputs->setEnabled(ui_->comboBoxWallets->count() > 0);

   connect(ui_->lineEditAddress, &QLineEdit::textChanged, this, &CreateTransactionDialogAdvanced::onAddressTextChanged);
   connect(ui_->lineEditAmount, &QLineEdit::textChanged, this, &CreateTransactionDialogAdvanced::onXBTAmountChanged);
   ui_->lineEditAddress->installEventFilter(this);
   ui_->lineEditAmount->installEventFilter(this);

   connect(ui_->pushButtonSelectInputs, &QPushButton::clicked, this, &CreateTransactionDialogAdvanced::onSelectInputs);
   connect(ui_->pushButtonAddOutput, &QPushButton::clicked, this, &CreateTransactionDialogAdvanced::onAddOutput);
   connect(ui_->pushButtonCreate, &QPushButton::clicked, this, &CreateTransactionDialogAdvanced::onCreatePressed);
   connect(ui_->pushButtonImport, &QPushButton::clicked, this, &CreateTransactionDialogAdvanced::onImportPressed);
   connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &CreateTransactionDialogAdvanced::reject);

   ui_->radioButtonNewAddrNative->setChecked(true);

   connect(ui_->radioButtonNewAddrNative, &QRadioButton::clicked, this, &CreateTransactionDialogAdvanced::onNewAddressSelectedForChange);
   connect(ui_->radioButtonNewAddrNested, &QRadioButton::clicked, this, &CreateTransactionDialogAdvanced::onNewAddressSelectedForChange);
   connect(ui_->radioButtonExistingAddress, &QRadioButton::clicked, this, &CreateTransactionDialogAdvanced::onExistingAddressSelectedForChange);

   ui_->treeViewOutputs->setContextMenuPolicy(Qt::CustomContextMenu);

   removeOutputAction_ = new QAction(tr("Remove Output"), this);
   connect(removeOutputAction_, &QAction::triggered, this, &CreateTransactionDialogAdvanced::onRemoveOutput);
   contextMenu_.addAction(removeOutputAction_);

   connect(ui_->treeViewOutputs, &QTreeView::customContextMenuRequested, this, &CreateTransactionDialogAdvanced::showContextMenu);

   connect(ui_->doubleSpinBoxFeesManualPerByte, QOverload<double>::of(&QDoubleSpinBox::valueChanged)
      , this, &CreateTransactionDialogAdvanced::setTxFees);
   connect(ui_->spinBoxFeesManualTotal, QOverload<int>::of(&QSpinBox::valueChanged)
      , this, &CreateTransactionDialogAdvanced::setTxFees);

   updateManualFeeControls();
}

void CreateTransactionDialogAdvanced::clear()
{
   CreateTransactionDialog::clear();
   outputsModel_->clear();
   usedInputsModel_->clear();
}

bool CreateTransactionDialogAdvanced::eventFilter(QObject *watched, QEvent *evt)
{
   if (evt->type() == QEvent::KeyPress) {
      auto keyID = static_cast<QKeyEvent *>(evt)->key();
      if ((keyID == Qt::Key_Return) || (keyID == Qt::Key_Enter)) {
         if ((watched == ui_->lineEditAddress) && currentAddress_.isValid() && qFuzzyIsNull(currentValue_)) {
            ui_->lineEditAmount->setFocus();
         }
         if ((watched == ui_->lineEditAmount) && !qFuzzyIsNull(currentValue_) && !currentAddress_.isValid()) {
            ui_->lineEditAddress->setFocus();
         }
         else if (ui_->pushButtonAddOutput->isEnabled()) {
            ui_->pushButtonAddOutput->animateClick();
            ui_->lineEditAddress->setFocus();
         }
         else if (ui_->lineEditAddress->text().isEmpty() && ui_->lineEditAmount->text().isEmpty()) {
            return QDialog::eventFilter(watched, evt);
         }
         return true;
      }
   }
   return QDialog::eventFilter(watched, evt);
}

QComboBox *CreateTransactionDialogAdvanced::comboBoxWallets() const
{
   return ui_->comboBoxWallets;
}

QComboBox *CreateTransactionDialogAdvanced::comboBoxFeeSuggestions() const
{
   return ui_->comboBoxFeeSuggestions;
}

QLineEdit *CreateTransactionDialogAdvanced::lineEditAddress() const
{
   return ui_->lineEditAddress;
}

QLineEdit *CreateTransactionDialogAdvanced::lineEditAmount() const
{
   return ui_->lineEditAmount;
}

QPushButton *CreateTransactionDialogAdvanced::pushButtonMax() const
{
   return ui_->pushButtonMax;
}

QTextEdit *CreateTransactionDialogAdvanced::textEditComment() const
{
   return ui_->textEditComment;
}

QCheckBox *CreateTransactionDialogAdvanced::checkBoxRBF() const
{
   return ui_->checkBoxRBF;
}

QLabel *CreateTransactionDialogAdvanced::labelBalance() const
{
   return ui_->labelBalance;
}

QLabel *CreateTransactionDialogAdvanced::labelAmount() const
{
   return ui_->labelInputAmount;
}

QLabel *CreateTransactionDialogAdvanced::labelTxInputs() const
{
   return ui_->labelTXInputs;
}

QLabel *CreateTransactionDialogAdvanced::labelEstimatedFee() const
{
   return ui_->labelFee;
}

QLabel *CreateTransactionDialogAdvanced::labelTotalAmount() const
{
   return ui_->labelTransationAmount;
}

QLabel *CreateTransactionDialogAdvanced::labelTxSize() const
{
   return ui_->labelTxSize;
}

QPushButton *CreateTransactionDialogAdvanced::pushButtonCreate() const
{
   return ui_->pushButtonCreate;
}

QPushButton *CreateTransactionDialogAdvanced::pushButtonCancel() const
{
   return ui_->pushButtonCancel;
}

QLabel* CreateTransactionDialogAdvanced::feePerByteLabel() const
{
   return ui_->labelFeePerByte;
}

QLabel* CreateTransactionDialogAdvanced::changeLabel() const
{
   return ui_->labelReturnAmount;
}

void CreateTransactionDialogAdvanced::onOutputsClicked(const QModelIndex &index)
{
   if (!index.isValid()) {
      return;
   }

   if (!removeOutputEnabled_) {
      return;
   }

   if (outputsModel_->isRemoveColumn(index.column())) {
      auto outputId = outputsModel_->GetOutputId(index.row());
      RemoveOutputByRow(outputsModel_->GetRowById(outputId));
   }
}

void CreateTransactionDialogAdvanced::showContextMenu(const QPoint &point)
{
   if (!removeOutputEnabled_) {
      return;
   }

   const auto index = ui_->treeViewOutputs->indexAt(point);
   if (index.row() != -1) {
      removeOutputAction_->setEnabled(true);
      removeOutputAction_->setData(index.row());

      contextMenu_.exec(ui_->treeViewOutputs->mapToGlobal(point));
   }
}

void CreateTransactionDialogAdvanced::onRemoveOutput()
{
   int row = removeOutputAction_->data().toInt();
   RemoveOutputByRow(row);
   onOutputRemoved();
}

void CreateTransactionDialogAdvanced::onOutputRemoved()
{
   for (const auto &recipId : transactionData_->allRecipientIds()) {
      UpdateRecipientAmount(recipId, transactionData_->GetRecipientAmount(recipId), false);
   }
   transactionData_->setTotalFee(0, false);
   setTxFees();
   enableFeeChanging();
}

void CreateTransactionDialogAdvanced::RemoveOutputByRow(int row)
{
   auto outputId = outputsModel_->GetOutputId(row);

   transactionData_->RemoveRecipient(outputId);
   outputsModel_->RemoveRecipient(row);

   ui_->comboBoxFeeSuggestions->setEnabled(true);
}

void CreateTransactionDialogAdvanced::onTransactionUpdated()
{
   fixFeePerByte();
   CreateTransactionDialog::onTransactionUpdated();

   // If RBF is active, prevent the inputs from being changed. It may be
   // desirable to change this one day. RBF TXs can change inputs but only if
   // all other inputs are RBF-enabled. Properly refactored, the user could
   // select only RBF-enabled inputs that are waiting for a conf.
   if (!isRBF_ && transactionData_->GetSelectedInputs()->UseAutoSel()) {
      usedInputsModel_->updateInputs(transactionData_->inputs());
   }

   const auto &summary = transactionData_->GetTransactionSummary();

   if (!changeAddressFixed_) {
      bool changeSelectionEnabled = summary.hasChange || (summary.txVirtSize == 0);
      ui_->changeAddrGroupBox->setEnabled(changeSelectionEnabled);
      showExistingChangeAddress(changeSelectionEnabled);
   }

   if (transactionData_->totalFee() && summary.txVirtSize
      && (transactionData_->totalFee() < summary.txVirtSize)) {
      transactionData_->setTotalFee(summary.txVirtSize);
      ui_->spinBoxFeesManualTotal->setValue(summary.txVirtSize);
   }

   QMetaObject::invokeMethod(this, &CreateTransactionDialogAdvanced::validateCreateButton
      , Qt::QueuedConnection);
}

void CreateTransactionDialogAdvanced::preSetAddress(const QString& address)
{
   ui_->lineEditAddress->setText(address);
   onAddressTextChanged(address);
}

void CreateTransactionDialogAdvanced::preSetValue(const double value)
{
   ui_->lineEditAmount->setText(UiUtils::displayAmount(value));
}

void CreateTransactionDialogAdvanced::onAddressTextChanged(const QString& addressString)
{
   try {
      currentAddress_ = bs::Address(addressString.trimmed().toStdString());
      if (currentAddress_.format() == bs::Address::Format::Hex) {
         currentAddress_.clear();   // P2WSH unprefixed address can resemble TX hash,
      }                             // so we disable hex format completely
   } catch (...) {
      currentAddress_.clear();
   }
   UiUtils::setWrongState(ui_->lineEditAddress, !currentAddress_.isValid());

   validateAddOutputButton();
}

void CreateTransactionDialogAdvanced::onXBTAmountChanged(const QString &text)
{
   currentValue_ = UiUtils::parseAmountBtc(text);
   validateAddOutputButton();
}

void CreateTransactionDialogAdvanced::onSelectInputs()
{
   const double prevBalance = transactionData_->GetTransactionSummary().availableBalance;
   const double spendBalance = transactionData_->GetTotalRecipientsAmount();
   const double totalFee = transactionData_->GetTransactionSummary().totalFee / BTCNumericTypes::BalanceDivider;
   CoinControlDialog dlg(transactionData_->GetSelectedInputs(), allowAutoSelInputs_, this);
   if (dlg.exec() == QDialog::Accepted) {
      SetInputs(dlg.selectedInputs());
   }

   const double curBalance = transactionData_->GetTransactionSummary().availableBalance;
   if (curBalance < (spendBalance + totalFee)) {
      BSMessageBox lowInputs(BSMessageBox::question, tr("Not enough inputs balance")
         , tr("Currently your inputs don't allow to spend the balance added to output[s]. Delete [some of] them?"));
      if (lowInputs.exec() == QDialog::Accepted) {
         while (outputsModel_->rowCount({})) {
            RemoveOutputByRow(0);
            if (curBalance >= (transactionData_->GetTotalRecipientsAmount()
               + transactionData_->GetTransactionSummary().totalFee / BTCNumericTypes::BalanceDivider)) {
               break;
            }
         }
         onOutputRemoved();
      }
   }
   else if (curBalance > prevBalance) {
      enableFeeChanging();
   }
}

void CreateTransactionDialogAdvanced::onAddOutput()
{
   const bs::Address address(ui_->lineEditAddress->text().trimmed().toStdString());
   const double maxValue = transactionData_->CalculateMaxAmount(address);
   bool maxAmount = std::abs(maxValue
      - transactionData_->GetTotalRecipientsAmount() - currentValue_) <= 0.00000001;

   AddRecipient(address, currentValue_, maxAmount);

   maxAmount |= FixRecipientsAmount();

   // clear edits
   ui_->lineEditAddress->clear();
   ui_->lineEditAmount->clear();
   ui_->pushButtonAddOutput->setEnabled(false);
   if (maxAmount) {
      enableFeeChanging(false);
   }
}

// Nothing is being done with isMax right now. We should use it or lose it.
unsigned int CreateTransactionDialogAdvanced::AddRecipient(const bs::Address &address, double amount, bool isMax)
{
   const auto recipientId = transactionData_->RegisterNewRecipient();
   transactionData_->UpdateRecipientAddress(recipientId, address);
   transactionData_->UpdateRecipientAmount(recipientId, amount, isMax);

   // add to the model
   outputsModel_->AddRecipient(recipientId, QString::fromStdString(address.display()), amount);

   return recipientId;
}

void CreateTransactionDialogAdvanced::AddRecipients(const std::vector<std::tuple<bs::Address, double, bool>> &recipients)
{
   std::vector<std::tuple<unsigned int, QString, double>> modelRecips;
   for (const auto &recip : recipients) {
      const auto recipientId = transactionData_->RegisterNewRecipient();
      transactionData_->UpdateRecipientAddress(recipientId, std::get<0>(recip));
      transactionData_->UpdateRecipientAmount(recipientId, std::get<1>(recip), std::get<2>(recip));
      modelRecips.push_back({recipientId, QString::fromStdString(std::get<0>(recip).display()), std::get<1>(recip)});
   }
   QMetaObject::invokeMethod(outputsModel_, [this, modelRecips] { outputsModel_->AddRecipients(modelRecips); });
}


// Attempts to remove the change if it's small enough and adds its amount to fees
bool CreateTransactionDialogAdvanced::FixRecipientsAmount()
{
   if (!transactionData_->totalFee()) {
      return false;
   }
   const double totalFee = UiUtils::amountToBtc(transactionData_->totalFee());
   double diffMax = transactionData_->GetTransactionSummary().availableBalance
      - transactionData_->GetTotalRecipientsAmount() - totalFee;
   const double newTotalFee = diffMax + totalFee;

   size_t maxOutputSize = 0;
   for (const auto &recipId : transactionData_->allRecipientIds()) {
      const auto recipAddr = transactionData_->GetRecipientAddress(recipId);
      const auto recip = recipAddr.getRecipient(transactionData_->GetRecipientAmount(recipId));
      if (!recip) {
         continue;
      }
      maxOutputSize = std::max(maxOutputSize, recip->getSize());
   }
   if (!maxOutputSize) {
      maxOutputSize = totalFee / kDustFeePerByte / 2; // fallback if failed to get any recipients size
   }

   if (diffMax < 0) {
      diffMax = 0;
   }
   // The code below tries to eliminate the change address if the change amount is too little (less than half of current fee).
   if ((diffMax >= 0.00000001) && (diffMax <= (maxOutputSize * kDustFeePerByte / BTCNumericTypes::BalanceDivider))) {
      BSMessageBox question(BSMessageBox::question, tr("Change fee")
         , tr("Your projected change amount %1 is too small as compared to the projected fee."
            " Attempting to keep the change will prevent the transaction from being propagated through"
            " the Bitcoin network.").arg(UiUtils::displayAmount(diffMax))
         , tr("Would you like to remove the change output and put its amount towards the fees?")
         , this);
      if (question.exec() == QDialog::Accepted) {
         transactionData_->setTotalFee(newTotalFee * BTCNumericTypes::BalanceDivider, false);
         for (const auto &recipId : transactionData_->allRecipientIds()) {
            UpdateRecipientAmount(recipId, transactionData_->GetRecipientAmount(recipId), true);
         }
         return true;
      }
   }
   else if (diffMax < 0.00000001) {   // if diff is less than 1 satoshi (which can be caused by maxAmount calc tolerance)
      for (const auto &recipId : transactionData_->allRecipientIds()) {
         UpdateRecipientAmount(recipId, transactionData_->GetRecipientAmount(recipId), true);
      }
      return true;
   }
   return false;
}

void CreateTransactionDialogAdvanced::UpdateRecipientAmount(unsigned int recipId, double amount, bool isMax)
{
   transactionData_->UpdateRecipientAmount(recipId, amount, isMax);
   outputsModel_->UpdateRecipientAmount(recipId, amount);
}

bool CreateTransactionDialogAdvanced::isCurrentAmountValid() const
{
   if (qFuzzyIsNull(currentValue_)) {
      return false;
   }
   const double maxAmount = transactionData_->CalculateMaxAmount(currentAddress_);
   if ((maxAmount - currentValue_) < -0.00000001) {  // 1 satoshi difference is allowed due to rounding error
      UiUtils::setWrongState(ui_->lineEditAmount, true);
      return false;
   }
   UiUtils::setWrongState(ui_->lineEditAmount, false);
   return true;
}

void CreateTransactionDialogAdvanced::validateAddOutputButton()
{
   ui_->pushButtonMax->setEnabled(currentAddress_.isValid());
   ui_->pushButtonAddOutput->setEnabled(currentAddress_.isValid() && isCurrentAmountValid());
}

void CreateTransactionDialogAdvanced::validateCreateButton()
{
   const bool isTxValid = transactionData_->IsTransactionValid() && transactionData_->GetTransactionSummary().txVirtSize;

   updateCreateButtonText();
   ui_->pushButtonCreate->setEnabled(isTxValid
      && !broadcasting_
      && (ui_->radioButtonNewAddrNative->isChecked() || ui_->radioButtonNewAddrNested->isChecked()
         || (selectedChangeAddress_.isValid())));
}

void CreateTransactionDialogAdvanced::SetInputs(const std::vector<UTXO> &inputs)
{
   usedInputsModel_->updateInputs(inputs);

   const auto maxAmt = transactionData_->CalculateMaxAmount();
   const double recipSumAmt = transactionData_->GetTotalRecipientsAmount();
   if (!qFuzzyCompare(maxAmt, recipSumAmt)) {
      for (const auto &recip : transactionData_->allRecipientIds()) {
         const auto recipAmt = transactionData_->GetRecipientAmount(recip);
         transactionData_->UpdateRecipientAmount(recip, recipAmt, false);
      }
   }
}

void CreateTransactionDialogAdvanced::AddManualFeeEntries(float feePerByte, float totalFee)
{
   ui_->doubleSpinBoxFeesManualPerByte->setValue(feePerByte);
   ui_->spinBoxFeesManualTotal->setValue(qRound(totalFee));
   ui_->comboBoxFeeSuggestions->addItem(tr("Manual Fee Selection"));
   ui_->comboBoxFeeSuggestions->addItem(tr("Total Network Fee"));
}

void CreateTransactionDialogAdvanced::onFeeSuggestionsLoaded(const std::map<unsigned int, float> &feeValues)
{
   if (feeChangeDisabled_) {
      return;
   }

   CreateTransactionDialog::onFeeSuggestionsLoaded(feeValues);

   float manualFeePerByte = advisedFeePerByte_;
   if (manualFeePerByte < minFeePerByte_) {
      manualFeePerByte = minFeePerByte_;
   }
   if (qFuzzyIsNull(manualFeePerByte)) {
      manualFeePerByte = feeValues.begin()->second;
   }
   AddManualFeeEntries(manualFeePerByte
      , (minTotalFee_ > 0) ? minTotalFee_ : transactionData_->totalFee());

   if (advisedFeePerByte_ > 0) {
      const auto index = ui_->comboBoxFeeSuggestions->count() - 2;
      ui_->comboBoxFeeSuggestions->setCurrentIndex(index);
      feeSelectionChanged(index);
   }
}

void CreateTransactionDialogAdvanced::SetMinimumFee(float totalFee, float feePerByte)
{
   minTotalFee_ = totalFee;
   minFeePerByte_ = feePerByte;

   ui_->doubleSpinBoxFeesManualPerByte->setMinimum(feePerByte);
   ui_->spinBoxFeesManualTotal->setMinimum(qRound(totalFee));

   transactionData_->setMinTotalFee(minTotalFee_);
}

// currentIndex isn't being used. We should use it or lose it.
void CreateTransactionDialogAdvanced::feeSelectionChanged(int currentIndex)
{
   updateManualFeeControls();
   setTxFees();
}

bs::Address CreateTransactionDialogAdvanced::getChangeAddress() const
{
   bs::Address result;
   if (transactionData_->GetTransactionSummary().hasChange) {
      if (changeAddressFixed_) {
         result = selectedChangeAddress_;
      }
      else if (ui_->radioButtonNewAddrNative->isChecked() || ui_->radioButtonNewAddrNested->isChecked()) {
         result = transactionData_->getWallet()->getNewChangeAddress(
            ui_->radioButtonNewAddrNative->isChecked() ? AddressEntryType_P2WPKH : AddressEntryType_P2SH);
         transactionData_->getWallet()->setAddressComment(result, bs::sync::wallet::Comment::toString(
            bs::sync::wallet::Comment::ChangeAddress));
      } else {
         result = selectedChangeAddress_;
      }
   }
   return result;
}

void CreateTransactionDialogAdvanced::onCreatePressed()
{
   if (!importedSignedTX_.isNull()) {
      if (BroadcastImportedTx()) {
         accept();
      }
      else {
         initUI();
         validateCreateButton();
      }
      return;
   }

   if (!CreateTransaction()) {
      reject();
   }
}

bool CreateTransactionDialogAdvanced::HaveSignedImportedTransaction() const
{
   return !importedSignedTX_.isNull();
}

void CreateTransactionDialogAdvanced::SetImportedTransactions(const std::vector<bs::core::wallet::TXSignRequest>& transactions)
{
   ui_->pushButtonCreate->setEnabled(false);
   ui_->pushButtonCreate->setText(tr("Broadcast"));

   const auto &tx = transactions[0];
   if (!tx.prevStates.empty()) {    // signed TX
      ui_->textEditComment->insertPlainText(QString::fromStdString(tx.comment));

      if (tx.prevStates.size() == 1) {
         importedSignedTX_ = tx.prevStates[0];

         Tx tx(importedSignedTX_);
         if (tx.isInitialized()) {
            ui_->pushButtonCreate->setEnabled(true);

            std::set<BinaryData> txHashSet;
            std::map<BinaryData, std::set<uint32_t>> txOutIndices;
            std::vector<std::pair<BinaryData, uint32_t>> utxoHashes;

            for (size_t i = 0; i < tx.getNumTxIn(); i++) {
               auto in = tx.getTxInCopy((int)i);
               OutPoint op = in.getOutPoint();
               utxoHashes.push_back({ op.getTxHash(), op.getTxOutIndex() });
               txHashSet.insert(op.getTxHash());
               txOutIndices[op.getTxHash()].insert(op.getTxOutIndex());
            }

            const auto &cbTXs = [this, tx, utxoHashes, txOutIndices](std::vector<Tx> txs) {
               std::shared_ptr<bs::sync::Wallet> wallet;
               int64_t totalVal = 0;

               for (const auto &prevTx : txs) {
                  const auto &itTxOut = txOutIndices.find(prevTx.getThisHash());
                  if (itTxOut == txOutIndices.end()) {
                     continue;
                  }
                  for (const auto &txOutIdx : itTxOut->second) {
                     const auto prevOut = prevTx.getTxOutCopy(txOutIdx);
                     totalVal += prevOut.getValue();
                     if (!wallet) {
                        const auto addr = bs::Address::fromTxOut(prevOut);
                        const auto &addrWallet = walletsManager_->getWalletByAddress(addr);
                        if (addrWallet) {
                           wallet = addrWallet;
                        }
                     }
                  }
               }

               if (wallet) {
                  SetFixedWallet(wallet->walletId());
                  auto selInputs = transactionData_->GetSelectedInputs();
                  for (const auto &txHash : utxoHashes) {
                     selInputs->SetUTXOSelection(txHash.first, txHash.second);
                  }
               }
               std::vector<std::tuple<bs::Address, double, bool>> recipients;
               for (size_t i = 0; i < tx.getNumTxOut(); ++i) {
                  TxOut out = tx.getTxOutCopy((int)i);
                  const auto addr = bs::Address::fromTxOut(out);
                  if (wallet && (i == (tx.getNumTxOut() - 1)) && (wallet->containsAddress(addr))) {
                     SetFixedChangeAddress(QString::fromStdString(addr.display()));
                  }
                  else {
                     recipients.push_back({ addr, out.getValue() / BTCNumericTypes::BalanceDivider, false });
                  }
                  totalVal -= out.getValue();
               }
               if (!recipients.empty()) {
                  AddRecipients(recipients);
               }
               SetPredefinedFee(totalVal);
            };
            armory_->getTXsByHash(txHashSet, cbTXs);
         }
      }
   } else {
      SetFixedWallet(tx.walletId);
      if (tx.change.value) {
         SetFixedChangeAddress(QString::fromStdString(tx.change.address.display()));
      }
      SetPredefinedFee(tx.fee);
      labelEstimatedFee()->setText(UiUtils::displayAmount(tx.fee));
      ui_->textEditComment->insertPlainText(QString::fromStdString(tx.comment));
      auto selInputs = transactionData_->GetSelectedInputs();
      for (const auto &utxo : tx.inputs) {
         selInputs->SetUTXOSelection(utxo.getTxHash(), utxo.getTxOutIndex());
      }

      std::vector<std::tuple<bs::Address, double, bool>> recipients;
      for (const auto &recip : tx.recipients) {
         const auto addr = bs::Address::fromRecipient(recip);
         recipients.push_back({ addr, recip->getValue() / BTCNumericTypes::BalanceDivider, false });
      }
      AddRecipients(recipients);

      if (!signingContainer_->isOffline() && tx.isValid()) {
         ui_->pushButtonCreate->setEnabled(true);
      }
   }

   ui_->checkBoxRBF->setChecked(tx.RBF);
   ui_->checkBoxRBF->setEnabled(false);

   disableOutputsEditing();
   disableInputSelection();
   enableFeeChanging(false);
   feeChangeDisabled_ = true;
   updateCreateButtonText();
   disableChangeAddressSelecting();
}

void CreateTransactionDialogAdvanced::onImportPressed()
{
   const auto transactions = ImportTransactions();
   if (transactions.empty()) {
      return;
   }

   SetImportedTransactions(transactions);
}

void CreateTransactionDialogAdvanced::onNewAddressSelectedForChange()
{
   selectedChangeAddress_ = bs::Address{};
   showExistingChangeAddress(false);
}

void CreateTransactionDialogAdvanced::onExistingAddressSelectedForChange()
{
   SelectAddressDialog selectAddressDialog(walletsManager_, transactionData_->getWallet(), this
      , AddressListModel::AddressType::Internal);

   if (selectAddressDialog.exec() == QDialog::Accepted) {
      selectedChangeAddress_ = selectAddressDialog.getSelectedAddress();
      showExistingChangeAddress(true);
   } else {
      if (!selectedChangeAddress_.isValid()) {
         ui_->radioButtonNewAddrNative->setChecked(true);
      }
   }
}

void CreateTransactionDialogAdvanced::SetFixedWallet(const std::string& walletId, const std::function<void()> &cbInputsReset)
{
   const int idx = SelectWallet(walletId);
   selectedWalletChanged(idx, true, cbInputsReset);
   ui_->comboBoxWallets->setEnabled(false);
}

void CreateTransactionDialogAdvanced::setFixedWalletAndInputs(const std::shared_ptr<bs::sync::Wallet> &wallet, const std::vector<UTXO> &inputs)
{
   SelectWallet(wallet->walletId());
   ui_->comboBoxWallets->setEnabled(false);
   disableInputSelection();
   usedInputsModel_->updateInputs(inputs);
   transactionData_->setWalletAndInputs(wallet, inputs, armory_->topBlock());
}

void CreateTransactionDialogAdvanced::disableOutputsEditing()
{
   ui_->lineEditAddress->setEnabled(false);
   ui_->lineEditAmount->setEnabled(false);
   ui_->pushButtonMax->setEnabled(false);
   ui_->pushButtonAddOutput->setEnabled(false);
   outputsModel_->enableRows(false);

   removeOutputEnabled_ = false;
}

void CreateTransactionDialogAdvanced::disableInputSelection()
{
   ui_->pushButtonSelectInputs->setEnabled(false);
}

void CreateTransactionDialogAdvanced::enableFeeChanging(bool enable)
{
   if (enable && feeChangeDisabled_) {
      return;
   }
   ui_->comboBoxFeeSuggestions->setEnabled(enable);
   ui_->doubleSpinBoxFeesManualPerByte->setEnabled(enable);
   ui_->spinBoxFeesManualTotal->setEnabled(enable);
}

void CreateTransactionDialogAdvanced::SetFixedChangeAddress(const QString& changeAddress)
{
   ui_->radioButtonExistingAddress->setChecked(true);
   ui_->radioButtonNewAddrNative->setEnabled(false);
   ui_->radioButtonNewAddrNested->setEnabled(false);

   selectedChangeAddress_ = bs::Address{changeAddress.toStdString()};
   showExistingChangeAddress(true);

   changeAddressFixed_ = true;
}

void CreateTransactionDialogAdvanced::SetPredefinedFee(const int64_t& manualFee)
{
   ui_->comboBoxFeeSuggestions->clear();
   ui_->comboBoxFeeSuggestions->addItem(tr("%1 satoshi").arg(manualFee), (qlonglong)manualFee);
   transactionData_->setTotalFee(manualFee);
}

// Set a TX such that it can't be altered.
void CreateTransactionDialogAdvanced::setUnchangeableTx()
{
   ui_->comboBoxFeeSuggestions->setEnabled(false);
   ui_->treeViewOutputs->setEnabled(false);
   ui_->lineEditAddress->setEnabled(false);
   ui_->lineEditAmount->setEnabled(false);
   ui_->textEditComment->setEnabled(false);
   ui_->pushButtonCreate->setEnabled(false);
}

void CreateTransactionDialogAdvanced::showExistingChangeAddress(bool show)
{
   if (show && selectedChangeAddress_.isValid()) {
      ui_->labelChangeAddress->setText(QString::fromStdString(selectedChangeAddress_.display()));
   } else {
      ui_->labelChangeAddress->clear();
   }
}

void CreateTransactionDialogAdvanced::disableChangeAddressSelecting()
{
   ui_->widgetChangeAddress->setEnabled(false);
}

void CreateTransactionDialogAdvanced::updateManualFeeControls()
{
   int itemIndex = ui_->comboBoxFeeSuggestions->currentIndex();
   int itemCount = ui_->comboBoxFeeSuggestions->count();

   ui_->doubleSpinBoxFeesManualPerByte->setVisible(itemCount > 2 && itemIndex == itemCount - 2);

   const bool totalFeeSelected = (itemCount > 2) && (itemIndex == itemCount - 1);
   ui_->spinBoxFeesManualTotal->setVisible(totalFeeSelected);
   if (totalFeeSelected) {
      ui_->spinBoxFeesManualTotal->setValue((int)transactionData_->totalFee());
   }
}

void CreateTransactionDialogAdvanced::fixFeePerByte()
{
   const auto txVirtSize = transactionData_->GetTransactionSummary().txVirtSize;
   if (!txVirtSize) {
      return;
   }

   // Any time the TX is adjusted under RBF, the minimum fee is adjusted.
   if (isRBF_) {
      const uint64_t newMinFee = originalFee_ + txVirtSize;
      SetMinimumFee(newMinFee, originalFeePerByte_);
   }

   // If the new fee is less than the minimum required fee, increase the fee.
   const auto totalFee = txVirtSize * transactionData_->feePerByte();
   if ((minTotalFee_ > 0) && (totalFee > 0) && (totalFee <= minTotalFee_)) {
      const float newFPB = (minTotalFee_ + 1) / txVirtSize;
      if (std::abs(transactionData_->feePerByte() - newFPB) > 0.01) {
         transactionData_->setFeePerByte(newFPB);
         if (ui_->comboBoxFeeSuggestions->currentIndex() == (ui_->comboBoxFeeSuggestions->count() - 2)) {
            QMetaObject::invokeMethod(this, [this, newFPB] {
               ui_->doubleSpinBoxFeesManualPerByte->setValue(newFPB);
            });
         }
      }
   }
}

void CreateTransactionDialogAdvanced::setTxFees()
{
   const int itemIndex = ui_->comboBoxFeeSuggestions->currentIndex();
   const int itemCount = ui_->comboBoxFeeSuggestions->count();

   if (itemIndex < (ui_->comboBoxFeeSuggestions->count() - 2)) {
      CreateTransactionDialog::feeSelectionChanged(itemIndex);
   } else if (itemIndex == itemCount - 2) {
      transactionData_->setFeePerByte(float(ui_->doubleSpinBoxFeesManualPerByte->value()));
      fixFeePerByte();
   } else if (itemIndex == itemCount - 1) {
      uint64_t fee = ui_->spinBoxFeesManualTotal->value();
      if ((minTotalFee_ > 0) && (fee < minTotalFee_)) {
         fee = minTotalFee_;
      }
      transactionData_->setTotalFee(fee);
   }

   validateAddOutputButton();

   if (FixRecipientsAmount()) {
      ui_->comboBoxFeeSuggestions->setCurrentIndex(itemCount - 1);
      ui_->doubleSpinBoxFeesManualPerByte->setValue(minFeePerByte_);
      ui_->doubleSpinBoxFeesManualPerByte->setVisible(false);
      ui_->spinBoxFeesManualTotal->setVisible(true);
      ui_->spinBoxFeesManualTotal->setValue(transactionData_->GetTransactionSummary().totalFee);
      enableFeeChanging(false);
   }
}


QLabel* CreateTransactionDialogAdvanced::labelTXAmount() const
{
   return ui_->labelTransactionAmount;
}

QLabel* CreateTransactionDialogAdvanced::labelTxOutputs() const
{
   return ui_->labelTXOutputs;
}
