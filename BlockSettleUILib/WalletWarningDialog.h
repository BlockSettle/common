#ifndef __WALLET_WARNING_DIALOG_H__
#define __WALLET_WARNING_DIALOG_H__

#include <QDialog>

namespace Ui
{
   class WalletWarningDialog;
};

class WalletWarningDialog : public QDialog
{
Q_OBJECT

public:
   WalletWarningDialog(QWidget* parent = nullptr);
   ~WalletWarningDialog() override;

private:
   std::unique_ptr<Ui::WalletWarningDialog> ui_;
};


#endif // __WALLET_WARNING_DIALOG_H__
