/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BLOCKCHAIN_ADAPTER_H
#define BLOCKCHAIN_ADAPTER_H

#include <atomic>
#include <future>
#include <unordered_map>
#include "Address.h"
#include "Message/Adapter.h"
#include "ArmoryConnection.h"

namespace spdlog {
   class logger;
}
namespace AsyncClient {
   class BtcWallet;
}
namespace BlockSettle {
   namespace Common {
      class ArmoryMessage_FeeLevelsRequest;
      class ArmoryMessage_GetOutpointsForAddrList;
      class ArmoryMessage_GetOutputsForOPs;
      class ArmoryMessage_GetSpentness;
      class ArmoryMessage_RegisterWallet;
      class ArmoryMessage_Settings;
      class ArmoryMessage_TXHashes;
      class ArmoryMessage_TXPushRequest;
      class ArmoryMessage_UTXOsForAddr;
      class ArmoryMessage_WalletIDs;
      class ArmoryMessage_WalletUnconfirmedTarget;
   }
}
class BitcoinFeeCache;

class TxWithHeight : public Tx
{
public:
   TxWithHeight(const Tx &tx) : Tx(tx), txHeight_(tx.getTxHeight()) {}
   TxWithHeight(const BinaryData &data) : Tx() { deserialize(data); }

   uint32_t getTxHeight() const override { return txHeight_; }
   BinaryData serialize() const;

private:
   void deserialize(const BinaryData &);

private:
   uint32_t txHeight_{ UINT32_MAX };
};


class BlockchainAdapter : public bs::message::Adapter, public ArmoryCallbackTarget
{
public:
   BlockchainAdapter(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::message::User> &
      , const std::shared_ptr<ArmoryConnection> &armory = nullptr);
   ~BlockchainAdapter() override;

   bool process(const bs::message::Envelope &) override;
   bool processBroadcast(const bs::message::Envelope&) override { return false; }

   std::set<std::shared_ptr<bs::message::User>> supportedReceivers() const override {
      return { user_ };
   }
   std::string name() const override { return "Blockchain"; }

   void setQueue(const std::shared_ptr<bs::message::QueueInterface> &) override;

protected:
   void onStateChanged(ArmoryState) override;
   void onNewBlock(unsigned int height, unsigned int branchHeight) override;
   void onRefresh(const std::vector<BinaryData> &, bool) override;
   void onZCInvalidated(const std::set<BinaryData> &ids) override;
   void onZCReceived(const std::string& requestId, const std::vector<bs::TXEntry> &) override;
   void onTxBroadcastError(const std::string& requestId, const BinaryData &txHash, int errCode
      , const std::string &errMsg) override;

   virtual void onBroadcastTimeout(const std::string &timeoutId);
   void sendBroadcastTimeout(const std::string &timeoutId);

   void reconnect();
   void start();
   void suspend();
   virtual void resumeRegistrations();

   void sendReady();
   void sendLoadingBC();
   void sendState(ArmoryState);
   bool processSettings(const BlockSettle::Common::ArmoryMessage_Settings &);

   bool processPushTxRequest(const bs::message::Envelope &
      , const BlockSettle::Common::ArmoryMessage_TXPushRequest &);
   bool processRegisterWallet(const bs::message::Envelope &
      , const BlockSettle::Common::ArmoryMessage_RegisterWallet &);
   bool processUnregisterWallets(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_WalletIDs&);
   bool processUnconfTarget(const bs::message::Envelope &
      , const BlockSettle::Common::ArmoryMessage_WalletUnconfirmedTarget &);
   bool processGetTxCount(const bs::message::Envelope &
      , const BlockSettle::Common::ArmoryMessage_WalletIDs &);
   bool processBalance(const bs::message::Envelope &
      , const BlockSettle::Common::ArmoryMessage_WalletIDs &);
   bool processGetTXsByHash(const bs::message::Envelope &
      , const BlockSettle::Common::ArmoryMessage_TXHashes &);
   bool processLedgerEntries(const bs::message::Envelope &, const std::string &filter);
   bool processLedgerUnsubscribe(const bs::message::Envelope &, const std::string &filter);
   bool processAddressHist(const bs::message::Envelope&, const std::string&);
   bool processFeeLevels(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_FeeLevelsRequest &);
   bool processGetUTXOs(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_WalletIDs&, bool zc = false, bool rbf = false);
   bool processUTXOsForAddr(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_UTXOsForAddr&);
   bool processGetOutpoints(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_GetOutpointsForAddrList&);
   bool processSpentnessRequest(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_GetSpentness&);
   bool processGetOutputsForOPs(const bs::message::Envelope&
      , const BlockSettle::Common::ArmoryMessage_GetOutputsForOPs&);
   bool processSubscribeAddressTX(const bs::message::Envelope&, const std::string& addr);
   void processZcForAddrSubscriptions(const bs::TXEntry&);

protected:
   std::shared_ptr<spdlog::logger>     logger_;
   std::shared_ptr<bs::message::User>  user_;
   std::shared_ptr<ArmoryConnection>   armoryPtr_;
   std::shared_ptr<BitcoinFeeCache>    feeEstimationsCache_;
   PRNG_Fortuna   fortuna_;

   struct Wallet {
      std::shared_ptr<AsyncClient::BtcWallet>   wallet;
      bool registered{ false };
      std::vector<BinaryData> addresses;
      bool asNew{ false };
   };
   std::unordered_map<std::string, Wallet>      wallets_;
   std::recursive_mutex mutex_;
   std::unordered_map<std::string, std::string> regMap_;
   std::unordered_map<std::string, bs::message::Envelope>   reqByRegId_;
   std::unordered_map<std::string, std::pair<std::string, bs::message::Envelope>>   unconfTgtMap_;

   bool walletsReady_{ false };
   std::atomic_bool registrationComplete_{ false };
   std::atomic_bool  suspended_{ true };
   std::shared_ptr<std::atomic_bool>   stopped_;
   std::unordered_map<std::string, std::set<BinaryData>> txHashByPushReqId_;
   std::map<bs::message::SeqId, bs::message::Envelope>   requestsPool_;
   std::mutex                                mtxReqPool_;

   std::shared_ptr< std::promise<bool>>   connKeyProm_;
   std::unordered_map<std::string, std::vector<std::shared_ptr<bs::message::User>>> ledgerSubscriptions_;

   struct AddressHistRequest
   {
      bs::message::Envelope   env;
      bs::Address    address;
      std::string    walletId;
   };
   std::unordered_map<std::string, AddressHistRequest>   addressSubscriptions_;

   struct PushTxData {
      bs::message::Envelope   env;
      std::string       pushId;
      bool              monitored{ false };
      std::vector<BinaryData> txs;
      bool              resultReported{ false };
   };
   std::unordered_map<std::string, PushTxData>   pendingTxMap_;
   std::set<BinaryData> pushedZCs_;
   std::unordered_set<std::string>  receivedZCs_;

   struct AddressSubscription {
      bs::message::SeqId                  msgId;
      std::shared_ptr<bs::message::User>  subscriber;
   };
   std::map<bs::Address, AddressSubscription>   addrTxSubscriptions_;

   struct Settings {
      std::string host;
      std::string port;
      std::string key;

      bool operator==(const Settings& other) const
      {
         return ((host == other.host) && (port == other.port) && (key == other.key));
      }
      bool operator!=(const Settings& other) const
      {
         return !operator==(other);
      }
   };
   Settings currentSettings_;

private:
   std::string registerWallet(const std::string &walletId, const Wallet &);
   bool unregisterWallet(const std::string& walletId);
   void singleAddrWalletRegistered(const AddressHistRequest &);
};


#endif	// BLOCKCHAIN_ADAPTER_H
