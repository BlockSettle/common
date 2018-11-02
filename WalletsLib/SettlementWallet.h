#ifndef __BS_SETTLEMENT_WALLET_H__
#define __BS_SETTLEMENT_WALLET_H__

#include <atomic>
#include <string>
#include <vector>
#include <unordered_set>

#include <QObject>

#include "Addresses.h"
#include "Assets.h"
#include "BlockDataManagerConfig.h"
#include "BtcDefinitions.h"
#include "PlainWallet.h"


namespace spdlog {
   class logger;
};

namespace bs {
   class SettlementAddressEntry;
   class SettlementAssetEntry : public GenericAsset
   {
   public:
      SettlementAssetEntry(const BinaryData &settlementId, const BinaryData &buyAuthPubKey
         , const BinaryData &sellAuthPubKey, int id = -1)
         : GenericAsset(AssetEntryType_Multisig, id)
         , settlementId_(settlementId), buyAuthPubKey_(buyAuthPubKey), sellAuthPubKey_(sellAuthPubKey)
         , addrType_(AddressEntryType_P2WSH)
      {}
      ~SettlementAssetEntry() override = default;

      static std::shared_ptr<SettlementAddressEntry> getAddressEntry(const shared_ptr<SettlementAssetEntry> &);
      static std::pair<bs::Address, std::shared_ptr<GenericAsset>> deserialize(BinaryDataRef value);
      BinaryData serialize(void) const override;

      bool hasPrivateKey(void) const override { return false; }
      const BinaryData &getPrivateEncryptionKeyId(void) const override { return {}; }

      const BinaryData &settlementId() const { return settlementId_; }
      const BinaryData &buyAuthPubKey() const { return buyAuthPubKey_; }
      const BinaryData &buyChainedPubKey() const;
      const BinaryData &sellAuthPubKey() const { return sellAuthPubKey_; }
      const BinaryData &sellChainedPubKey() const;

      AddressEntryType addressType() const { return addrType_; }

      const BinaryData &script() const;
      void setScript(const BinaryData &script) { script_ = script; }
      const BinaryData &p2wshScript() const;

      BinaryData hash() const { return BtcUtils::hash160(script()); }
      const BinaryData &prefixedHash() const;
      const BinaryData &p2wsHash() const;
      const BinaryData &prefixedP2SHash() const;

      const std::vector<BinaryData> &supportedAddresses() const;
      const std::vector<BinaryData> &supportedAddrHashes() const;

   private:
      BinaryData  settlementId_;
      BinaryData  buyAuthPubKey_;
      BinaryData  sellAuthPubKey_;
      AddressEntryType     addrType_;
      mutable BinaryData   script_;
      mutable BinaryData   p2wshScript_;
      mutable BinaryData   p2wshScriptH160_;
      mutable BinaryData   hash_;
      mutable BinaryData   p2wsHash_;
      mutable BinaryData   prefixedP2SHash_;
      mutable BinaryData   buyChainedPubKey_;
      mutable BinaryData   sellChainedPubKey_;
      mutable std::vector<BinaryData>  supportedHashes_, supportedAddresses_;
   };


   class SettlementAddressEntry : public AddressEntry
   {
   public:
      SettlementAddressEntry(const std::shared_ptr<SettlementAssetEntry> &ae, AddressEntryType aeType = AddressEntryType_Multisig)
         : AddressEntry(aeType), ae_(ae) {}
      ~SettlementAddressEntry() noexcept override = default;

      const std::shared_ptr<SettlementAssetEntry>  getAsset() const { return ae_; }
      const BinaryData &getAddress() const override { return ae_->script(); }
      const BinaryData &getScript() const override { return ae_->script(); }
      const BinaryData &getPreimage(void) const override { return ae_->script(); }
      const BinaryData &getPrefixedHash() const override { return ae_->prefixedHash(); }
      const BinaryData &getHash() const override { return ae_->hash(); }
      shared_ptr<ScriptRecipient> getRecipient(uint64_t val) const override { return bs::Address(getPrefixedHash()).getRecipient(val); }
      size_t getInputSize(void) const override { return getAddress().getSize() + 2 + 73 * 1/*m*/ + 40; }

      const BinaryData& getID(void) const override { return ae_->getID(); }
      int getIndex() const { return ae_->getIndex(); }

   protected:
      std::shared_ptr<SettlementAssetEntry>  ae_;
   };

   class SettlementAddressEntry_P2SH : public SettlementAddressEntry
   {
   public:
      SettlementAddressEntry_P2SH(const std::shared_ptr<SettlementAssetEntry> &ae)
         : SettlementAddressEntry(ae, AddressEntryType_P2SH) {}

      const BinaryData &getPrefixedHash(void) const override { return ae_->prefixedP2SHash(); }
      const BinaryData &getHash() const override { return ae_->p2wsHash(); }
      shared_ptr<ScriptRecipient> getRecipient(uint64_t val) const override { return std::make_shared<Recipient_P2SH>(ae_->p2wsHash(), val); }
      size_t getInputSize(void) const override { return 75; }
   };

   class SettlementAddressEntry_P2WSH : public SettlementAddressEntry
   {
   public:
      SettlementAddressEntry_P2WSH(const std::shared_ptr<SettlementAssetEntry> &ae)
         : SettlementAddressEntry(ae, AddressEntryType_P2WSH) {}

      const BinaryData &getPrefixedHash(void) const override;
      const BinaryData &getHash() const override;
      const BinaryData &getAddress() const override { return BtcUtils::scrAddrToSegWitAddress(getHash()); }
      shared_ptr<ScriptRecipient> getRecipient(uint64_t val) const override { return std::make_shared<Recipient_PW2SH>(getHash(), val); }
      size_t getInputSize(void) const override { return 41; }

   private:
      mutable BinaryData   hash_;
      mutable BinaryData   prefixedHash_;
   };


   class SettlementMonitor;

   class SettlementWallet : public PlainWallet
   {
      Q_OBJECT

   public:
      SettlementWallet();
      SettlementWallet(const std::string &filename);
      ~SettlementWallet() override = default;

      SettlementWallet(const SettlementWallet&) = delete;
      SettlementWallet(SettlementWallet&&) = delete;
      SettlementWallet& operator = (const SettlementWallet&) = delete;
      SettlementWallet& operator = (SettlementWallet&&) = delete;

      std::shared_ptr<bs::SettlementAddressEntry> getExistingAddress(const BinaryData &settlementId);

      std::shared_ptr<SettlementAddressEntry> newAddress(const BinaryData &settlementId
         , const BinaryData &buyAuthPubKey, const BinaryData &sellAuthPubKey, const std::string &comment = {});
      bool containsAddress(const bs::Address &addr) override;

      bool hasWalletId(const std::string &id) const;
      bool isTempWalletId(const std::string &id) const;
      wallet::Type GetType() const override { return wallet::Type::Settlement; }

      static std::string fileNamePrefix() { return "settlement_"; }
      std::string getFileName(const std::string &dir) const override;

      bool GetInputFor(const shared_ptr<SettlementAddressEntry> &, std::function<void(UTXO)>, bool allowZC = true);
      uint64_t GetEstimatedFeeFor(UTXO input, const bs::Address &recvAddr, float feePerByte);

      bs::wallet::TXSignRequest CreatePayoutTXRequest(const UTXO &, const bs::Address &recvAddr, float feePerByte);
      UTXO GetInputFromTX(const std::shared_ptr<SettlementAddressEntry> &, const BinaryData &payinHash, const double amount) const;
      BinaryData SignPayoutTXRequest(const bs::wallet::TXSignRequest &, const KeyPair &, const BinaryData &settlementId
         , const BinaryData &buyAuthKey, const BinaryData &sellAuthKey);

      bool getSpendableZCList(std::function<void(std::vector<UTXO>)>, QObject *obj=nullptr) override;

      std::shared_ptr<ResolverFeed> GetResolver(const SecureBinaryData &) override { return nullptr; }   // can't resolve without external data
      std::shared_ptr<ResolverFeed> GetPublicKeyResolver() override { return nullptr; }   // no public keys are stored

      bs::Address GetNewExtAddress(AddressEntryType) override { return {}; }  // can't generate address without input data
      bs::Address GetNewIntAddress(AddressEntryType) override { return {}; }  // can't generate address without input data

      std::shared_ptr<AddressEntry> getAddressEntryForAddr(const BinaryData &addr) override;
      std::string GetAddressIndex(const bs::Address &) override;
      bool AddressIndexExists(const std::string &index) const override;
      bs::Address CreateAddressWithIndex(const std::string &index, AddressEntryType, bool signal = true) override;

      SecureBinaryData GetPublicKeyFor(const bs::Address &) override;
      KeyPair GetKeyPairFor(const bs::Address &, const SecureBinaryData &password) override;

      std::shared_ptr<SettlementMonitor> createMonitor(const shared_ptr<SettlementAddressEntry> &addr
         , const std::shared_ptr<spdlog::logger>& logger);

   protected:
      int addAddress(const bs::Address &, std::shared_ptr<GenericAsset> asset) override;
      int addAddress(const std::shared_ptr<SettlementAddressEntry> &, const std::shared_ptr<SettlementAssetEntry> &);
      std::pair<bs::Address, std::shared_ptr<GenericAsset>> deserializeAsset(BinaryDataRef ref) override {
         return SettlementAssetEntry::deserialize(ref);
      }

      AddressEntryType getAddrTypeForAddr(const BinaryData &addr) override;

   private:
      std::shared_ptr<bs::SettlementAddressEntry> getAddressBySettlementId(const BinaryData &settlementId) const;

      void createTempWalletForAsset(const std::shared_ptr<SettlementAssetEntry>& asset);

   private:
      mutable std::atomic_flag                           lockAddressMap_ = ATOMIC_FLAG_INIT;
      std::map<bs::Address, std::shared_ptr<SettlementAddressEntry>>    addrEntryByAddr_;
      std::map<BinaryData, std::shared_ptr<bs::SettlementAddressEntry>> addressBySettlementId_;
      std::map<int, std::shared_ptr<AsyncClient::BtcWallet>>   rtWallets_;
      std::unordered_map<std::string, int>                     rtWalletsById_;
   };


   struct PayoutSigner
   {
      enum Type {
         SignatureUndefined,
         SignedByBuyer,
         SignedBySeller
      };

      static const char *toString(const Type t) {
         switch (t) {
         case SignedByBuyer:        return "buyer";
         case SignedBySeller:       return "seller";
         case SignatureUndefined:
         default:                   return "undefined";
         }
      }

      static void WhichSignature(const Tx &
         , uint64_t value
         , const std::shared_ptr<bs::SettlementAddressEntry> &
         , const std::shared_ptr<spdlog::logger> &
         , const std::shared_ptr<ArmoryConnection> &, std::function<void(Type)>);
   };

   class SettlementMonitor : public QObject
   {
      Q_OBJECT
      friend class SettlementWallet;
   public:
      SettlementMonitor(const std::shared_ptr<AsyncClient::BtcWallet> rtWallet
         , const std::shared_ptr<ArmoryConnection> &
         , const shared_ptr<bs::SettlementAddressEntry> &
         , const std::shared_ptr<spdlog::logger> &
         , QObject *parent = nullptr);

      ~SettlementMonitor() noexcept override;

      void start();
      void stop();

      int getPayinConfirmations() const { return payinConfirmations_; }
      int getPayoutConfirmations() const { return payoutConfirmations_; }

      PayoutSigner::Type getPayoutSignerSide() const { return payoutSignedBy_; }

      int confirmedThreshold() const { return 6; }

   signals:
      // payin detected is sent on ZC and once it's get to block.
      // if payin is already on chain before monitor started, payInDetected will
      // emited only once
      void payInDetected(int confirmationsNumber, const BinaryData &txHash);

      void payOutDetected(int confirmationsNumber, PayoutSigner::Type signedBy);
      void payOutConfirmed(PayoutSigner::Type signedBy);

   protected slots:
      void checkNewEntries(unsigned int);

   private:
      std::atomic_bool                 stopped_;
      std::atomic_flag                 walletLock_ = ATOMIC_FLAG_INIT;
      std::shared_ptr<AsyncClient::BtcWallet>   rtWallet_;
      std::shared_ptr<ArmoryConnection>         armory_;
      std::set<BinaryData>             ownAddresses_;
      int               id_;

      std::string                            addressString_;
      shared_ptr<bs::SettlementAddressEntry> addressEntry_;

      int payinConfirmations_;
      int payoutConfirmations_;

      bool payinInBlockChain_;
      bool payoutConfirmedFlag_;

      PayoutSigner::Type payoutSignedBy_ = PayoutSigner::Type::SignatureUndefined;

      std::shared_ptr<spdlog::logger> logger_;

   protected:
      void IsPayInTransaction(const ClientClasses::LedgerEntry &, std::function<void(bool)>) const;
      void IsPayOutTransaction(const ClientClasses::LedgerEntry &, std::function<void(bool)>) const;

      void CheckPayoutSignature(const ClientClasses::LedgerEntry &, std::function<void(PayoutSigner::Type)>) const;

      void SendPayInNotification(const int confirmationsNumber, const BinaryData &txHash);
      void SendPayOutNotification(const ClientClasses::LedgerEntry &);
   };

}  //namespace bs

#endif //__BS_SETTLEMENT_WALLET_H__
