#ifndef CORE_WALLETS_MANAGER_H
#define CORE_WALLETS_MANAGER_H

#include <memory>
#include <vector>
#include <unordered_map>
#include "BTCNumericTypes.h"
#include "EncryptionUtils.h"
#include "CoreSettlementWallet.h"
#include "CoreWallet.h"


namespace spdlog {
   class logger;
}
namespace bs {
   namespace core {
      namespace hd {
         class Leaf;
         class Wallet;
      }

      class WalletsManager
      {
      public:
         using CbProgress = std::function<void(size_t cur, size_t total)>;
         using WalletPtr = std::shared_ptr<bs::core::hd::Leaf>;
         using HDWalletPtr = std::shared_ptr<bs::core::hd::Wallet>;

         WalletsManager(const std::shared_ptr<spdlog::logger> &, unsigned int nbBackups = 10);
         ~WalletsManager() noexcept = default;

         WalletsManager(const WalletsManager&) = delete;
         WalletsManager& operator = (const WalletsManager&) = delete;
         WalletsManager(WalletsManager&&) = delete;
         WalletsManager& operator = (WalletsManager&&) = delete;

         void reset();

         bool walletsLoaded() const { return walletsLoaded_; }
         void loadWallets(NetworkType, const std::string &walletsPath, const CbProgress &cb = nullptr);
         HDWalletPtr loadWoWallet(NetworkType, const std::string &walletsPath, const std::string &walletFileName);
         void backupWallet(const HDWalletPtr &, const std::string &targetDir) const;

         bool empty() const { return hdWallets_.empty(); }
         WalletPtr getWalletById(const std::string& walletId) const;
         WalletPtr getWalletByAddress(const bs::Address &addr) const;

         WalletPtr createSettlementWallet(NetworkType, const std::string &walletsPath);
//         WalletPtr getSettlementWallet() const { return settlementWallet_; }
/*         void setSettlementWallet(const std::shared_ptr<bs::core::SettlementWallet> &wallet) {
            settlementWallet_ = wallet; }*/
//         WalletPtr getAuthWallet() const;
         HDWalletPtr getPrimaryWallet() const;

         size_t getHDWalletsCount() const { return hdWalletsId_.size(); }
         const HDWalletPtr getHDWallet(const unsigned int index) const;
         const HDWalletPtr getHDWalletById(const std::string &walletId) const;
         const HDWalletPtr getHDRootForLeaf(const std::string &walletId) const;

         bool deleteWalletFile(const WalletPtr &);
         bool deleteWalletFile(const HDWalletPtr &);

         HDWalletPtr createWallet(const std::string& name, const std::string& description
            , bs::core::wallet::Seed, const std::string &folder
            , const SecureBinaryData& passprase, bool primary);
         void addWallet(const HDWalletPtr &);

         std::vector<std::string> ccLeaves() const { return ccLeaves_; }
         void setCCLeaves(const std::vector<std::string> &ccLeaves) { ccLeaves_ = ccLeaves; }

      private:
         bool isWalletFile(const std::string &fileName) const;
         void saveWallet(const HDWalletPtr &);
         void eraseWallet(const WalletPtr &);

      private:
         std::shared_ptr<spdlog::logger>        logger_;
         bool  walletsLoaded_ = false;
         const unsigned int                  nbBackupFilesToKeep_;
         std::unordered_map<std::string, HDWalletPtr> hdWallets_;
         //std::unordered_map<std::string, WalletPtr>   wallets_;
         std::unordered_set<std::string>     walletNames_;
         //std::vector<BinaryData>             walletsId_;
         std::vector<std::string>            hdWalletsId_;
         std::shared_ptr<SettlementWallet>   settlementWallet_;
         std::vector<std::string>            ccLeaves_;
      };

   }  //namespace core
}  //namespace bs

#endif // CORE_WALLETS_MANAGER_H
