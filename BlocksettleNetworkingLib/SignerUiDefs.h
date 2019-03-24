#ifndef __SIGNER_UI_DEFS_H__
#define __SIGNER_UI_DEFS_H__

#include "QString"

namespace bs {

// List of callable Signer dialogs
enum SignerDialog {
   CreateWallet,
   ImportWallet,
   BackupWallet,
   DeleteWallet,
   ManageWallet
};

inline QString getSignerDialogPath(SignerDialog signerDialog) {
   switch (signerDialog) {
   case CreateWallet:
      return QStringLiteral("createNewWalletDialog");
   case ImportWallet:
      return QStringLiteral("importWalletDialog");
   case BackupWallet:
      return QStringLiteral("backupWalletDialog");
   case DeleteWallet:
      return QStringLiteral("deleteWalletDialog");
   case ManageWallet:
      return QStringLiteral("manageEncryptionDialog");


   default:
      return QStringLiteral("");
   }
}

}
#endif
