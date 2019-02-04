#ifndef __IMPORT_WALLET_NEW_DIALOG_H__
#define __IMPORT_WALLET_NEW_DIALOG_H__

#include <memory>
#include <QDialog>

#include "BtcDefinitions.h"
#include "EasyCoDec.h"
#include "MetaData.h"

namespace Ui {
   class ImportWalletNewDialog;
}
class ApplicationSettings;
class ArmoryConnection;
class AssetManager;
class AuthAddressManager;
class SignContainer;
class WalletImporter;
class WalletsManager;

class ImportWalletNewDialog : public QDialog
{
Q_OBJECT

public:
   ImportWalletNewDialog(const std::shared_ptr<WalletsManager> &, const std::shared_ptr<SignContainer> &
      , const std::shared_ptr<AssetManager> &, const std::shared_ptr<AuthAddressManager> &
      , const std::shared_ptr<ArmoryConnection> &
      , const EasyCoDec::Data& walletData, const EasyCoDec::Data& chainCodeData
      , const std::shared_ptr<ApplicationSettings> &
      , const std::shared_ptr<spdlog::logger> &logger
      , const QString& username
      , const std::string &walletName = {}, const std::string &walletDesc = {}
      , QWidget *parent = nullptr);
   ~ImportWalletNewDialog() override;

   QString getNewWalletName() const { return walletName_; }
   std::string getWalletId() const { return walletId_; }
   std::shared_ptr<WalletImporter> getWalletImporter() const { return walletImporter_; }

   bool ImportedAsPrimary() const { return importedAsPrimary_; }

private slots:
   void onImportAccepted();
   void onWalletCreated(const std::string &walletId);
   void onError(const QString &errMsg);
   void onKeyTypeChanged(bool password);
   void updateAcceptButtonState();

protected:
   void reject() override;

private:
   std::unique_ptr<Ui::ImportWalletNewDialog> ui_;
   std::shared_ptr<WalletsManager>  walletsMgr_;
   std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<ArmoryConnection>      armory_;
   std::shared_ptr<WalletImporter>  walletImporter_;
   std::shared_ptr<spdlog::logger> logger_;
   bs::wallet::Seed  walletSeed_;
   std::string walletId_;
   QString     walletName_;
   bool importedAsPrimary_ = false;
   bool authNoticeWasShown_ = false;
};

#endif // __IMPORT_WALLET_NEW_DIALOG_H__
