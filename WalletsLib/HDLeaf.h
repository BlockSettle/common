#ifndef __BS_HD_LEAF_H__
#define __BS_HD_LEAF_H__

#include <atomic>
#include <functional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "ArmoryConnection.h"
#include "HDNode.h"
#include "MetaData.h"

namespace spdlog {
   class logger;
}

namespace bs {
   class TxAddressChecker;

   namespace hd {
      class Group;

      class BlockchainScanner
      {
      protected:
         struct AddrPoolKey {
            Path  path;
            AddressEntryType  aet;

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
         using cb_completed = std::function<void()>;
         using cb_save_to_wallet = std::function<void(const std::vector<PooledAddress> &)>;
         using cb_write_last = std::function<void(const std::string &walletId, unsigned int idx)>;

      protected:
         BlockchainScanner(const cb_save_to_wallet &, const cb_completed &);
         ~BlockchainScanner() noexcept = default;

         void init(const std::shared_ptr<Node> &node, const std::string &walletId);
         void setArmory(const std::shared_ptr<ArmoryConnection> &armory) { armoryConn_ = armory; }

         std::vector<PooledAddress> generateAddresses(hd::Path::Elem prefix, hd::Path::Elem start
            , size_t nb, AddressEntryType aet);
         void scanAddresses(unsigned int startIdx, unsigned int portionSize = 100, const cb_write_last &cbw = nullptr);
         void onRefresh(const std::vector<BinaryData> &ids);

      private:
         struct Portion {
            std::vector<PooledAddress>  addresses;
            std::map<bs::Address, AddrPoolKey>  poolKeyByAddr;
            Path::Elem        start;
            Path::Elem        end;
            std::atomic_bool  registered;
            std::vector<PooledAddress> activeAddresses;
            Portion() : start(0), end(0), registered(false) {}
         };

         std::shared_ptr<Node>   node_;
         std::shared_ptr<ArmoryConnection>   armoryConn_;
         std::shared_ptr<AsyncClient::BtcWallet>   rescanWallet_;
         std::string             walletId_;
         std::string             rescanWalletId_;
         std::string             rescanRegId_;
         unsigned int            portionSize_ = 100;
         const cb_save_to_wallet cbSaveToWallet_;
         const cb_completed      cbCompleted_;
         cb_write_last           cbWriteLast_ = nullptr;
         Portion                 currentPortion_;
         std::atomic_int         processing_;

      private:
         bs::Address newAddress(const Path &path, AddressEntryType aet);
         static std::vector<BinaryData> getRegAddresses(const std::vector<PooledAddress> &src);
         void fillPortion(Path::Elem start, unsigned int size = 100);
         void processPortion();
      };


      class Leaf : public bs::Wallet, protected BlockchainScanner
      {
         Q_OBJECT
         friend class bs::hd::Group;

      public:
         using cb_complete_notify = std::function<void(Path::Elem wallet, bool isValid)>;

         Leaf(const std::string &name, const std::string &desc
            , bs::wallet::Type type = bs::wallet::Type::Bitcoin, bool extOnlyAddresses = false);
         ~Leaf() override;
         virtual void init(const std::shared_ptr<Node> &node, const hd::Path &, Nodes rootNodes);
         virtual bool copyTo(std::shared_ptr<hd::Leaf> &) const;

         void firstInit(bool force = false) override;
         std::string GetWalletId() const override;
         std::string GetWalletDescription() const override;
         void SetDescription(const std::string &desc) override { desc_ = desc; }
         std::string GetShortName() const override { return suffix_; }
         bs::wallet::Type GetType() const override { return type_; }
         bool isWatchingOnly() const override { return rootNodes_.empty(); }
         std::vector<wallet::EncryptionType> encryptionTypes() const override { return rootNodes_.encryptionTypes(); }
         std::vector<SecureBinaryData> encryptionKeys() const override { return rootNodes_.encryptionKeys(); }
         std::pair<unsigned int, unsigned int> encryptionRank() const override { return rootNodes_.rank(); }
         bool hasExtOnlyAddresses() const override { return isExtOnly_; }

         bool getSpendableTxOutList(std::function<void(std::vector<UTXO>)>
                                    , QObject *obj, const bool& startup = false
                                    , uint64_t val = UINT64_MAX) override;

         bool containsAddress(const bs::Address &addr) override;
         bool containsHiddenAddress(const bs::Address &addr) const override;
         BinaryData getRootId() const override;
         BinaryData getPubKey() const { return node_ ? node_->pubCompressedKey() : BinaryData(); }
         BinaryData getChainCode() const { return node_ ? node_->chainCode() : BinaryData(); }

         std::vector<bs::Address> GetExtAddressList() const override { return extAddresses_; }
         std::vector<bs::Address> GetIntAddressList() const override { return intAddresses_; }
         size_t GetExtAddressCount() const override { return extAddresses_.size(); }
         size_t GetIntAddressCount() const override { return intAddresses_.size(); }
         bool IsExternalAddress(const Address &) const override;
         bs::Address GetNewExtAddress(AddressEntryType aet = AddressEntryType_Default) override;
         bs::Address GetNewIntAddress(AddressEntryType aet = AddressEntryType_Default) override;
         bs::Address GetNewChangeAddress(AddressEntryType aet = AddressEntryType_Default) override;
         bs::Address GetRandomChangeAddress(AddressEntryType aet = AddressEntryType_Default) override;
         std::shared_ptr<AddressEntry> getAddressEntryForAddr(const BinaryData &addr) override;
         std::string GetAddressIndex(const bs::Address &) override;
         bool AddressIndexExists(const std::string &index) const override;
         bs::Address CreateAddressWithIndex(const std::string &index, AddressEntryType, bool signal = true) override;

         std::shared_ptr<ResolverFeed> GetResolver(const SecureBinaryData &password) override;
         std::shared_ptr<ResolverFeed> GetPublicKeyResolver() override;

         SecureBinaryData GetPublicKeyFor(const bs::Address &) override;
         SecureBinaryData GetPubChainedKeyFor(const bs::Address &) override;
         KeyPair GetKeyPairFor(const bs::Address &, const SecureBinaryData &password) override;

         const Path &path() const { return path_; }
         Path::Elem index() const { return static_cast<Path::Elem>(path_.get(-1)); }
         BinaryData serialize() const;
         void setDB(const std::shared_ptr<LMDBEnv> &env, LMDB *db);

         void SetArmory(const std::shared_ptr<ArmoryConnection> &) override;

         std::shared_ptr<LMDBEnv> getDBEnv() override { return dbEnv_; }
         LMDB *getDB() override { return db_; }

         AddressEntryType getAddrTypeForAddr(const BinaryData &) override;
         std::set<BinaryData> getAddrHashSet() override;
         void addAddress(const bs::Address &, const BinaryData &pubChainedKey, const Path &path);

         void setScanCompleteCb(const cb_complete_notify &cb) { cbScanNotify_ = cb; }
         void scanAddresses(unsigned int startIdx = 0, unsigned int portionSize = 100
            , const BlockchainScanner::cb_write_last &cbw = nullptr) {
            BlockchainScanner::scanAddresses(startIdx, portionSize, cbw);
         }

      signals:
         void scanComplete(const std::string &walletId);

      protected slots:
         virtual void onZeroConfReceived(ArmoryConnection::ReqIdType);
         virtual void onRefresh(std::vector<BinaryData> ids);

      protected:
         virtual bs::Address createAddress(const Path &path, Path::Elem index, AddressEntryType aet
            , bool signal = true);
         virtual BinaryData serializeNode() const { return node_ ? node_->serialize() : BinaryData{}; }
         virtual void setRootNodes(Nodes);
         void reset();
         Path getPathForAddress(const bs::Address &) const;
         std::shared_ptr<Node> getNodeForAddr(const bs::Address &) const;
         std::shared_ptr<hd::Node> GetPrivNodeFor(const bs::Address &, const SecureBinaryData &password);
         void activateAddressesFromLedger(const std::vector<ClientClasses::LedgerEntry> &);
         void activateHiddenAddress(const bs::Address &);
         bs::Address createAddressWithPath(const hd::Path &, AddressEntryType, bool signal = true);

      protected:
         const Path::Elem  addrTypeExternal = 0u;
         const Path::Elem  addrTypeInternal = 1u;
         const AddressEntryType defaultAET_ = AddressEntryType_P2WPKH;

         mutable std::string     walletId_;
         bs::wallet::Type        type_;
         std::shared_ptr<Node>   node_;
         Nodes                   rootNodes_;
         hd::Path                path_;
         std::string name_, desc_;
         std::string suffix_;
         bool        isExtOnly_ = false;

         Path::Elem  lastIntIdx_ = 0;
         Path::Elem  lastExtIdx_ = 0;

         size_t intAddressPoolSize_ = 100;
         size_t extAddressPoolSize_ = 100;
         const std::vector<AddressEntryType> poolAET_ = { AddressEntryType_P2SH, AddressEntryType_P2WPKH };

         std::map<BinaryData, BinaryData> hashToPubKey_;
         std::map<BinaryData, hd::Path>   pubKeyToPath_;
         using TempAddress = std::pair<Path, AddressEntryType>;
         std::unordered_map<Path::Elem, TempAddress>  tempAddresses_;
         std::unordered_map<AddrPoolKey, bs::Address, AddrPoolHasher>   addressPool_;
         std::map<bs::Address, AddrPoolKey>           poolByAddr_;

      private:
         shared_ptr<LMDBEnv> dbEnv_ = nullptr;
         LMDB* db_ = nullptr;
         using AddressTuple = std::tuple<bs::Address, std::shared_ptr<Node>, Path>;
         std::unordered_map<Path::Elem, AddressTuple> addressMap_;
         std::vector<bs::Address>                     intAddresses_;
         std::vector<bs::Address>                     extAddresses_;
         std::map<BinaryData, Path::Elem>             addrToIndex_;
         cb_complete_notify                           cbScanNotify_ = nullptr;
         volatile bool activateAddressesInvoked_ = false;

      private:
         bs::Address createAddress(AddressEntryType aet, bool isInternal = false);
         std::shared_ptr<AddressEntry> getAddressEntryForAsset(std::shared_ptr<AssetEntry> assetPtr
                         , AddressEntryType ae_type = AddressEntryType_Default);
         Path::Elem getAddressIndexForAddr(const BinaryData &addr) const;
         Path::Elem getAddressIndex(const bs::Address &addr) const;
         void onScanComplete();
         void onSaveToWallet(const std::vector<PooledAddress> &);
         void topUpAddressPool(size_t intAddresses = 0
                               , size_t extAddresses = 0);
         Path::Elem getLastAddrPoolIndex(Path::Elem) const;

         static void serializeAddr(BinaryWriter &bw, Path::Elem index
                                   , AddressEntryType, const Path &);
         bool deserialize(const BinaryData &ser, Nodes rootNodes);
      };


      class AuthLeaf : public Leaf
      {
      public:
         AuthLeaf(const std::string &name, const std::string &desc);

         void init(const std::shared_ptr<Node> &node, const hd::Path &, Nodes rootNodes) override;
         void SetUserID(const BinaryData &) override;

      protected:
         bs::Address createAddress(const Path &path, Path::Elem index, AddressEntryType aet
            , bool signal = true) override;
         BinaryData serializeNode() const override {
            return unchainedNode_ ? unchainedNode_->serialize() : BinaryData{};
         }
         void setRootNodes(Nodes) override;

      private:
         std::shared_ptr<Node>   unchainedNode_;
         Nodes                   unchainedRootNodes_;
         BinaryData              userId_;
      };


      class CCLeaf : public Leaf
      {
         Q_OBJECT

      public:
         CCLeaf(const std::string &name, const std::string &desc,
                const std::shared_ptr<spdlog::logger> &logger,
                bool extOnlyAddresses = false);
         ~CCLeaf() override;

         wallet::Type GetType() const override { return wallet::Type::ColorCoin; }

         void setData(const std::string &) override;
         void setData(uint64_t data) override { lotSizeInSatoshis_ = data; }
         void firstInit(bool force) override;

         bool getSpendableTxOutList(std::function<void(std::vector<UTXO>)>
                                    , QObject *, const bool& startup = false
                                    , uint64_t val = UINT64_MAX) override;
         bool getSpendableZCList(std::function<void(std::vector<UTXO>)>
                                 , QObject *
                                 , const bool& startup = false) override;
         bool isBalanceAvailable() const override;
         BTCNumericTypes::balance_type GetSpendableBalance() const override;
         BTCNumericTypes::balance_type GetUnconfirmedBalance() const override;
         BTCNumericTypes::balance_type GetTotalBalance() const override;
         bool getAddrBalance(const bs::Address &) const override;
         bool getAddrBalance(const bs::Address &addr
                   , std::function<void(std::vector<uint64_t>)>) const override;

         BTCNumericTypes::balance_type GetTxBalance(int64_t) const override;
         QString displayTxValue(int64_t val) const override;
         QString displaySymbol() const override;
         bool isTxValid(const BinaryData &) const override;

         void SetArmory(const std::shared_ptr<ArmoryConnection> &) override;

      private slots:
         void onZeroConfReceived(ArmoryConnection::ReqIdType) override;
         void onStateChanged(ArmoryConnection::State);

      private:
         void validationProc(const bool& initValidation);
         void findInvalidUTXOs(const std::vector<UTXO> &
                               , std::function<void (const std::vector<UTXO> &)>);
         void refreshInvalidUTXOs(const bool& initValidation
                                  , const bool& ZConly = false);
         BTCNumericTypes::balance_type correctBalance(BTCNumericTypes::balance_type
                                           , bool applyCorrection = true) const;
         std::vector<UTXO> filterUTXOs(const std::vector<UTXO> &) const;

         std::shared_ptr<TxAddressChecker>   checker_;
         std::shared_ptr<spdlog::logger>     logger_;
         uint64_t       lotSizeInSatoshis_ = 0;
         volatile bool  validationStarted_, validationEnded_;
         double         balanceCorrection_ = 0;
         std::set<UTXO> invalidTx_;
         std::set<BinaryData> invalidTxHash_;
      };

   }  //namespace hd
}  //namespace bs

#endif //__BS_HD_LEAF_H__
