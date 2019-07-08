#ifndef __ARMORY_CONNECTION_H__
#define __ARMORY_CONNECTION_H__

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/logger.h>

#include "Address.h"
#include "AsyncClient.h"
#include "BtcDefinitions.h"
#include "BlockObj.h"

class ArmoryConnection;

enum class ArmoryState : uint8_t {
   Offline,
   Connecting,
   Cancelled,
   Connected,
   Scanning,
   Error,
   Closing,
   Ready
};

namespace bs {
   struct TXEntry {
      BinaryData  txHash;
      std::string walletId;
      int64_t     value;
      uint32_t    blockNum;
      uint32_t    txTime;
      bool        isRBF;
      bool        isChainedZC;
      bool        merged;
      std::chrono::time_point<std::chrono::steady_clock> recvTime;

      bool operator==(const TXEntry &other) const { return (txHash == other.txHash); }
      void merge(const TXEntry &);
      static TXEntry fromLedgerEntry(const ClientClasses::LedgerEntry &);
      static std::vector<TXEntry> fromLedgerEntries(const std::vector<ClientClasses::LedgerEntry> &);
   };
}

class ArmoryCallbackTarget
{
public:
   ArmoryCallbackTarget(ArmoryConnection *armory);
   virtual ~ArmoryCallbackTarget();

   // use empty methods in abstract class to avoid re-implementation in descendants
   // for more brevity if some of them are not needed
   virtual void onDestroy();
   virtual void onStateChanged(ArmoryState) {}
   virtual void onPrepareConnection(NetworkType, const std::string &host, const std::string &port) {}
   virtual void onRefresh(const std::vector<BinaryData> &, bool) {}
   virtual void onNewBlock(unsigned int) {}
   virtual void onZCReceived(const std::vector<bs::TXEntry> &) {}
   virtual void onZCInvalidated(const std::vector<bs::TXEntry> &) {}
   virtual void onLoadProgress(BDMPhase, float, unsigned int, unsigned int) {}
   virtual void onNodeStatus(NodeStatus, bool, RpcStatus) {}
   virtual void onError(const std::string &str, const std::string &extra) {}
   virtual void onTxBroadcastError(const std::string &, const std::string &) {}

   virtual void onCombinedBalances(const std::map<std::string, CombinedBalances> &) {}
   virtual void onCombinedTxnCounts(const std::map<std::string, CombinedCounts> &) {}
   virtual void onLedgerForAddress(const bs::Address &, const std::shared_ptr<AsyncClient::LedgerDelegate> &) {}

protected:
   ArmoryConnection  * armory_ = nullptr;
};

// The class is used as a callback that processes asynchronous Armory events.
class ArmoryCallback : public RemoteCallback
{
public:
   ArmoryCallback(ArmoryConnection *conn, const std::shared_ptr<spdlog::logger> &logger)
      : RemoteCallback(), connection_(conn), logger_(logger) {}
   virtual ~ArmoryCallback() noexcept override = default;

   void run(BDMAction action, void* ptr, int block = 0) override;
   void progress(BDMPhase phase,
      const std::vector<std::string> &walletIdVec,
      float progress, unsigned secondsRem,
      unsigned progressNumeric) override;

   void disconnected() override;

   void resetConnection() { connection_ = nullptr; }

private:
   ArmoryConnection * connection_ = nullptr;
   std::shared_ptr<spdlog::logger>  logger_;
};

// The abstracted connection between BS and Armory. When BS code needs to
// communicate with Armory, this class is what the code should use. Only one
// connection should exist at any given time.
class ArmoryConnection
{
   friend class ArmoryCallback;
public:
   ArmoryConnection(const std::shared_ptr<spdlog::logger> &);
   virtual ~ArmoryConnection() noexcept;

   ArmoryState state() const { return state_; }

   bool goOnline();

   bool broadcastZC(const BinaryData& rawTx);

   unsigned int topBlock() const { return topBlock_; }

   using RegisterWalletCb = std::function<void(const std::string &regId)>;
   using WalletsHistoryCb = std::function<void(const std::vector<ClientClasses::LedgerEntry>&)>;
   using LedgerDelegateCb = std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)>;
   using UTXOsCb = std::function<void(const std::vector<UTXO> &)>;

   virtual std::string registerWallet(const std::string &walletId
      , const std::vector<BinaryData> &addrVec, const RegisterWalletCb&
      , bool asNew = false);
   virtual bool getWalletsHistory(const std::vector<std::string> &walletIDs, const WalletsHistoryCb&);
   virtual bool getCombinedBalances(const std::vector<std::string> &walletIDs);  // will return result to ACT
   virtual bool getCombinedTxNs(const std::vector<std::string> &walletIDs);      // will return result to ACT

   // If context is not null and cbInMainThread is true then the callback will be called
   // on main thread only if context is still alive.
   bool getLedgerDelegateForAddress(const std::string &walletId, const bs::Address &); // result to ACT
   virtual bool getWalletsLedgerDelegate(const LedgerDelegateCb &);

   bool getSpendableTxOutListForValue(const std::vector<std::string> &walletIds, uint64_t
      , const UTXOsCb &);
   bool getSpendableZCoutputs(const std::vector<std::string> &walletIds, const UTXOsCb &);
   bool getRBFoutputs(const std::vector<std::string> &walletIds, const UTXOsCb &);

   using TxCb = std::function<void(const Tx&)>;
   using TXsCb = std::function<void(const std::vector<Tx>&)>;

   using BinaryDataCb = std::function<void(const BinaryData&)>;

   virtual bool getTxByHash(const BinaryData &hash, const TxCb&);
   virtual bool getTXsByHash(const std::set<BinaryData> &hashes, const TXsCb &);
   virtual bool getRawHeaderForTxHash(const BinaryData& inHash, const BinaryDataCb &);
   virtual bool getHeaderByHeight(const unsigned int inHeight, const BinaryDataCb &);

   using FloatCb = std::function<void(float)>;
   using FloatMapCb = std::function<void(const std::map<unsigned int, float> &)>;

   virtual bool estimateFee(unsigned int nbBlocks, const FloatCb &);
   virtual bool getFeeSchedule(const FloatMapCb&);

   bool isTransactionVerified(const ClientClasses::LedgerEntry &) const;
   bool isTransactionVerified(uint32_t blockNum) const;
   bool isTransactionConfirmed(const ClientClasses::LedgerEntry &) const;
   unsigned int getConfirmationsNumber(const ClientClasses::LedgerEntry &item) const;
   unsigned int getConfirmationsNumber(uint32_t blockNum) const;

   bool isOnline() const { return isOnline_; }

   void setState(ArmoryState);
   std::atomic_bool  needsBreakConnectionLoop_ {false};

   bool addTarget(ArmoryCallbackTarget *);
   bool removeTarget(ArmoryCallbackTarget *);

   using BIP151Cb = std::function<bool(const BinaryData&, const std::string&)>;

   std::shared_ptr<AsyncClient::BtcWallet> instantiateWallet(const std::string &walletId);

protected:
   void setupConnection(NetworkType, const std::string &host, const std::string &port
      , const std::string &dataDir, const BinaryData &serverKey
      , const BIP151Cb &cbBIP151);

   using CallbackQueueCb = std::function<void(ArmoryCallbackTarget *)>;
   void addToMaintQueue(const CallbackQueueCb &);

private:
   void registerBDV(NetworkType);
   void setTopBlock(unsigned int topBlock) { topBlock_ = topBlock; }
   void onRefresh(const std::vector<BinaryData> &);
   void onZCsReceived(const std::vector<ClientClasses::LedgerEntry> &);
   void onZCsInvalidated(const std::set<BinaryData> &);

   void stopServiceThreads();

   bool addGetTxCallback(const BinaryData &hash, const TxCb &);  // returns true if hash exists
   void callGetTxCallbacks(const BinaryData &hash, const Tx &);

   void processDelayedZC();
   void maintenanceThreadFunc();

protected:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<AsyncClient::BlockDataViewer>   bdv_;
   std::shared_ptr<ArmoryCallback>  cbRemote_;
   std::atomic<ArmoryState>         state_ { ArmoryState::Offline };
   std::atomic_uint                 topBlock_ { 0 };
   std::shared_ptr<BlockHeader>     getTxBlockHeader_;

   std::vector<SecureBinaryData> bsBIP150PubKeys_;

   std::atomic_bool  regThreadRunning_{ false };
   std::atomic_bool  connThreadRunning_{ false };
   std::atomic_bool  maintThreadRunning_{ false };

   std::atomic_bool              isOnline_;
   std::unordered_map<
      std::string, std::function<void(const std::string &)>> registrationCallbacks_;
   std::mutex registrationCallbacksMutex_;

   std::mutex  cbMutex_;
   std::map<BinaryData, std::vector<TxCb>>   txCallbacks_;

   std::map<BinaryData, bs::TXEntry>   zcNotifiedEntries_;
   std::map<BinaryData, bs::TXEntry>   zcWaitingEntries_;
   std::mutex                 zcMutex_;

   std::unordered_set<ArmoryCallbackTarget *>   activeTargets_;
   std::atomic_bool  actChanged_{ false };

   std::thread    regThread_;
   std::mutex     regMutex_;
   std::condition_variable regCV_;

   std::deque<CallbackQueueCb>   actQueue_;
   std::thread    maintThread_;
   std::condition_variable actCV_;
   std::mutex              actMutex_;
};

#endif // __ARMORY_CONNECTION_H__
