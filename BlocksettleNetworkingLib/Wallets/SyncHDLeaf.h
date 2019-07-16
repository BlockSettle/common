#ifndef BS_SYNC_HD_LEAF_H
#define BS_SYNC_HD_LEAF_H

#include <atomic>
#include <functional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "ArmoryConnection.h"
#include "HDPath.h"
#include "SyncWallet.h"

namespace spdlog {
   class logger;
}

namespace bs {
   class TxAddressChecker;

   namespace sync {
      namespace hd {
         class Group;

         class Leaf : public bs::sync::Wallet
         {
            friend class Group;

         public:
            using cb_complete_notify = std::function<void(bs::hd::Path::Elem wallet, bool isValid)>;

            Leaf(const std::string &walletId, const std::string &name, const std::string &desc
               , SignContainer *, const std::shared_ptr<spdlog::logger> &
               , bs::core::wallet::Type type = bs::core::wallet::Type::Bitcoin
               , bool extOnlyAddresses = false);
            ~Leaf() override;

            virtual void setPath(const bs::hd::Path &);
            void synchronize(const std::function<void()> &cbDone) override;

            void init(bool force = false) override;
            
            const std::string& walletId() const override;
            const std::string& walletIdInt() const override;

            std::string description() const override;
            void setDescription(const std::string &desc) override { desc_ = desc; }
            std::string shortName() const override { return suffix_; }
            bs::core::wallet::Type type() const override { return type_; }
            std::vector<bs::wallet::EncryptionType> encryptionTypes() const override { return encryptionTypes_; }
            std::vector<SecureBinaryData> encryptionKeys() const override { return encryptionKeys_; }
            std::pair<unsigned int, unsigned int> encryptionRank() const override { return encryptionRank_; }
            bool hasExtOnlyAddresses() const override { return isExtOnly_; }
            bool hasId(const std::string &) const override;

            BTCNumericTypes::balance_type getSpendableBalance() const override;
            bool getHistoryPage(uint32_t id, std::function<void(const bs::sync::Wallet *wallet
               , std::vector<ClientClasses::LedgerEntry>)>, bool onlyNew = false) const;

            bool containsAddress(const bs::Address &addr) override;
            bool containsHiddenAddress(const bs::Address &addr) const override;

            std::vector<bs::Address> getExtAddressList() const override { return extAddresses_; }
            std::vector<bs::Address> getIntAddressList() const override { return intAddresses_; }

            size_t getExtAddressCount() const override { return extAddresses_.size(); }
            size_t getIntAddressCount() const override { return intAddresses_.size(); }
            size_t getAddressPoolSize() const { return addressPool_.size(); }

            bool isExternalAddress(const Address &) const override;
            void getNewExtAddress(const CbAddress &, AddressEntryType aet = AddressEntryType_Default) override;
            void getNewIntAddress(const CbAddress &, AddressEntryType aet = AddressEntryType_Default) override;
            void getNewChangeAddress(const CbAddress &, AddressEntryType aet = AddressEntryType_Default) override;
            std::string getAddressIndex(const bs::Address &) override;
            bool addressIndexExists(const std::string &index) const override;
            bool getLedgerDelegateForAddress(const bs::Address &
               , const std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)> &) override;

            int addAddress(const bs::Address &, const std::string &index, AddressEntryType, bool sync = true) override;

            const bs::hd::Path &path() const { return path_; }
            bs::hd::Path::Elem index() const { return static_cast<bs::hd::Path::Elem>(path_.get(-1)); }

            std::vector<std::string> registerWallet(const std::shared_ptr<ArmoryConnection> &armory = nullptr
               , bool asNew = false) override;

            std::vector<BinaryData> getAddrHashes() const override;
            std::vector<BinaryData> getAddrHashesExt() const;
            std::vector<BinaryData> getAddrHashesInt() const;

            virtual void merge(const std::shared_ptr<Wallet>) override;

            std::vector<std::string> setUnconfirmedTarget(void);

         protected:
            struct AddrPoolKey {
               bs::hd::Path  path;
               AddressEntryType  aet;

               bool empty() const { return (path.length() == 0); }
               bool operator < (const AddrPoolKey &other) const {
                  if (path == other.path) {
                     return aet < other.aet;
                  }
                  return (path < other.path);
               }
               bool operator==(const AddrPoolKey &other) const {
                  return ((path == other.path) && (aet == other.aet));
               }
            };
            struct AddrPoolHasher {
               std::size_t operator()(const AddrPoolKey &key) const {
                  return (std::hash<std::string>()(key.path.toString()) ^ std::hash<int>()((int)key.aet));
               }
            };

            using PooledAddress = std::pair<AddrPoolKey, bs::Address>;

         protected:
            void onRefresh(const std::vector<BinaryData> &ids, bool online) override;
            virtual void createAddress(const CbAddress &cb, const AddrPoolKey &);
            void reset();
            bs::hd::Path getPathForAddress(const bs::Address &) const;
            virtual void topUpAddressPool(bool extInt, const std::function<void()> &cb = nullptr);
            void postOnline();
            bool isOwnId(const std::string &) const override;

         protected:
            const bs::hd::Path::Elem   addrTypeExternal = 0u;
            const bs::hd::Path::Elem   addrTypeInternal = 1u;
            const AddressEntryType defaultAET_ = AddressEntryType_P2WPKH;

            mutable std::string     walletId_, walletIdInt_;
            bs::core::wallet::Type  type_;
            bs::hd::Path            path_;
            std::string name_, desc_;
            std::string suffix_;
            bool  isExtOnly_ = false;
            std::vector<bs::wallet::EncryptionType>   encryptionTypes_;
            std::vector<SecureBinaryData>          encryptionKeys_;
            std::pair<unsigned int, unsigned int>  encryptionRank_{0, 0};

            std::shared_ptr<AsyncClient::BtcWallet>   btcWallet_;
            std::shared_ptr<AsyncClient::BtcWallet>   btcWalletInt_;

            bs::hd::Path::Elem  lastIntIdx_ = 0;
            bs::hd::Path::Elem  lastExtIdx_ = 0;

            size_t intAddressPoolSize_ = 20;
            size_t extAddressPoolSize_ = 100;
            std::vector<AddressEntryType> poolAET_ = { 
               AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH), 
               AddressEntryType_P2WPKH };

            std::set<AddrPoolKey>   tempAddresses_;
            std::map<AddrPoolKey, bs::Address>  addressPool_;
            std::map<bs::Address, AddrPoolKey>  poolByAddr_;

         private:
            std::unordered_map<AddrPoolKey, bs::Address, AddrPoolHasher>   addressMap_;
            std::vector<bs::Address>                     intAddresses_;
            std::vector<bs::Address>                     extAddresses_;
            std::map<BinaryData, AddrPoolKey>            addrToIndex_;
            cb_complete_notify                           cbScanNotify_ = nullptr;
            std::function<void(const std::string &walletId, unsigned int idx)> cbWriteLast_ = nullptr;
            volatile bool activateAddressesInvoked_ = false;
            BTCNumericTypes::balance_type spendableBalanceCorrection_ = 0;

            struct AddrPrefixedHashes {
               std::set<BinaryData> external;
               std::set<BinaryData> internal;

               void clear() {
                  external.clear();
                  internal.clear();
               }
            };
            mutable AddrPrefixedHashes addrPrefixedHashes_;

            std::string regIdExt_, regIdInt_;
            std::mutex  regMutex_;
            std::vector<std::string> unconfTgtRegIds_;

         private:
            void createAddress(const CbAddress &, AddressEntryType aet, bool isInternal = false);
            AddrPoolKey getAddressIndexForAddr(const BinaryData &addr) const;
            AddrPoolKey addressIndex(const bs::Address &) const;
            bs::hd::Path::Elem getLastAddrPoolIndex(bs::hd::Path::Elem) const;

            static std::vector<BinaryData> getRegAddresses(const std::vector<PooledAddress> &src);
         };


         class AuthLeaf : public Leaf
         {
         public:
            AuthLeaf(const std::string &walletId, const std::string &name, const std::string &desc
               , SignContainer *, const std::shared_ptr<spdlog::logger> &);

            void setUserId(const BinaryData &) override;

         protected:
            void createAddress(const CbAddress &, const AddrPoolKey &) override;
            void topUpAddressPool(bool extInt, const std::function<void()> &cb = nullptr) override;

         private:
            BinaryData              userId_;
         };


         class CCLeaf : public Leaf
         {
         public:
            CCLeaf(const std::string &walletId, const std::string &name, const std::string &desc
               , SignContainer *,const std::shared_ptr<spdlog::logger> &
               , bool extOnlyAddresses = false);
            ~CCLeaf() override;

            bs::core::wallet::Type type() const override { return bs::core::wallet::Type::ColorCoin; }

            void setCCDataResolver(const std::shared_ptr<CCDataResolver> &resolver);
            void init(bool force) override;
            void setPath(const bs::hd::Path &) override;

            bool getSpendableZCList(const ArmoryConnection::UTXOsCb &) const override;
            bool isBalanceAvailable() const override;
            BTCNumericTypes::balance_type getSpendableBalance() const override;
            BTCNumericTypes::balance_type getUnconfirmedBalance() const override;
            BTCNumericTypes::balance_type getTotalBalance() const override;
            std::vector<uint64_t> getAddrBalance(const bs::Address &addr) const override;

            BTCNumericTypes::balance_type getTxBalance(int64_t) const override;
            QString displayTxValue(int64_t val) const override;
            QString displaySymbol() const override;
            bool isTxValid(const BinaryData &) const override;

            void setArmory(const std::shared_ptr<ArmoryConnection> &) override;

         protected:
            void onZeroConfReceived(const std::vector<bs::TXEntry> &) override;

         private:
            void validationProc();
            void findInvalidUTXOs(const std::vector<UTXO> &
               , const ArmoryConnection::UTXOsCb &);
            void refreshInvalidUTXOs(const bool& ZConly = false);
            BTCNumericTypes::balance_type correctBalance(BTCNumericTypes::balance_type
               , bool applyCorrection = true) const;
            std::vector<UTXO> filterUTXOs(const std::vector<UTXO> &) const;

            class CCWalletACT : public WalletACT
            {
            public:
               CCWalletACT(ArmoryConnection *armory, Wallet *leaf)
                  : WalletACT(armory, leaf) {}
               void onStateChanged(ArmoryState) override;
            };

            std::shared_ptr<TxAddressChecker>   checker_;
            std::shared_ptr<CCDataResolver>     ccResolver_;
            volatile bool  validationStarted_, validationEnded_;
            double         balanceCorrection_ = 0;
            std::set<UTXO> invalidTx_;
            std::set<BinaryData> invalidTxHash_;
         };


         class SettlementLeaf : public Leaf
         {
         public:
            SettlementLeaf(const std::string &walletId, const std::string &name, 
               const std::string &desc, SignContainer *, const std::shared_ptr<spdlog::logger> &);

            SecureBinaryData getRootPubkey(void) const;
            void setSettlementID(const SecureBinaryData&);

         protected:
            void createAddress(const CbAddress &, const AddrPoolKey &) override;
            void topUpAddressPool(bool extInt, const std::function<void()> &cb = nullptr) override;

         private:
            BinaryData              userId_;
         };

      }  //namespace hd
   }  //namespace sync
}  //namespace bs

#endif //BS_SYNC_HD_LEAF_H
