/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_SYNC_WALLETS_MANAGER_H
#define BS_SYNC_WALLETS_MANAGER_H

#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>

#include "ArmoryConnection.h"
#include "BSErrorCode.h"
#include "BTCNumericTypes.h"
#include "CoreWallet.h"
#include "SyncWallet.h"
#include "ValidityFlag.h"
#include "WalletSignerContainer.h"


#define RECIP_GROUP_SPEND_1 0xA000
#define RECIP_GROUP_CHANG_1 0xA001
#define RECIP_GROUP_SPEND_2 0xB000
#define RECIP_GROUP_CHANG_2 0xB001

namespace spdlog {
   class logger;
}
class ApplicationSettings;
class ColoredCoinTrackerClient;
class WalletSignerContainer;
class CcTrackerClient;

namespace bs {
   namespace hd {
      class Wallet;
   }
   namespace sync {
      namespace hd {
         class CCLeaf;
         class DummyWallet;
         class Group;
         class SettlementLeaf;
         class Wallet;
      }
      class Wallet;

      using RecipientMap = std::map<unsigned, std::vector<std::shared_ptr<ArmorySigner::ScriptRecipient>>>;

      class WalletsManager : public QObject, public ArmoryCallbackTarget, public WalletCallbackTarget
      {
         Q_OBJECT
      public:
         using CbProgress = std::function<void(int, int)>;
         using WalletPtr = std::shared_ptr<Wallet>;     // Generic wallet interface
         using HDWalletPtr = std::shared_ptr<hd::Wallet>;
         using GroupPtr = std::shared_ptr<hd::Group>;

         // trackerClient is optional, if not set armory connection is used
         WalletsManager(const std::shared_ptr<spdlog::logger> &, const std::shared_ptr<ApplicationSettings>& appSettings
            , const std::shared_ptr<ArmoryConnection> &, const std::shared_ptr<CcTrackerClient> &trackerClient = nullptr);
         ~WalletsManager() noexcept override;

         WalletsManager(const WalletsManager&) = delete;
         WalletsManager& operator = (const WalletsManager&) = delete;
         WalletsManager(WalletsManager&&) = delete;
         WalletsManager& operator = (WalletsManager&&) = delete;

         void setSignContainer(const std::shared_ptr<WalletSignerContainer> &container);
         void reset();

         bool syncWallets(const CbProgress &cb = nullptr);

         bool isSynchronising() const;
         bool isWalletsReady() const;

         size_t walletsCount() const { return wallets_.size(); }
         bool hasPrimaryWallet() const;
         HDWalletPtr getPrimaryWallet() const;

         std::vector<WalletPtr> getAllWallets() const;
         std::vector<std::string> getHwWallets(
            bs::wallet::HardwareEncKey::WalletType walletType, std::string deviceId = {}) const;
         WalletPtr getWalletById(const std::string& walletId) const;
         WalletPtr getWalletByAddress(const bs::Address &addr) const;
         WalletPtr getDefaultWallet() const;
         GroupPtr getGroupByWalletId(const std::string &walletId) const;

         bool PromoteWalletToPrimary(const std::string& walletId);

         bool CreateCCLeaf(const std::string &cc
            , const std::function<void(bs::error::ErrorCode result)> &cb = nullptr);

         WalletPtr getCCWallet(const std::string &cc);
         bool isValidCCOutpoint(const std::string &cc, const BinaryData &txHash
            , uint32_t txOutIndex, uint64_t value) const;

         const WalletPtr getAuthWallet() const { return authAddressWallet_; }

         void createSettlementLeaf(const bs::Address &authAddr
            , const std::function<void(const SecureBinaryData &)> &);

         std::vector<HDWalletPtr> hdWallets() const { return hdWallets_; }
         const HDWalletPtr getHDWalletById(const std::string &walletId) const;
         const HDWalletPtr getHDRootForLeaf(const std::string &walletId) const;
         bool walletNameExists(const std::string &walletName) const;
         bool isWatchingOnly(const std::string &walletId) const;

      private:
         // Do not use references here (could crash when underlying pointers are cleared)
         bool deleteWallet(WalletPtr);
         bool deleteWallet(HDWalletPtr);

      public:

         std::shared_ptr<CCDataResolver> ccResolver() const { return ccResolver_; }

         bool isArmoryReady() const;
         bool isReadyForTrading() const;

         void goOnline();

         BTCNumericTypes::balance_type getSpendableBalance() const;
         BTCNumericTypes::balance_type getUnconfirmedBalance() const;
         BTCNumericTypes::balance_type getTotalBalance() const;

         std::vector<std::string> registerWallets();
         void unregisterWallets();

         bool getTransactionDirection(Tx, const std::string &walletId
            , const std::function<void(Transaction::Direction, std::vector<bs::Address> inAddrs)> &);
         bool getTransactionMainAddress(const Tx &, const std::string &walletId
            , bool isReceiving, const std::function<void(QString, int)> &);

         void adoptNewWallet(const HDWalletPtr &);

         bool estimatedFeePerByte(const unsigned int blocksToWait, std::function<void(float)>, QObject *obj = nullptr);
         bool getFeeSchedule(const std::function<void(const std::map<unsigned int, float> &)> &);

         //run after registration to update address chain usage counters
         void trackAddressChainUse(std::function<void(bool)>);

         std::shared_ptr<bs::sync::hd::SettlementLeaf> getSettlementLeaf(const bs::Address &addr) const;
         bool hasSettlementLeaf(const bs::Address &addr) const { return (getSettlementLeaf(addr) != nullptr); }

         bool mergeableEntries(const bs::TXEntry &, const bs::TXEntry &) const;
         std::vector<bs::TXEntry> mergeEntries(const std::vector<bs::TXEntry> &) const;

         static core::wallet::TXSignRequest createPartialTXRequest(uint64_t spendVal
            , const std::map<UTXO, std::string> &inputs, bs::Address changeAddress = {}
            , float feePerByte = 0, uint32_t topHeight = 0
            , const RecipientMap &recipients = {}
            , unsigned changeGroup = UINT32_MAX
            , const Codec_SignerState::SignerState &prevPart = {}, bool useAllInputs = false
            , unsigned assumedRecipientCount = UINT32_MAX
            , const std::shared_ptr<spdlog::logger> &logger = nullptr);

         std::shared_ptr<ColoredCoinTrackerClient> tracker(const std::string &cc) const;

         std::string getDefaultSpendWalletId() const;

      signals:
         void CCLeafCreated(const std::string& ccName);
         void CCLeafCreateFailed(const std::string& ccName, bs::error::ErrorCode result);

         void AuthLeafCreated();

         void walletPromotedToPrimary(const std::string& walletId);

         void walletChanged(const std::string &walletId);
         void walletDeleted(const std::string &walletId);
         void walletAdded(const std::string &walletId);
         void walletsReady();
         void walletsSynchronizationStarted();
         void walletsSynchronized();
         void walletBalanceUpdated(const std::string &walletId);
         void walletMetaChanged(const std::string &walletId);
         void walletIsReady(const std::string &walletId);
         void newWalletAdded(const std::string &walletId);
         void authWalletChanged();
         void blockchainEvent();
         void info(const QString &title, const QString &text) const;
         void error(const QString &title, const QString &text) const;
         void walletImportStarted(const std::string &walletId);
         void walletImportFinished(const std::string &walletId);
         void newTransactions(std::vector<bs::TXEntry>) const;
         void invalidatedZCs(const std::set<BinaryData> &ids) const;
         void ccTrackerReady(const std::string &cc);
         void settlementLeavesLoaded(unsigned int);

      public slots:
         void onCCSecurityInfo(QString ccProd, unsigned long nbSatoshis, QString genesisAddr);
         void onCCInfoLoaded();

      private:
         void onZCReceived(const std::string& requestId, const std::vector<bs::TXEntry>&) override;
         void onZCInvalidated(const std::set<BinaryData> &ids) override;
         void onTxBroadcastError(const std::string& requestId, const BinaryData &txHash, int errCode
            , const std::string &errMsg) override;
         void onNewBlock(unsigned int height, unsigned int branchHeight) override;
         void onStateChanged(ArmoryState) override;

      private slots:
         void onWalletsListUpdated();

      private:
         void addressAdded(const std::string &) override;
         void balanceUpdated(const std::string &) override;
         void walletReady(const std::string &) override;
         void walletCreated(const std::string &) override;
         void walletDestroyed(const std::string &) override;
         void walletReset(const std::string &) override;
         void scanComplete(const std::string &walletId) override;
         void metadataChanged(const std::string &walletId) override;

         bool empty() const { return wallets_.empty(); }

         void syncWallet(const bs::sync::WalletInfo &, const std::function<void()> &cbDone);
         void addWallet(const WalletPtr &, bool isHDLeaf = false);
         void addWallet(const HDWalletPtr &);
         void saveWallet(const WalletPtr &);
         void saveWallet(const HDWalletPtr &);
         void eraseWallet(const WalletPtr &);

         void updateTxDirCache(const std::string &txKey, Transaction::Direction
            , const std::vector<bs::Address> &inAddrs
            , std::function<void(Transaction::Direction, std::vector<bs::Address>)>);
         void updateTxDescCache(const std::string &txKey, const QString &, int
            , std::function<void(QString, int)>);

         void invokeFeeCallbacks(unsigned int blocks, float fee);

         BTCNumericTypes::balance_type getBalanceSum(
            const std::function<BTCNumericTypes::balance_type(const WalletPtr &)> &) const;

         void startWalletRescan(const HDWalletPtr &);

         using MaintQueueCb = std::function<void()>;
         void addToQueue(const MaintQueueCb &);
         void threadFunction();

         void processCreatedCCLeaf(const std::string &cc, bs::error::ErrorCode result
            , const std::string &walletId);

         void processPromoteWallet(bs::error::ErrorCode result, const std::string& walletId);

         void startTracker(const std::string &cc);
         void updateTracker(const std::shared_ptr<bs::sync::hd::CCLeaf> &ccLeaf);
         void checkTrackerUpdate(const std::string &cc);

      private:
         std::shared_ptr<WalletSignerContainer>         signContainer_;
         std::shared_ptr<spdlog::logger>        logger_;
         std::shared_ptr<ApplicationSettings>   appSettings_;
         std::shared_ptr<ArmoryConnection>      armoryPtr_;

         using wallet_container_type = std::unordered_map<std::string, WalletPtr>;
         using hd_wallet_container_type = std::vector<HDWalletPtr>;

         hd_wallet_container_type            hdWallets_;
         std::unordered_set<std::string>     walletNames_;
         wallet_container_type               wallets_;
         mutable QMutex                      mtxWallets_;
         std::set<std::string>               readyWallets_;
         bool     isReady_ = false;
         WalletPtr                           authAddressWallet_;
         std::set<std::string>               newWallets_;
         mutable std::unordered_map<std::string, GroupPtr>  groupsByWalletId_;

         class CCResolver : public CCDataResolver
         {
         public:
            std::string nameByWalletIndex(bs::hd::Path::Elem) const override;
            uint64_t lotSizeFor(const std::string &cc) const override;
            bs::Address genesisAddrFor(const std::string &cc) const override;
            std::vector<std::string> securities() const override;

            void addData(const std::string &cc, uint64_t lotSize, const bs::Address &genAddr);

         private:
            struct CCInfo {
               uint64_t    lotSize;
               bs::Address genesisAddr;
            };
            std::unordered_map<std::string, CCInfo>   securities_;
            std::unordered_map<bs::hd::Path::Elem, std::string>   walletIdxMap_;
         };
         std::shared_ptr<CCResolver>   ccResolver_;

         std::map<std::string, std::shared_ptr<ColoredCoinTrackerClient>>  trackers_;

         std::unordered_map<std::string, std::pair<Transaction::Direction, std::vector<bs::Address>>> txDirections_;
         mutable std::atomic_flag      txDirLock_ = ATOMIC_FLAG_INIT;
         std::unordered_map<std::string, std::pair<QString, int>> txDesc_;
         mutable std::atomic_flag      txDescLock_ = ATOMIC_FLAG_INIT;

         mutable std::map<unsigned int, float>     feePerByte_;
         mutable std::map<unsigned int, QDateTime> lastFeePerByte_;
         std::map<QObject *, std::map<unsigned int
            , std::pair<QPointer<QObject>, std::function<void(float)>>>> feeCallbacks_;

         enum class WalletsSyncState
         {
            NotSynced = 0,
            Running,
            Synced
         };

         std::atomic<WalletsSyncState>  syncState_{ WalletsSyncState::NotSynced };

         std::atomic_bool           threadRunning_{ false };
         std::deque<MaintQueueCb>   queue_;
         std::thread                thread_;
         std::condition_variable    queueCv_;
         std::mutex                 mutex_;

         std::shared_ptr<CcTrackerClient> trackerClient_;

         bool walletsRegistered_{};
         bool trackersStarted_{};

         // Cached CC outpoint maps from CC tracker.
         // Used to detect CC balance changes.
         std::map<std::string, std::map<BinaryData, std::set<unsigned>>> ccOutpointMapsFromTracker_;
         std::map<std::string, std::map<BinaryData, std::set<unsigned>>> ccOutpointMapsFromTrackerZc_;
         std::mutex ccOutpointMapsFromTrackerMutex_;

         ValidityFlag   validityFlag_;
      };

   }  //namespace sync
}  //namespace bs

#endif // BS_SYNC_WALLETS_MANAGER_H
