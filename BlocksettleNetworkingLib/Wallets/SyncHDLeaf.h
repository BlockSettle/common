#ifndef BS_SYNC_HD_LEAF_H
#define BS_SYNC_HD_LEAF_H

#include <atomic>
#include <functional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "ArmoryObject.h"
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
            Q_OBJECT
               friend class Group;

         public:
            using cb_complete_notify = std::function<void(bs::hd::Path::Elem wallet, bool isValid)>;

            Leaf(const std::string &walletId, const std::string &name, const std::string &desc
               , SignContainer *, const std::shared_ptr<spdlog::logger> &
               , bs::core::wallet::Type type = bs::core::wallet::Type::Bitcoin
               , bool extOnlyAddresses = false);
            ~Leaf() override;

            virtual void init(const bs::hd::Path &);
            void synchronize(const std::function<void()> &cbDone) override;

            void firstInit(bool force = false) override;
            std::string walletId() const override;
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
            bool getSpendableTxOutList(std::function<void(std::vector<UTXO>)>
               , QObject *obj, uint64_t val = UINT64_MAX) override;
            bool getSpendableZCList(std::function<void(std::vector<UTXO>)>
               , QObject *obj) override;
            bool getUTXOsToSpend(uint64_t val, std::function<void(std::vector<UTXO>)>) const override;
            bool getRBFTxOutList(std::function<void(std::vector<UTXO>)>) const override;
            bool getHistoryPage(uint32_t id, std::function<void(const bs::sync::Wallet *wallet
               , std::vector<ClientClasses::LedgerEntry>)>, bool onlyNew = false) const override;

            bool containsAddress(const bs::Address &addr) override;
            bool containsHiddenAddress(const bs::Address &addr) const override;

            std::vector<bs::Address> getExtAddressList() const override { return extAddresses_; }
            std::vector<bs::Address> getIntAddressList() const override { return intAddresses_; }
            size_t getExtAddressCount() const override { return extAddresses_.size(); }
            size_t getIntAddressCount() const override { return intAddresses_.size(); }
            bool isExternalAddress(const Address &) const override;
            bs::Address getNewExtAddress(AddressEntryType aet = AddressEntryType_Default
               , const CbAddress &cb = nullptr) override;
            bs::Address getNewIntAddress(AddressEntryType aet = AddressEntryType_Default
               , const CbAddress &cb = nullptr) override;
            bs::Address getNewChangeAddress(AddressEntryType aet = AddressEntryType_Default
               , const CbAddress &cb = nullptr) override;
            bs::Address getRandomChangeAddress(AddressEntryType aet = AddressEntryType_Default
               , const CbAddress &cb = nullptr) override;
            std::string getAddressIndex(const bs::Address &) override;
            bool addressIndexExists(const std::string &index) const override;
            bool getLedgerDelegateForAddress(const bs::Address &
               , const std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)> &
               , QObject *context = nullptr) override;

            int addAddress(const bs::Address &, const std::string &index, AddressEntryType, bool sync = true) override;

            void updateBalances(const std::function<void(std::vector<uint64_t>)> &cb = nullptr) override;
            bool getAddrBalance(const bs::Address &addr, std::function<void(std::vector<uint64_t>)>) const override;
            bool getAddrTxN(const bs::Address &addr, std::function<void(uint32_t)>) const override;
            bool getActiveAddressCount(const std::function<void(size_t)> &) const override;

            const bs::hd::Path &path() const { return path_; }
            bs::hd::Path::Elem index() const { return static_cast<bs::hd::Path::Elem>(path_.get(-1)); }

            void setArmory(const std::shared_ptr<ArmoryObject> &) override;
            std::vector<std::string> registerWallet(const std::shared_ptr<ArmoryObject> &armory = nullptr
               , bool asNew = false) override;
            void unregisterWallet() override;

            std::vector<BinaryData> getAddrHashes() const override;
            std::vector<BinaryData> getAddrHashesExt() const;
            std::vector<BinaryData> getAddrHashesInt() const;

            void setScanCompleteCb(const cb_complete_notify &cb) { cbScanNotify_ = cb; }
            void scanAddresses(unsigned int startIdx = 0, unsigned int portionSize = 100
               , const std::function<void(const std::string &walletId, unsigned int idx)> &cbw = nullptr);

         signals:
            void scanComplete(const std::string &walletId);

         protected slots:
            virtual void onZeroConfReceived(const std::vector<bs::TXEntry>);
            virtual void onRefresh(std::vector<BinaryData> ids, bool online);

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
            virtual bs::Address createAddress(const AddrPoolKey &, const CbAddress &, bool signal = true);
            void reset();
            bs::hd::Path getPathForAddress(const bs::Address &) const;
            void activateAddressesFromLedger(const std::vector<ClientClasses::LedgerEntry> &);
            void activateHiddenAddress(const bs::Address &);
            bs::Address createAddressWithIndex(const std::string &index, AddressEntryType, bool signal = true);
            bs::Address createAddressWithPath(const AddrPoolKey &, bool signal = true);
            virtual void topUpAddressPool(const std::function<void()> &cb = nullptr
               , size_t intAddresses = 0, size_t extAddresses = 0);
            void postOnline();

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

            std::shared_ptr<AsyncClient::BtcWallet>   btcWalletInt_;

            bs::hd::Path::Elem  lastIntIdx_ = 0;
            bs::hd::Path::Elem  lastExtIdx_ = 0;

            size_t intAddressPoolSize_ = 100;
            size_t extAddressPoolSize_ = 100;
            std::vector<AddressEntryType> poolAET_ = { AddressEntryType_P2SH, AddressEntryType_P2WPKH };

            std::set<AddrPoolKey>   tempAddresses_;
            std::unordered_map<AddrPoolKey, bs::Address, AddrPoolHasher>   addressPool_;
            std::map<bs::Address, AddrPoolKey>           poolByAddr_;

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

            struct Portion {
               std::vector<PooledAddress>  addresses;
               std::map<bs::Address, AddrPoolKey>  poolKeyByAddr;
               bs::hd::Path::Elem   start;
               bs::hd::Path::Elem   end;
               std::atomic_bool  registered;
               std::vector<PooledAddress> activeAddresses;
               Portion() : start(0), end(0), registered(false) {}
            };

            std::shared_ptr<AsyncClient::BtcWallet>   rescanWallet_;
            const std::string       rescanWalletId_;
            std::string             rescanRegId_;
            unsigned int            portionSize_ = 100;
            Portion                 currentPortion_;
            std::atomic_int         processing_;
            std::set<AddrPoolKey>   activeScanAddresses_;

         private:
            bs::Address createAddress(AddressEntryType aet, const CbAddress &cb = nullptr
               , bool isInternal = false);
            AddrPoolKey getAddressIndexForAddr(const BinaryData &addr) const;
            AddrPoolKey addressIndex(const bs::Address &) const;
            void onScanComplete();
            void onSaveToWallet(const std::vector<PooledAddress> &);
            bs::hd::Path::Elem getLastAddrPoolIndex(bs::hd::Path::Elem) const;

            std::string getWalletIdInt() const;

            static std::vector<BinaryData> getRegAddresses(const std::vector<PooledAddress> &src);
            void fillPortion(bs::hd::Path::Elem start, const std::function<void()> &cb, unsigned int size = 100);
            void processPortion();
         };


         class AuthLeaf : public Leaf
         {
         public:
            AuthLeaf(const std::string &walletId, const std::string &name, const std::string &desc
               , SignContainer *, const std::shared_ptr<spdlog::logger> &);

            void setUserId(const BinaryData &) override;

         protected:
            bs::Address createAddress(const AddrPoolKey &, const CbAddress &
               , bool signal = true) override;
            void topUpAddressPool(const std::function<void()> &cb = nullptr
               , size_t intAddresses = 0, size_t extAddresses = 0) override;

         private:
            BinaryData              userId_;
         };


         class CCLeaf : public Leaf
         {
            Q_OBJECT

         public:
            CCLeaf(const std::string &walletId, const std::string &name, const std::string &desc
               , SignContainer *, const std::shared_ptr<spdlog::logger> &
               , bool extOnlyAddresses = false);
            ~CCLeaf() override;

            bs::core::wallet::Type type() const override { return bs::core::wallet::Type::ColorCoin; }

            void setData(const std::string &) override;
            void setData(uint64_t data) override { lotSizeInSatoshis_ = data; }
            void firstInit(bool force) override;

            bool getSpendableTxOutList(std::function<void(std::vector<UTXO>)>
               , QObject *, uint64_t val = UINT64_MAX) override;
            bool getSpendableZCList(std::function<void(std::vector<UTXO>)>
               , QObject *) override;
            bool isBalanceAvailable() const override;
            BTCNumericTypes::balance_type getSpendableBalance() const override;
            BTCNumericTypes::balance_type getUnconfirmedBalance() const override;
            BTCNumericTypes::balance_type getTotalBalance() const override;
            bool getAddrBalance(const bs::Address &addr
               , std::function<void(std::vector<uint64_t>)>) const override;

            BTCNumericTypes::balance_type getTxBalance(int64_t) const override;
            QString displayTxValue(int64_t val) const override;
            QString displaySymbol() const override;
            bool isTxValid(const BinaryData &) const override;

            void setArmory(const std::shared_ptr<ArmoryObject> &) override;

         private slots:
            void onZeroConfReceived(const std::vector<bs::TXEntry>) override;
            void onStateChanged(ArmoryConnection::State);

         private:
            void validationProc();
            void findInvalidUTXOs(const std::vector<UTXO> &
               , std::function<void(const std::vector<UTXO> &)>);
            void refreshInvalidUTXOs(const bool& ZConly = false);
            BTCNumericTypes::balance_type correctBalance(BTCNumericTypes::balance_type
               , bool applyCorrection = true) const;
            std::vector<UTXO> filterUTXOs(const std::vector<UTXO> &) const;

            std::shared_ptr<TxAddressChecker>   checker_;
            uint64_t       lotSizeInSatoshis_ = 0;
            volatile bool  validationStarted_, validationEnded_;
            double         balanceCorrection_ = 0;
            std::set<UTXO> invalidTx_;
            std::set<BinaryData> invalidTxHash_;
         };

      }  //namespace hd
   }  //namespace sync
}  //namespace bs

#endif //BS_SYNC_HD_LEAF_H
