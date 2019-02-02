#ifndef __WALLETS_WIDGET_H__
#define __WALLETS_WIDGET_H__

#include <memory>
#include <QWidget>
#include "WalletsManager.h"
#include "TabWithShortcut.h"


namespace Ui {
    class WalletsWidget;
}
namespace bs {
   namespace hd {
      class Wallet;
   }
}
class AddressListModel;
class AddressSortFilterModel;
class ApplicationSettings;
class ArmoryConnection;
class AssetManager;
class AuthAddressManager;
class QAction;
class QMenu;
class SignContainer;
class WalletImporter;
class WalletsViewModel;


class WalletsWidget : public TabWithShortcut
{
Q_OBJECT

public:
   WalletsWidget(QWidget* parent = nullptr );
   ~WalletsWidget() override;

   void init(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<WalletsManager> &, const std::shared_ptr<SignContainer> &
      , const std::shared_ptr<ApplicationSettings> &, const std::shared_ptr<AssetManager> &
      , const std::shared_ptr<AuthAddressManager> &, const std::shared_ptr<ArmoryConnection> &);

   void setUsername(const QString& username);

   std::vector<WalletsManager::wallet_gen_type> GetSelectedWallets() const;
   std::vector<WalletsManager::wallet_gen_type> GetFirstWallets() const;

   bool CreateNewWallet(bool report = true);
   bool ImportNewWallet(bool report = true);

   void shortcutActivated(ShortcutType s) override;

private:
   void InitWalletsView(const std::string& defaultWalletId);

   void showInfo(bool report, const QString &title, const QString &text) const;
   void showError(const QString &text) const;

   int getUIFilterSettings() const;
   void updateAddressFilters(int filterSettings);

signals:
   void showContextMenu(QMenu *, QPoint);

private slots:
   void showWalletProperties(const QModelIndex& index);
   void showSelectedWalletProperties();
   void showAddressProperties(const QModelIndex& index);
   void updateAddresses();
   void onAddressContextMenu(const QPoint &);
   void onWalletContextMenu(const QPoint &);
   void onNewWallet();
   void onCopyAddress();
   void onEditAddrComment();
   void onRevokeSettlement();
   void onTXSigned(unsigned int id, BinaryData signedTX, std::string error, bool cancelledByUser);
   void onDeleteWallet();
   void onFilterSettingsChanged();
   void onEnterKeyInAddressesPressed(const QModelIndex &index);
   void onEnterKeyInWalletsPressed(const QModelIndex &index);
   void onShowContextMenu(QMenu *, QPoint);
   void onWalletBalanceChanged(std::string);

private:
   std::unique_ptr<Ui::WalletsWidget> ui;

   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<WalletsManager>  walletsManager_;
   std::shared_ptr<SignContainer>   signingContainer_;
   std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<AssetManager>          assetManager_;
   std::shared_ptr<AuthAddressManager>    authMgr_;
   std::shared_ptr<ArmoryConnection>      armory_;
   WalletsViewModel        *  walletsModel_;
   AddressListModel        *  addressModel_;
   AddressSortFilterModel  *  addressSortFilterModel_;
   std::unordered_map<std::string, std::shared_ptr<WalletImporter>>  walletImporters_;
   QAction  *  actCopyAddr_ = nullptr;
   QAction  *  actEditComment_ = nullptr;
   QAction  *  actRevokeSettl_ = nullptr;
   QAction  *  actDeleteWallet_ = nullptr;
   bs::Address curAddress_;
   std::shared_ptr<bs::Wallet>   curWallet_;
   unsigned int   revokeReqId_ = 0;
   QString username_;
   std::vector<std::shared_ptr<bs::Wallet>>  prevSelectedWallets_;
};

bool WalletBackupAndVerify(const std::shared_ptr<bs::hd::Wallet> &
   , const std::shared_ptr<SignContainer> &
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<spdlog::logger> &logger
   , QWidget *parent);

#endif // __WALLETS_WIDGET_H__
