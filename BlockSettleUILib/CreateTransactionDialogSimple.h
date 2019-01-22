#ifndef __CREATE_TRANSACTION_DIALOG_SIMPLE_H__
#define __CREATE_TRANSACTION_DIALOG_SIMPLE_H__

#include "CreateTransactionDialog.h"

namespace Ui {
    class CreateTransactionDialogSimple;
}
class ArmoryConnection;
class CreateTransactionDialogAdvanced;


class CreateTransactionDialogSimple : public CreateTransactionDialog
{
Q_OBJECT

public:
   CreateTransactionDialogSimple(const std::shared_ptr<ArmoryConnection> &
      , const std::shared_ptr<WalletsManager> &
      , const std::shared_ptr<SignContainer> &
      , const std::shared_ptr<spdlog::logger>&, QWidget* parent = nullptr);
   ~CreateTransactionDialogSimple() override;

   bool userRequestedAdvancedDialog() const;
   std::shared_ptr<CreateTransactionDialogAdvanced> CreateAdvancedDialog();

protected:
   QComboBox * comboBoxWallets() const override;
   QComboBox *comboBoxFeeSuggestions() const override;
   QLineEdit *lineEditAddress() const override;
   QLineEdit *lineEditAmount() const override;
   QPushButton *pushButtonMax() const override;
   QTextEdit *textEditComment() const override;
   QCheckBox *checkBoxRBF() const override;
   QLabel *labelBalance() const override;
   QLabel *labelAmount() const override;
   QLabel *labelTxInputs() const override;
   QLabel *labelEstimatedFee() const override;
   QLabel *labelTotalAmount() const override;
   QLabel *labelTxSize() const override;
   QPushButton *pushButtonCreate() const override;
   QPushButton *pushButtonCancel() const override;
   QLabel *feePerByteLabel() const override;
   QLabel *changeLabel() const override;

   bs::Address getChangeAddress() const override;

protected slots:
   void onMaxPressed() override;

private slots:
   void showAdvanced();
   void onAddressTextChanged(const QString &address);
   void onXBTAmountChanged(const QString& text);
   void createTransaction();
   void onImportPressed();

private:
   void initUI();

   std::unique_ptr<Ui::CreateTransactionDialogSimple> ui_;
   unsigned int   recipientId_ = 0;
   bool  advancedDialogRequested_ = false;

   std::vector<bs::wallet::TXSignRequest> offlineTransactions_;
};

#endif // __CREATE_TRANSACTION_DIALOG_SIMPLE_H__
