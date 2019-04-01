#ifndef __SIGNER_UI_DEFS_H__
#define __SIGNER_UI_DEFS_H__

#include "QObject"
#include "QString"

class SignerUiDefs
{
   Q_GADGET
public:
   // List of callable Signer dialogs
   enum class SignerDialog {
      CreateWallet,
      ImportWallet,
      BackupWallet,
      DeleteWallet,
      ManageWallet
   };

   static QString getSignerDialogPath(SignerDialog signerDialog) {
      switch (signerDialog) {
      case SignerDialog::CreateWallet:
         return QStringLiteral("createNewWalletDialog");
      case SignerDialog::ImportWallet:
         return QStringLiteral("importWalletDialog");
      case SignerDialog::BackupWallet:
         return QStringLiteral("backupWalletDialog");
      case SignerDialog::DeleteWallet:
         return QStringLiteral("deleteWalletDialog");
      case SignerDialog::ManageWallet:
         return QStringLiteral("manageEncryptionDialog");

      default:
         return QStringLiteral("");
      }
   }


   enum class SignerRunMode {
      fullgui,
      lightgui,
      headless,
      cli
   };
   Q_ENUM(SignerRunMode)
};

#endif
