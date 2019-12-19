/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_SYNC_HD_WALLET_H
#define BS_SYNC_HD_WALLET_H

#include <functional>
#include "SignerDefs.h"
#include "SyncHDGroup.h"
#include "SyncHDLeaf.h"
#include "WalletEncryption.h"

namespace spdlog {
   class logger;
}
class WalletSignerContainer;

namespace bs {
   namespace sync {
      class Wallet;

      namespace hd {

         class Wallet : public WalletCallbackTarget
         {
         public:
            using cb_scan_notify = std::function<void(Group *, bs::hd::Path::Elem wallet, bool isValid)>;
            using cb_scan_read_last = std::function<unsigned int(const std::string &walletId)>;
            using cb_scan_write_last = std::function<void(const std::string &walletId, unsigned int idx)>;

            Wallet(const bs::sync::WalletInfo &info, WalletSignerContainer * = nullptr
               , const std::shared_ptr<spdlog::logger> &logger = nullptr);

            Wallet(const bs::sync::WatchingOnlyWallet &info, WalletSignerContainer * = nullptr
               , const std::shared_ptr<spdlog::logger> &logger = nullptr);

            ~Wallet() override;

            Wallet(const Wallet&) = delete;
            Wallet& operator = (const Wallet&) = delete;
            Wallet(Wallet&&) = delete;
            Wallet& operator = (Wallet&&) = delete;

            void synchronize(const std::function<void()> &cbDone);

            std::vector<bs::wallet::EncryptionType> encryptionTypes() const;
            std::vector<BinaryData> encryptionKeys() const;
            bs::wallet::KeyRank encryptionRank() const { return encryptionRank_; }
            bool isPrimary() const;
            bool isOffline() const { return isOffline_; }
            NetworkType networkType() const { return netType_; }

            std::shared_ptr<Group> getGroup(bs::hd::CoinType ct) const;
            std::shared_ptr<Group> createGroup(bs::hd::CoinType ct, bool isExtOnly);
            void addGroup(const std::shared_ptr<Group> &group);
            size_t getNumGroups() const { return groups_.size(); }
            std::vector<std::shared_ptr<Group>> getGroups() const;
            virtual size_t getNumLeaves() const;
            std::vector<std::shared_ptr<bs::sync::Wallet>> getLeaves() const;
            std::shared_ptr<bs::sync::Wallet> getLeaf(const std::string &id) const;

            virtual std::string walletId() const;
            std::string name() const { return name_; }
            std::string description() const { return desc_; }

            BTCNumericTypes::balance_type getTotalBalance() const;

            void setUserId(const BinaryData &usedId);
            bool deleteRemotely();

            std::vector<std::string> registerWallet(
               const std::shared_ptr<ArmoryConnection> &, bool asNew = false);
            std::vector<std::string> setUnconfirmedTargets(void);

            void setArmory(const std::shared_ptr<ArmoryConnection> &);
            void startRescan();
            void scan(const std::function<void(bs::sync::SyncState)> &);

            static bs::hd::CoinType getXBTGroupType();

            void merge(const Wallet&);

            template<class U> void setCustomACT(
               const std::shared_ptr<ArmoryConnection> &armory)
            {
               const auto &leaves = getLeaves();
               for (auto& leaf : leaves)
               {
                  //settlement leaves are not registered, they dont need an ACT
                  if (leaf->type() == bs::core::wallet::Type::Settlement)
                     continue;

                  leaf->setCustomACT<U>(armory);
               }
            }

            void setWCT(WalletCallbackTarget *);

            //settlement shenanigans
            void getSettlementPayinAddress(const SecureBinaryData &settlId
               , const SecureBinaryData &cpPubKey, const bs::sync::Wallet::CbAddress &
               , bool myFirst = true) const;

         protected:
            void addressAdded(const std::string &walletId) override { wct_->addressAdded(walletId); }
            void walletReady(const std::string &walletId) override { wct_->walletReady(walletId); }
            void balanceUpdated(const std::string &walletId) override { wct_->balanceUpdated(walletId); }
            void metadataChanged(const std::string &) override { wct_->metadataChanged(walletId()); }
            void walletCreated(const std::string &walletId) override;
            void walletDestroyed(const std::string &walletId) override;

         protected:
            WalletCallbackTarget *wct_{};
            const std::string walletId_;
            const std::string name_, desc_;
            NetworkType    netType_ = NetworkType::MainNet;
            std::map<bs::hd::Path::Elem, std::shared_ptr<Group>>        groups_;
            mutable std::map<std::string, std::shared_ptr<bs::sync::Wallet>>  leaves_;
            BinaryData        userId_;
            WalletSignerContainer  *  signContainer_{};
            std::shared_ptr<ArmoryConnection>   armory_;
            std::shared_ptr<spdlog::logger>     logger_;
            std::vector<bs::wallet::EncryptionType>   encryptionTypes_;
            std::vector<BinaryData>                   encryptionKeys_;
            bs::wallet::KeyRank  encryptionRank_{ 1, 1 };
            const bool isOffline_;

         private:
            std::unordered_set<std::string>  scannedLeaves_;
         };


/*!         class DummyWallet : public Wallet    // Just a container for old-style wallets
         {
         public:
            DummyWallet(const std::shared_ptr<spdlog::logger> &logger)
               : Wallet(NetworkType::Invalid, "Dummy", tr("Armory Wallets").toStdString()
                  , "", logger) {}

            size_t getNumLeaves() const override { return leaves_.size(); }

            void add(const std::shared_ptr<bs::sync::Wallet> wallet) {
               leaves_[wallet->walletId()] = wallet;
            }
         };*/
      }  //namespace hd
   }  //namespace sync
}  //namespace bs

#endif //BS_SYNC_HD_WALLET_H
