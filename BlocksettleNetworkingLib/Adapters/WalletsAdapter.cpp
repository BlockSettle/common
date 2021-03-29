/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WalletsAdapter.h"
#include <spdlog/spdlog.h>
#include "CoinSelection.h"
#include "ProtobufHeadlessUtils.h"
#include "ScriptRecipient.h"
#include "SignerClient.h"
#include "TradesUtils.h"
#include "UtxoReservation.h"
#include "Wallets/SyncHDWallet.h"
#include "WalletUtils.h"

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;
using namespace bs::sync;


WalletsAdapter::WalletsAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &ownUser
   , std::unique_ptr<SignerClient> signerClient
   , const std::shared_ptr<bs::message::User> &blockchainUser)
   : logger_(logger), ownUser_(ownUser), signerClient_(std::move(signerClient))
   , blockchainUser_(blockchainUser)
{
   utxoResMgr_ = std::make_shared<bs::UtxoReservation>(logger);
   signerClient_->setClientUser(ownUser_);
   signerClient_->setSignerReady([this] {
      sendLoadingBC();
   });
   signerClient_->setWalletsLoaded([this] {
      startWalletsSync();
   });
   signerClient_->setNoWalletsFound([this] {
      logger_->debug("[WalletsAdapter] no wallets found");
   });
   signerClient_->setWalletsListUpdated([this] { reset(); });
   signerClient_->setAuthLeafAdded([this](const std::string& walletId) {
      authLeafAdded(walletId);
   });
}

WalletsAdapter::~WalletsAdapter()
{
   utxoResMgr_->shutdownCheck();
   stop();
}

bool WalletsAdapter::processEnvelope(const Envelope &env)
{
   if (signerClient_->isSignerUser(env.sender)) {
      return signerClient_->process(env);
   }
   else if (env.sender->value() == blockchainUser_->value()) {
      return processBlockchain(env);
   }
   else if (env.receiver && (env.receiver->value() == ownUser_->value())) {
      return processOwnRequest(env);
   }
   return true;
}

bool WalletsAdapter::processBlockchain(const Envelope &env)
{
   if (!env.receiver && env.request) {
      return true;
   }
   ArmoryMessage msg;
   if (!msg.ParseFromString(env.message)) {
      logger_->error("[{}] failed to parse msg #{}", __func__, env.id);
      return true;
   }
   switch (msg.data_case()) {
   case ArmoryMessage::kStateChanged:
      topBlock_ = msg.state_changed().top_block();
      break;
   case ArmoryMessage::kNewBlock:
      topBlock_ = msg.new_block().top_block();
      break;
   case ArmoryMessage::kZcReceived:
      processZCReceived(msg.zc_received());
      break;
   case ArmoryMessage::kWalletRegistered:
      if (msg.wallet_registered().success() && !msg.wallet_registered().wallet_id().empty()) {
         processWalletRegistered(msg.wallet_registered().wallet_id());
      }
      else {
         sendWalletError(msg.wallet_registered().wallet_id(), "registration failed");
      }
      break;
   case ArmoryMessage::kUnconfTargetSet:
      processUnconfTgtSet(msg.unconf_target_set());
      break;
   case ArmoryMessage::kAddrTxnResponse:
      processAddrTxN(msg.addr_txn_response());
      break;
   case ArmoryMessage::kWalletBalanceResponse:
      processWalletBal(msg.wallet_balance_response());
      break;
   case ArmoryMessage::kTransactions:
      processTransactions(env.id, msg.transactions());
      break;
   case ArmoryMessage::kUtxos:
      return processUTXOs(env.id, msg.utxos());
   default: break;
   }
   return true;
}

void WalletsAdapter::sendLoadingBC()
{
   WalletsMessage msg;
   msg.mutable_loading();
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::startWalletsSync()
{
   const auto &cbWalletsReceived = [this](const std::vector<bs::sync::WalletInfo> &wallets)
   {
      std::vector<std::shared_ptr<hd::Wallet>> deletedWallets;
      for (const auto& hdWallet : prevHdWallets_) {
         const auto& itWallet = std::find_if(wallets.cbegin(), wallets.cend()
            , [walletId=hdWallet->walletId()](const bs::sync::WalletInfo &wi) {
            if (wi.format == bs::sync::WalletFormat::HD) {
               return (std::find(wi.ids.cbegin(), wi.ids.cend(), walletId) != wi.ids.cend());
            }
            return false;
         });
         if (itWallet == wallets.end()) {
            logger_->debug("[WalletsAdapter::startWalletsSync] {} deleted", hdWallet->walletId());
            deletedWallets.push_back(hdWallet);
         }
      }
      for (const auto& hdWallet : deletedWallets) {
         eraseWallet(hdWallet);
      }

      for (const auto &wallet : wallets) {
         loadingWallets_.insert(*wallet.ids.cbegin());
      }
      for (const auto &wallet : wallets) {
         loadWallet(wallet);
      }
   };
   signerClient_->syncWalletInfo(cbWalletsReceived);
}

void WalletsAdapter::loadWallet(const bs::sync::WalletInfo &info)
{
   logger_->debug("[WalletsManager::syncWallets] syncing wallet {} ({} {})"
      , *info.ids.cbegin(), info.name, info.description);

   switch (info.format) {
   case bs::sync::WalletFormat::HD:
   {
      try {
         const auto hdWallet = std::make_shared<hd::Wallet>(info, signerClient_.get(), logger_);
         hdWallet->setWCT(this);

         if (hdWallet) {
            const auto &cbHDWalletDone = [this, hdWallet]
            {
               logger_->debug("[WalletsAdapter::loadWallet] synced HD wallet {}"
                  , hdWallet->walletId());
               saveWallet(hdWallet);

               const auto &wi = bs::sync::WalletInfo::fromWallet(hdWallet);
               WalletsMessage msg;
               auto msgWallet = msg.mutable_wallet_loaded();
               wi.toCommonMsg(*msgWallet);
               Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
               pushFill(env);

               loadingWallets_.erase(hdWallet->walletId());
               if (loadingWallets_.empty()) {
                  ArmoryMessage msg;
                  auto msgReq = msg.mutable_register_wallet();
                  msgReq->set_wallet_id("");
                  Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
                  pushFill(env);
               }
            };
            hdWallet->synchronize(cbHDWalletDone);
         }
      } catch (const std::exception &e) {
         logger_->error("[WalletsAdapter::loadWallet] failed to create HD wallet "
            "{}: {}", *info.ids.cbegin(), e.what());
      }
      break;
   }

   case bs::sync::WalletFormat::Settlement:
      throw std::runtime_error("not supported");
      break;

   default:
      logger_->warn("[WalletsAdapter::loadWallet] wallet format {} is not "
         "supported yet", (int)info.format);
      break;
   }
}

std::shared_ptr<hd::Wallet> WalletsAdapter::getHDWalletById(const std::string& walletId) const
{
   auto it = std::find_if(hdWallets_.cbegin(), hdWallets_.cend()
      , [walletId](const std::shared_ptr<hd::Wallet> &hdWallet) {
      return (hdWallet->walletId() == walletId);
   });
   if (it != hdWallets_.end()) {
      return *it;
   }
   return nullptr;
}

std::shared_ptr<Wallet> WalletsAdapter::getWalletById(const std::string &walletId) const
{
   for (const auto &wallet : wallets_) {
      if (wallet.second->hasId(walletId)) {
         return wallet.second;
      }
   }
   return nullptr;
}

std::shared_ptr<Wallet> WalletsAdapter::getWalletByAddress(const bs::Address &address) const
{
   for (const auto &wallet : wallets_) {
      if (wallet.second && (wallet.second->containsAddress(address)
         || wallet.second->containsHiddenAddress(address))) {
         return wallet.second;
      }
   }
   return nullptr;
}

std::shared_ptr<hd::Group> WalletsAdapter::getGroupByWalletId(const std::string &walletId) const
{
   const auto itGroup = groupsByWalletId_.find(walletId);
   if (itGroup == groupsByWalletId_.end()) {
      const auto hdWallet = getHDRootForLeaf(walletId);
      if (hdWallet) {
         for (const auto &group : hdWallet->getGroups()) {
            for (const auto &leaf : group->getLeaves()) {
               if (leaf->hasId(walletId)) {
                  groupsByWalletId_[walletId] = group;
                  return group;
               }
            }
         }
      }
      groupsByWalletId_[walletId] = nullptr;
      return nullptr;
   }
   return itGroup->second;
}

std::shared_ptr<hd::Wallet> WalletsAdapter::getHDRootForLeaf(const std::string &walletId) const
{
   for (const auto &hdWallet : hdWallets_) {
      for (const auto &leaf : hdWallet->getLeaves()) {
         if (leaf->hasId(walletId)) {
            return hdWallet;
         }
      }
   }
   return nullptr;
}

std::shared_ptr<bs::sync::hd::Wallet> WalletsAdapter::getPrimaryWallet() const
{
   for (const auto& hdWallet : hdWallets_) {
      if (hdWallet->isPrimary()) {
         return hdWallet;
      }
   }
   return nullptr;
}

void WalletsAdapter::eraseWallet(const std::shared_ptr<hd::Wallet>& hdWallet)
{
   ArmoryMessage msg;
   auto msgReq = msg.mutable_unregister_wallets();
   const auto& leaves = hdWallet->getLeaves();
   for (const auto& leaf : leaves) {
      for (const auto& id : leaf->internalIds()) {
         msgReq->add_wallet_ids(id);
      }
      eraseWallet(leaf, false);
   }
   Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   pushFill(env);
   const auto& wi = bs::sync::WalletInfo::fromWallet(hdWallet);
   WalletsMessage msgWlt;
   wi.toCommonMsg(*msgWlt.mutable_wallet_deleted());
   env = Envelope{ 0, ownUser_, nullptr, {}, {}, msgWlt.SerializeAsString() };
   pushFill(env);
   std::remove_if(hdWallets_.begin(), hdWallets_.end()
      , [hdWallet](const std::shared_ptr<hd::Wallet>& w) {
         return (hdWallet->walletId() == w->walletId());
      });
}

void WalletsAdapter::eraseWallet(const std::shared_ptr<Wallet> &wallet, bool unregister)
{
   if (!wallet) {
      return;
   }
   if (unregister) {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_unregister_wallets();
      for (const auto& walletId : wallet->internalIds()) {
         msgReq->add_wallet_ids(walletId);
      }
      Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      pushFill(env);
   }
   wallets_.erase(wallet->walletId());
}

bool WalletsAdapter::isAddressUsed(const bs::Address& addr, const std::string& walletId) const
{
   const auto& hasTxNs = [this, addr](const std::map<BinaryData, uint64_t>& txnMap) -> bool
   {
      const auto& itTxN = txnMap.find(addr.id());
      if (itTxN != txnMap.end()) {
         return (itTxN->second != 0);
      }
      return false;
   };

   if (walletId.empty()) {
      for (const auto& bal : walletBalances_) {
         if (hasTxNs(bal.second.addressTxNMap)) {
            return true;
         }
      }
   }
   else {
      const auto& itBal = walletBalances_.find(walletId);
      if (itBal != walletBalances_.end()) {
         return hasTxNs(itBal->second.addressTxNMap);
      }
   }
   return false;
}

void WalletsAdapter::saveWallet(const std::shared_ptr<bs::sync::hd::Wallet> &wallet)
{
   if (!userId_.empty()) {
      wallet->setUserId(userId_);
   }
   const auto existingHdWallet = getHDWalletById(wallet->walletId());

   if (existingHdWallet) {    // merge if HD wallet already exists
      existingHdWallet->merge(*wallet);
   } else {
      hdWallets_.push_back(wallet);
   }

   for (const auto &leaf : wallet->getLeaves()) {
      addWallet(leaf);
   }
}

void WalletsAdapter::addWallet(const std::shared_ptr<Wallet> &wallet)
{
   auto ccLeaf = std::dynamic_pointer_cast<bs::sync::hd::CCLeaf>(wallet);
   if (ccLeaf) {
//      ccLeaf->setCCDataResolver(ccResolver_);
//      updateTracker(ccLeaf);   //TODO: should be request to OnChainTrackerClient
   }
   wallet->setUserId(userId_);

   const auto &itWallet = wallets_.find(wallet->walletId());
   if (itWallet != wallets_.end()) {
      itWallet->second->merge(wallet);
   } else {
      wallets_[wallet->walletId()] = wallet;
   }

   if ((wallet->type() == bs::core::wallet::Type::Authentication)) {
      authAddressWallet_ = wallet;
      logger_->debug("[WalletsAdapter::addWallet] auth leaf {} created", wallet->walletId());
      WalletsMessage msg;
      msg.set_auth_leaf_created(authAddressWallet_->walletId());
      Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
      pushFill(env);
   }
   registerWallet(wallet);
}

void WalletsAdapter::registerWallet(const std::shared_ptr<Wallet> &wallet)
{
   const auto &regData = wallet->regData();
   auto &pendingReg = pendingRegistrations_[wallet->walletId()];
   for (const auto &reg : regData) {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_register_wallet();
      msgReq->set_wallet_id(reg.first);
      pendingReg.insert(reg.first);
      msgReq->set_as_new(false);
      for (const auto &addr : reg.second) {
         msgReq->add_addresses(addr.toBinStr());
      }
      Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      pushFill(env);
   }
}

void WalletsAdapter::scanWallet(const std::shared_ptr<Wallet>& wallet, bool isExt)
{
   const auto& leaf = std::dynamic_pointer_cast<bs::sync::hd::Leaf>(wallet);
   if (!leaf) {
      logger_->error("[{}] can't scan non-HD leaves ({})", __func__, wallet->walletId());
      return;
   }

   const auto& walletId = isExt ? leaf->walletScanId() : leaf->walletScanIdInt();

   const auto& cbExtAddrChain = [this, leaf, isExt, walletId]
      (const std::vector<std::pair<bs::Address, std::string>>&addrVec)
   {
      auto& curScanBatch = activeScanAddrs_[walletId];
      curScanBatch.clear();
      ArmoryMessage msg;
      auto msgReq = msg.mutable_register_wallet();
      msgReq->set_wallet_id(walletId);
      msgReq->set_as_new(false);
      std::set<std::string> indices;
      for (auto& addrPair : addrVec) {
         const auto& addr = addrPair.first.prefixed();
         msgReq->add_addresses(addr.toBinStr());
         curScanBatch.insert(addr);
         indices.insert(addrPair.second);
      }
      Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      pushFill(env);
      logger_->debug("[WalletsAdapter::scanWallet] {}: {} addresses from {} to {}"
         , walletId, curScanBatch.size(), *indices.cbegin(), *indices.rbegin());
   };
   const auto& itScan = activeScanAddrs_.find(walletId);
   if (itScan == activeScanAddrs_.end()) {   // first invocation
      if (!wallet->getUsedAddressCount()) {
         std::vector<std::pair<bs::Address, std::string>> addrVec;
         for (const auto& pooledAddr : wallet->getAddressPool()) {
            const bool isAddrExt = (pooledAddr.second[0] == '0');
            if (isExt == isAddrExt) {
               addrVec.push_back(pooledAddr);
            }
         }
         cbExtAddrChain(addrVec);
      }
      else {   // existing wallet - no need to scan
         return;
      }
   }
   else {
      signerClient_->extendAddressChain(wallet->walletId(), isExt ?
         leaf->extAddressPoolSize() : leaf->intAddressPoolSize(), isExt, cbExtAddrChain);
   }
}

void WalletsAdapter::processScanRegistered(const std::shared_ptr<bs::sync::Wallet>&
   , const std::string &scanId)
{
   ArmoryMessage msg;
   auto msgReq = msg.mutable_addr_txn_request();
   msgReq->add_wallet_ids(scanId);
   Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   pushFill(env);
}

void WalletsAdapter::resumeScan(const std::shared_ptr<bs::sync::Wallet>& wallet
   , const std::string& scanId, const ArmoryMessage_AddressTxNsResponse& countMap)
{
   const auto& leaf = std::dynamic_pointer_cast<bs::sync::hd::Leaf>(wallet);
   if (!leaf) {
      logger_->error("[{}] can't scan non-HD leaves ({})", __func__, wallet->walletId());
      return;
   }
   if (countMap.wallet_txns_size() != 1) {
      logger_->error("[{}] invalid countMap size: {} for {}", __func__
         , countMap.wallet_txns_size(), scanId);
      return;
   }

   const auto& itActiveAddrs = activeScanAddrs_.find(scanId);
   if (itActiveAddrs == activeScanAddrs_.end()) {
      logger_->error("[{}] {} is not in progress", __func__, scanId);
      return;
   }

   const auto& stopScan = [this, scanId]
   {
      logger_->debug("[WalletsAdapter::resumeScan] {} complete", scanId);
      activeScanAddrs_.erase(scanId);
      ArmoryMessage msg;
      auto msgReq = msg.mutable_unregister_wallets();
      msgReq->add_wallet_ids(scanId);
      Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      pushFill(env);
   };
   if (!countMap.wallet_txns(0).txns_size()) {
      stopScan();
   }
   else {
      std::set<BinaryData> activeAddrs;
      for (const auto& activeAddr : countMap.wallet_txns(0).txns()) {
         activeAddrs.insert(BinaryData::fromString(activeAddr.address()));
      }
      const bool isFullBatch = (activeAddrs.size() > (itActiveAddrs->second.size() / 5)); //FIXME if needed
      const auto& cbSyncAddrs = [this, leaf, scanId, isFullBatch, stopScan]
         (bs::sync::SyncState state)
      {
         if (!isFullBatch) {  // no more scans will follow
            stopScan();
         }
         if (state != bs::sync::SyncState::Success) {
            return;
         }
         leaf->synchronize([this, leaf, scanId, isFullBatch] {
            if (isFullBatch) {
               scanWallet(leaf, (leaf->walletScanId() == scanId));
            }
            registerWallet(leaf);
         });
      };
      logger_->debug("[{}] adding {} new addresses to {}", __func__
         , activeAddrs.size(), wallet->walletId());
      signerClient_->syncAddressBatch(wallet->walletId(), activeAddrs, cbSyncAddrs);
   }
}

void WalletsAdapter::registerWallet(const std::shared_ptr<bs::sync::hd::Wallet> &hdWallet)
{
   for (const auto &leaf : hdWallet->getLeaves()) {
      //settlement leaves are not registered
      if (leaf->type() == bs::core::wallet::Type::Settlement) {
         continue;
      }
      registerWallet(leaf);
   }
}

void WalletsAdapter::reset()
{
   std::unique_lock<std::mutex> lock(mtx_);
   userId_.clear();
   prevHdWallets_.swap(hdWallets_);
   hdWallets_.clear();
   wallets_.clear();
   walletNames_.clear();
   readyWallets_.clear();
   authAddressWallet_.reset();
   startWalletsSync();
}

void WalletsAdapter::balanceUpdated(const std::string &walletId)
{
   WalletsMessage msg;
   msg.set_balance_updated(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::sendWalletChanged(const std::string &walletId)
{
   WalletsMessage msg;
   msg.set_wallet_changed(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::sendWalletReady(const std::string &walletId)
{
   const auto& wallet = getWalletById(walletId);
   if (wallet) {
      pendingRegistrations_.erase(wallet->walletId());
      for (const auto& wltId : wallet->internalIds()) {
         readyWallets_.insert(wltId);
      }
   }
   else {
      readyWallets_.insert(walletId);
   }
   WalletsMessage msg;
   msg.set_wallet_ready(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::sendWalletError(const std::string &walletId
   , const std::string &errMsg)
{
   WalletsMessage msg;
   auto msgError = msg.mutable_error();
   msgError->set_wallet_id(walletId);
   msgError->set_error_message(errMsg);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::authLeafAdded(const std::string& walletId)
{
   const auto &priWallet = getPrimaryWallet();
   if (!priWallet) {
      logger_->error("[{}] can't find primary wallet", __func__);
      return;
   }
   std::shared_ptr<bs::sync::hd::Group> authGroup = priWallet->getGroup(bs::hd::CoinType::BlockSettle_Auth);
   if (!authGroup) {
      authGroup = std::make_shared<bs::sync::hd::AuthGroup>("Authentication"
         , "", signerClient_.get(), this, logger_);
      priWallet->addGroup(authGroup);
   }
   const auto &authLeaf = std::make_shared<hd::AuthLeaf>(walletId, "Authentication"
      , "", signerClient_.get(), logger_);
   authGroup->addLeaf(authLeaf);
   authAddressWallet_ = authLeaf;
   priWallet->synchronize([this, walletId, priWallet] {
      saveWallet(priWallet);
      WalletsMessage msg;
      auto msgAuth = msg.mutable_auth_wallet();
      msgAuth->set_wallet_id(walletId);
      for (const auto& addr : authAddressWallet_->getUsedAddressList()) {
         auto msgAddr = msgAuth->add_used_addresses();
         msgAddr->set_address(addr.display());
         msgAddr->set_index(authAddressWallet_->getAddressIndex(addr));
         msgAddr->set_comment(authAddressWallet_->getAddressComment(addr));
      }
      Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
      pushFill(env);
   });
}

void WalletsAdapter::addressAdded(const std::string &walletId)
{
   sendWalletChanged(walletId);
}

void WalletsAdapter::metadataChanged(const std::string &walletId)
{
   WalletsMessage msg;
   msg.set_wallet_meta_changed(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::processWalletRegistered(const std::string &walletId)
{
   for (const auto &wallet : wallets_) {
      if (wallet.second->hasScanId(walletId)) {
         processScanRegistered(wallet.second, walletId);
         return;
      }
      if (!wallet.second->hasId(walletId)) {
         continue;
      }
      auto &pendingReg = pendingRegistrations_[wallet.first];
      pendingReg.erase(walletId);
      if (!pendingReg.empty()) {
         break;
      }
      pendingRegistrations_.erase(wallet.first);
      wallet.second->onRegistered();

      const auto &unconfTgts = wallet.second->unconfTargets();
      const auto &itUnconfTgt = unconfTgts.find(walletId);
      if (!balanceEnabled() || (itUnconfTgt == unconfTgts.end())
         || (wallet.second->type() == bs::core::wallet::Type::ColorCoin)) {
         sendWalletReady(wallet.second->walletId());
      }
      else {
         ArmoryMessage msg;
         auto msgUnconfTgt = msg.mutable_set_unconf_target();
         msgUnconfTgt->set_wallet_id(wallet.second->walletId());
         msgUnconfTgt->set_conf_count(itUnconfTgt->second);
         Envelope env{ 0, ownUser_, blockchainUser_, {}, {}
            , msg.SerializeAsString(), true };
         pushFill(env);
      }
      break;
   }
}

void WalletsAdapter::processUnconfTgtSet(const std::string &walletId)
{
   const auto &itWallet = wallets_.find(walletId);
   if (itWallet == wallets_.end()) {
      return;
   }
   auto &pendingReg = pendingRegistrations_[itWallet->second->walletId()];
   for (const auto &id : itWallet->second->internalIds()) {
      pendingReg.insert(id + ".bal");
      pendingReg.insert(id + ".txn");
      sendTxNRequest(id);
      sendBalanceRequest(id);
   }
}

void WalletsAdapter::processAddrTxN(const ArmoryMessage_AddressTxNsResponse &response)
{
   for (const auto &byWallet : response.wallet_txns()) {
      bool isScan = false;
      for (const auto& wallet : wallets_) {
         if (wallet.second->hasScanId(byWallet.wallet_id())) {
            resumeScan(wallet.second, byWallet.wallet_id(), response);
            isScan = true;
            break;
         }
      }
      if (isScan) {
         continue;
      }
      auto &balanceData = walletBalances_[byWallet.wallet_id()];
      balanceData.addrTxNUpdated = true;
      for (const auto &txn : byWallet.txns()) {
         balanceData.addressTxNMap[BinaryData::fromString(txn.address())] = txn.txn();
      }

      const auto& wallet = getWalletById(byWallet.wallet_id());
      if (!wallet) {
         logger_->error("[{}] unknown wallet id: {}", __func__, byWallet.wallet_id());
         continue;
      }
      auto& pendingReg = pendingRegistrations_[wallet->walletId()];
      pendingReg.erase(byWallet.wallet_id() + ".txn");
      if (balanceData.addrBalanceUpdated) {
         if (trackLiveAddresses()) {
            pendingReg.insert(byWallet.wallet_id() + ".tar");
            sendTrackAddrRequest(byWallet.wallet_id());
         }
         else {
            sendWalletReady(wallet->walletId());
         }
      }
   }
}

void WalletsAdapter::processWalletBal(const ArmoryMessage_WalletBalanceResponse &response)
{
   for (const auto &byWallet : response.balances()) {
      auto &balanceData = walletBalances_[byWallet.wallet_id()];
      balanceData.walletBalance.totalBalance = byWallet.full_balance();
      balanceData.walletBalance.unconfirmedBalance = byWallet.unconfirmed_balance();
//      balanceData.walletBalance.spendableBalance = byWallet.spendable_balance();
      balanceData.walletBalance.spendableBalance = balanceData.walletBalance.totalBalance
         - balanceData.walletBalance.unconfirmedBalance;
      balanceData.addrCount = byWallet.address_count();
      balanceData.addrBalanceUpdated = true;
      for (const auto &addrBal : byWallet.addr_balances()) {
         auto &addrBalance = balanceData.addressBalanceMap[BinaryData::fromString(addrBal.address())];
         addrBalance.totalBalance = addrBal.full_balance();
         addrBalance.spendableBalance = addrBal.spendable_balance();
         addrBalance.unconfirmedBalance = addrBal.unconfirmed_balance();
      }

      const auto& wallet = getWalletById(byWallet.wallet_id());
      if (!wallet) {
         logger_->error("[{}] unknown wallet id: {}", __func__, byWallet.wallet_id());
         continue;
      }
      auto& pendingReg = pendingRegistrations_[wallet->walletId()];
      pendingReg.erase(byWallet.wallet_id() + ".bal");

      if (balanceData.addrTxNUpdated) {
         if (trackLiveAddresses()) {
            const auto &itReg = pendingReg.find(byWallet.wallet_id() + ".tar");
            if (itReg != pendingReg.end()) {
               pendingReg.erase(itReg);
               sendWalletReady(wallet->walletId());
            }
            else {
               pendingReg.insert(byWallet.wallet_id() + ".tar");
               sendTrackAddrRequest(byWallet.wallet_id());
            }
         }
         else {
            sendWalletReady(wallet->walletId());
         }
      }
   }
}

void WalletsAdapter::sendTrackAddrRequest(const std::string &walletId)
{
   const auto& balanceData = walletBalances_[walletId];
   if (!balanceData.addrTxNUpdated || !balanceData.addrBalanceUpdated) {
      return;  // wait for both requests to complete first
   }
   const auto& wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] can't find wallet for {}", __func__, walletId);
      return;
   }
   std::set<BinaryData> usedAddrSet;
   for (const auto& addrPair : balanceData.addressTxNMap) {
      if (addrPair.second != 0) {
         usedAddrSet.insert(addrPair.first);
      }
   }
   for (const auto& addrPair : balanceData.addressBalanceMap) {
      if (usedAddrSet.find(addrPair.first) != usedAddrSet.end()) {
         continue;   // skip already added addresses
      }
      if (addrPair.second.totalBalance) {
         usedAddrSet.insert(addrPair.first);
      }
   }

   std::set<BinaryData> usedAndRegAddrs;
   const auto& regAddresses = wallet->allAddresses();
   std::set_intersection(regAddresses.cbegin(), regAddresses.cend()
      , usedAddrSet.cbegin(), usedAddrSet.cend()
      , std::inserter(usedAndRegAddrs, usedAndRegAddrs.end()));

   const auto &cb = [this, walletId, usedAndRegAddrs](bs::sync::SyncState st)
   {
      walletBalances_[walletId].addrTxNUpdated = true;
      const auto& wallet = getWalletById(walletId);
      if (!wallet) {
         logger_->error("[WalletsAdapter::sendTrackAddrRequest] unknown wallet {}", walletId);
         return;
      }
      const bool isExt = (wallet->walletId() == walletId);
      if (st == bs::sync::SyncState::Success) {
         const auto &cbSync = [this, wallet, walletId]
         {
            pendingRegistrations_[wallet->walletId()].insert(walletId + ".bal");
            sendBalanceRequest(walletId);
            sendWalletReady(walletId);
            //TODO: top up if needed
         };
         wallet->synchronize(cbSync);
      }
      else {
         pendingRegistrations_[wallet->walletId()].erase(walletId + ".tar");
         sendWalletReady(wallet->walletId());
      }
      if (st != bs::sync::SyncState::Failure) {
         scanWallet(wallet, isExt);
      }
   };
   signerClient_->syncAddressBatch(wallet->walletId(), usedAndRegAddrs, cb);
}

void WalletsAdapter::sendTxNRequest(const std::string &walletId)
{
   walletBalances_[walletId].addrTxNUpdated = false;
   ArmoryMessage msg;
   auto msgReq = msg.mutable_addr_txn_request();
   msgReq->add_wallet_ids(walletId);
   Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   pushFill(env);
}

void WalletsAdapter::sendBalanceRequest(const std::string &walletId)
{
   walletBalances_[walletId].addrTxNUpdated = false;
   ArmoryMessage msg;
   auto msgReq = msg.mutable_wallet_balance_request();
   msgReq->add_wallet_ids(walletId);
   Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   pushFill(env);
}

void WalletsAdapter::scanComplete(const std::string &walletId)
{
   logger_->debug("[{}] - HD wallet {} imported", __func__, walletId);
   const auto hdWallet = getHDWalletById(walletId);
   if (hdWallet) {
      registerWallet(hdWallet);
   }
   sendWalletChanged(walletId);
//   emit walletImportFinished(walletId);
}

void WalletsAdapter::walletReset(const std::string &walletId)
{
   sendWalletChanged(walletId);
}

void WalletsAdapter::walletCreated(const std::string &walletId)
{
   for (const auto &hdWallet : hdWallets_) {
      const auto leaf = hdWallet->getLeaf(walletId);
      if (leaf == nullptr) {
         continue;
      }
      logger_->debug("[WalletsAdapter::walletCreated] HD leaf {} ({}) added"
         , walletId, leaf->name());

      addWallet(leaf);
      sendWalletChanged(walletId);
      break;
   }
}

void WalletsAdapter::walletDestroyed(const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   eraseWallet(wallet);
   sendWalletChanged(walletId);
}

void WalletsAdapter::processZCReceived(const ArmoryMessage_ZCReceived &event)
{
   //TODO: update TX counts for participating addresses

   std::unordered_set<std::string> participatingWalletIds;
   for (const auto &entry : event.tx_entries()) {
      for (const auto &walletId : entry.wallet_ids()) {
         participatingWalletIds.insert(walletId);
      }
   }
   for (const auto &walletId : participatingWalletIds) {
      sendBalanceRequest(walletId);
   }
}

bool WalletsAdapter::processOwnRequest(const bs::message::Envelope &env)
{
   WalletsMessage msg;
   if (!msg.ParseFromString(env.message)) {
      logger_->error("[{}] failed to parse msg #{}", __func__, env.id);
      return true;
   }
   switch (msg.data_case()) {
   case WalletsMessage::kSetSettlementFee:
      settlementFee_ = msg.set_settlement_fee();
      break;
   case WalletsMessage::kHdWalletGet:
      return processHdWalletGet(env, msg.hd_wallet_get());
   case WalletsMessage::kWalletGet:
      return processWalletGet(env, msg.wallet_get());
   case WalletsMessage::kWalletsListRequest:
      return processWalletsList(env, msg.wallets_list_request());
   case WalletsMessage::kTxCommentGet:
      return processGetTxComment(env, msg.tx_comment_get());
   case WalletsMessage::kGetWalletBalances:
      return processGetWalletBalances(env, msg.get_wallet_balances());
   case WalletsMessage::kGetExtAddresses:
      return processGetExtAddresses(env, msg.get_ext_addresses());
   case WalletsMessage::kGetIntAddresses:
      return processGetIntAddresses(env, msg.get_int_addresses());
   case WalletsMessage::kGetUsedAddresses:
      return processGetUsedAddresses(env, msg.get_used_addresses());
   case WalletsMessage::kCreateExtAddress:
      return processCreateExtAddress(env, msg.create_ext_address());
   case WalletsMessage::kGetAddrComments:
      return processGetAddrComments(env, msg.get_addr_comments());
   case WalletsMessage::kSetAddrComments:
      return processSetAddrComments(env, msg.set_addr_comments());
   case WalletsMessage::kSetTxComment:
      return processSetTxComment(msg.set_tx_comment());
   case WalletsMessage::kTxDetailsRequest:
      return processTXDetails(env, msg.tx_details_request());
   case WalletsMessage::kGetUtxos:
      return processGetUTXOs(env, msg.get_utxos());
   case WalletsMessage::kSetUserId:
      return processSetUserId(msg.set_user_id());
   case WalletsMessage::kGetAuthKey:
      return processAuthKey(env, msg.get_auth_key());
   case WalletsMessage::kReserveUtxos:
      return processReserveUTXOs(env, msg.reserve_utxos());
   case WalletsMessage::kGetReservedUtxos:
      return processGetReservedUTXOs(env, msg.get_reserved_utxos());
   case WalletsMessage::kUnreserveUtxos:
      return processUnreserveUTXOs(msg.unreserve_utxos());
   case WalletsMessage::kPayinRequest:
      return processPayin(env, msg.payin_request());
   case WalletsMessage::kPayoutRequest:
      return processPayout(env, msg.payout_request());
   default: break;
   }
   return true;
}

bool WalletsAdapter::processHdWalletGet(const Envelope &env
   , const std::string &walletId)
{
   const auto &hdWallet = getHDWalletById(walletId);
   if (!hdWallet) {
      logger_->error("[{}] HD wallet {} not found", __func__, walletId);
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_hd_wallet();
   msgResp->set_wallet_id(hdWallet->walletId());
   msgResp->set_name(hdWallet->name());
   msgResp->set_is_primary(hdWallet->isPrimary());
   msgResp->set_is_offline(hdWallet->isOffline());

   for (const auto &group : hdWallet->getGroups()) {
      auto msgGroup = msgResp->add_groups();
      msgGroup->set_type(group->index());
      msgGroup->set_ext_only(group->extOnly());
      msgGroup->set_name(group->name());
      msgGroup->set_desc(group->description());

      const auto &authGroup = std::dynamic_pointer_cast<bs::sync::hd::AuthGroup>(group);
      if (authGroup && !authGroup->userId().empty()) {
         msgGroup->set_salt(authGroup->userId().toBinStr());
      }

      for (const auto &leaf : group->getLeaves()) {
         auto msgLeaf = msgGroup->add_leaves();
         for (const auto &id : leaf->internalIds()) {
            msgLeaf->add_ids(id);
         }
         msgLeaf->set_path(leaf->path().toString());
         msgLeaf->set_name(leaf->shortName());
         msgLeaf->set_desc(leaf->description());
         msgLeaf->set_ext_only(leaf->extOnly());
      }
   }
   Envelope envResp{ env.id, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };  // broadcast
   return pushFill(envResp);
}

bool WalletsAdapter::processWalletGet(const Envelope &env
   , const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   std::vector<std::shared_ptr<bs::sync::Wallet>>  wallets;
   if (wallet) {
      wallets = { wallet };
   }
   else {
      const auto& hdWallet = getHDWalletById(walletId);
      if (!hdWallet) {
         logger_->error("[{}] wallet {} not found", __func__, walletId);
         return true;
      }
      const auto& group = hdWallet->getGroup(hdWallet->getXBTGroupType());
      if (!group) {
         logger_->error("[{}] no XBT group in wallet {}", __func__, walletId);
         return true;
      }
      wallets = group->getAllLeaves();
   }
   if (wallets.empty()) {
      logger_->error("[{}] no leaves for wallet {}", __func__, walletId);
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_wallet_data();
   msgResp->set_wallet_id(walletId);
   for (const auto& wallet : wallets) {
      for (const auto& addr : wallet->getUsedAddressList()) {
         auto msgAddr = msgResp->add_used_addresses();
         const auto& idx = wallet->getAddressIndex(addr);
         const auto& comment = wallet->getAddressComment(addr);
         msgAddr->set_index(idx);
         msgAddr->set_address(addr.display());
         msgAddr->set_comment(comment);
      }
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processWalletsList(const bs::message::Envelope& env
   , const WalletsMessage_WalletsListRequest& request)
{
   const auto& mapGroup = [](const std::shared_ptr<bs::sync::hd::Group>& grp)
   {
      bs::sync::HDWalletData::Group hdGroup;
      hdGroup.description = grp->description();
      hdGroup.name = grp->name();
      hdGroup.type = static_cast<bs::hd::CoinType>(grp->index());
      return hdGroup;
   };
   const auto& mapLeaf = [](const std::shared_ptr<bs::sync::hd::Leaf>& leaf)
   {
      bs::sync::HDWalletData::Leaf hdLeaf;
      hdLeaf.ids = leaf->internalIds();
      hdLeaf.name = leaf->shortName();
      hdLeaf.description = leaf->description();
      hdLeaf.extOnly = leaf->extOnly();
      hdLeaf.path = leaf->path();
      return hdLeaf;
   };
   std::vector<bs::sync::HDWalletData> result;
   for (const auto& wallet : hdWallets_) {
      bs::sync::HDWalletData hdWallet;
      hdWallet.id = wallet->walletId();
      hdWallet.name = wallet->name();
      hdWallet.primary = wallet->isPrimary();
      hdWallet.offline = wallet->isOffline();

      if (request.auth_group()) {
         const auto& grp = wallet->getGroup(bs::hd::BlockSettle_Auth);
         if (grp) {
            auto hdGroup = mapGroup(grp);
            for (const auto& leaf : grp->getLeaves()) {
               hdGroup.leaves.push_back(mapLeaf(leaf));
            }
            hdWallet.groups.push_back(hdGroup);
         }
      }

      if (request.cc_group()) {
         const auto& grp = wallet->getGroup(bs::hd::BlockSettle_CC);
         if (grp) {
            auto hdGroup = mapGroup(grp);
            for (const auto& leaf : grp->getLeaves()) {
               hdGroup.leaves.push_back(mapLeaf(leaf));
            }
            hdWallet.groups.push_back(hdGroup);
         }
      }

      const auto& xbtGroup = wallet->getGroup(wallet->getXBTGroupType());
      if (!xbtGroup) {
         continue;
      }
      auto hdGroup = mapGroup(xbtGroup);

      if (!wallet->canMixLeaves()) {
         // HW wallets marked as offline too, make sure to check that first
         if (wallet->isHardwareOfflineWallet() && request.watch_only()) {
            continue;
         }
         for (const auto& leaf : xbtGroup->getLeaves()) {
            const auto purpose = leaf->purpose();
            BTCNumericTypes::satoshi_type leafBalance = 0;
            const auto& itBal = walletBalances_.find(leaf->walletId());
            if (itBal != walletBalances_.end()) {
               leafBalance = itBal->second.walletBalance.spendableBalance;
            }
            if (((purpose == bs::hd::Purpose::Native) && request.hardware() && request.native_sw())
               || ((purpose == bs::hd::Purpose::Nested) && request.hardware() && request.native_sw())
               || ((purpose == bs::hd::Purpose::NonSegWit) && request.hardware() && request.legacy() && leafBalance)) {
               hdGroup.leaves.push_back(mapLeaf(leaf));
            }
         }
         hdWallet.groups.push_back(hdGroup);
         result.push_back(hdWallet);
      }
      else if (wallet->isOffline()) {
         for (const auto& leaf : xbtGroup->getLeaves()) {
            hdGroup.leaves.push_back(mapLeaf(leaf));
         }
         hdWallet.groups.push_back(hdGroup);
         result.push_back(hdWallet);
      }
      else if (request.full()) {
         for (const auto& leaf : xbtGroup->getLeaves()) {
            hdGroup.leaves.push_back(mapLeaf(leaf));
         }
         hdWallet.groups.push_back(hdGroup);
         result.push_back(hdWallet);
      }
   }

   WalletsMessage msg;
   auto msgResp = msg.mutable_wallets_list_response();
   msgResp->set_id(request.id());
   for (const auto& hdWallet : result) {
      auto walletData = msgResp->add_wallets();
      *walletData = hdWallet.toCommonMessage();
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processGetTxComment(const Envelope &env
   , const std::string &txBinHash)
{
   const auto &txHash = BinaryData::fromString(txBinHash);
   for (const auto &wallet : wallets_) {
      const auto &comment = wallet.second->getTransactionComment(txHash);
      if (!comment.empty()) {
         WalletsMessage msg;
         auto msgResp = msg.mutable_tx_comment();
         msgResp->set_tx_hash(txBinHash);
         msgResp->set_comment(comment);
         Envelope envResp{ env.id, ownUser_, env.sender, {}, {}
            , msg.SerializeAsString() };
         return pushFill(envResp);
      }
   }
   return true;
}

bool WalletsAdapter::processGetWalletBalances(const bs::message::Envelope &env
   , const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   {
      std::unique_lock<std::mutex> lock(mtx_);
      if (readyWallets_.find(walletId) == readyWallets_.end()) {
         return false;  // postpone processing until wallet will become ready
      }
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_wallet_balances();
   msgResp->set_wallet_id(walletId);
   double totalBalance = 0, spendableBalance = 0, unconfirmedBalance = 0;
   unsigned int addrCount = 0;
   for (const auto &id : wallet->internalIds()) {
      const auto &itBal = walletBalances_.find(id);
      if (itBal == walletBalances_.end()) {
         continue;
      }
      totalBalance += itBal->second.walletBalance.totalBalance / BTCNumericTypes::BalanceDivider;
      spendableBalance += itBal->second.walletBalance.spendableBalance / BTCNumericTypes::BalanceDivider;
      unconfirmedBalance += itBal->second.walletBalance.unconfirmedBalance / BTCNumericTypes::BalanceDivider;
      addrCount += itBal->second.addrCount;
      for (const auto &addrTxN : itBal->second.addressTxNMap) {
         auto msgAddr = msgResp->add_address_balances();
         msgAddr->set_address(addrTxN.first.toBinStr());
         msgAddr->set_txn(addrTxN.second);
         const auto &itAddrBal = itBal->second.addressBalanceMap.find(addrTxN.first);
         if (itAddrBal != itBal->second.addressBalanceMap.end()) {
            msgAddr->set_total_balance(itAddrBal->second.totalBalance);
            msgAddr->set_spendable_balance(itAddrBal->second.spendableBalance);
            msgAddr->set_unconfirmed_balance(itAddrBal->second.unconfirmedBalance);
         }
      }
   }
   msgResp->set_total_balance(totalBalance);
   msgResp->set_spendable_balance(spendableBalance);
   msgResp->set_unconfirmed_balance(unconfirmedBalance);
   msgResp->set_nb_addresses(addrCount);
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processGetExtAddresses(const bs::message::Envelope &env, const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   std::vector<bs::sync::Address> addresses;
   for (const auto &addr : wallet->getExtAddressList()) {
      const auto &index = wallet->getAddressIndex(addr);
      addresses.push_back({ addr, index, wallet->getWalletIdForAddress(addr) });
   }
   return sendAddresses(env, wallet->walletId(), addresses);
}

bool WalletsAdapter::processGetIntAddresses(const bs::message::Envelope &env, const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   std::vector<bs::sync::Address> addresses;
   for (const auto &addr : wallet->getIntAddressList()) {
      const auto &index = wallet->getAddressIndex(addr);
      addresses.push_back({ addr, index, wallet->getWalletIdForAddress(addr) });
   }
   return sendAddresses(env, wallet->walletId(), addresses);
}

bool WalletsAdapter::processGetUsedAddresses(const bs::message::Envelope &env, const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   std::vector<bs::sync::Address> addresses;
   for (const auto &addr : wallet->getUsedAddressList()) {
      const auto &index = wallet->getAddressIndex(addr);
      addresses.push_back({addr, index, wallet->getWalletIdForAddress(addr) });
   }
   return sendAddresses(env, wallet->walletId(), addresses);
}

bool WalletsAdapter::sendAddresses(const bs::message::Envelope &env
   , const std::string &walletId, const std::vector<bs::sync::Address> &addrs)
{
   WalletsMessage msg;
   auto msgResp = msg.mutable_wallet_addresses();
   msgResp->set_wallet_id(walletId);
   for (const auto &addr : addrs) {
      auto addrResp = msgResp->add_addresses();
      addrResp->set_address(addr.address.display());
      addrResp->set_index(addr.index);
      addrResp->set_wallet_id(addr.walletId);
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processCreateExtAddress(const bs::message::Envelope& env
   , const std::string& walletId)
{
   auto wallet = getWalletById(walletId);
   if (!wallet) {
      const auto& hdWallet = walletId.empty() ? getPrimaryWallet() : getHDWalletById(walletId);
      if (!hdWallet) {
         logger_->error("[{}] failed to find wallet {}", __func__, walletId);
         sendAddresses(env, walletId, {});
         return true;
      }
      const auto& xbtGroup = hdWallet->getGroup(hdWallet->getXBTGroupType());
      if (!xbtGroup) {
         logger_->error("[{}] no XBT group in wallet {}", __func__, walletId);
         sendAddresses(env, walletId, {});
         return true;
      }
      wallet = xbtGroup->getLeaf(bs::hd::Purpose::Native);
      if (!wallet) {
         logger_->error("[{}] no native XBT leaf in wallet {}", __func__, walletId);
         sendAddresses(env, walletId, {});
         return true;
      }
   }
   const auto& cbAddress = [env, wallet, this](const bs::Address&)
   {  // send all ext addresses - the new one should be included as the last
      processGetExtAddresses(env, wallet->walletId());
   };
   wallet->getNewExtAddress(cbAddress);
   return true;
}

bool WalletsAdapter::processGetAddrComments(const bs::message::Envelope &env
   , const BlockSettle::Common::WalletsMessage_WalletAddresses &request)
{
   const auto &wallet = getWalletById(request.wallet_id());
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, request.wallet_id());
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_addr_comments();
   msgResp->set_wallet_id(wallet->walletId());
   for (const auto &addrPair : request.addresses()) {
      try {
         const auto &addr = bs::Address::fromAddressString(addrPair.address());
         const auto &comment = wallet->getAddressComment(addr);
         if (!comment.empty()) {
            auto commentData = msgResp->add_comments();
            commentData->set_address(addrPair.address());
            commentData->set_comment(comment);
         }
      }
      catch (const std::exception &) {}
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processSetAddrComments(const bs::message::Envelope &env
   , const BlockSettle::Common::WalletsMessage_AddressComments &request)
{
   const auto &wallet = getWalletById(request.wallet_id());
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, request.wallet_id());
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_addr_comments();
   msgResp->set_wallet_id(request.wallet_id());
   for (const auto &comm : request.comments()) {
      try {
         const auto &addr = bs::Address::fromAddressString(comm.address());
         if (wallet->setAddressComment(addr, comm.comment())) {
            auto commData = msgResp->add_comments();
            commData->set_address(comm.address());
            commData->set_comment(comm.comment());
         }
      }
      catch (const std::exception &) {}
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processSetTxComment(const WalletsMessage_TXComment& request)
{
   auto wallet = getWalletById(request.wallet_id());
   if (!wallet) {
      const auto& hdWallet = getHDWalletById(request.wallet_id());
      if (!hdWallet) {
         logger_->error("[{}] wallet {} not found", __func__, request.wallet_id());
         return true;
      }
      const auto& group = hdWallet->getGroup(hdWallet->getXBTGroupType());
      if (group) {
         wallet = group->getLeaf(bs::hd::Purpose::Native);
      }
      if (!wallet) {
         logger_->error("[{}] no nativeSW XBT wallet in {}", __func__, request.wallet_id());
         return true;
      }
   }
   wallet->setTransactionComment(BinaryData::fromString(request.tx_hash()), request.comment());
   WalletsMessage msg;
   auto msgResp = msg.mutable_tx_comment();
   *msgResp = request;
   Envelope envResp{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processTXDetails(const bs::message::Envelope &env
   , const BlockSettle::Common::WalletsMessage_TXDetailsRequest &request)
{
   std::set<BinaryData> initialHashes;
   std::vector<bs::sync::TXWallet>  requests;
   for (const auto &req : request.requests()) {
      const auto &txHash = BinaryData::fromString(req.tx_hash());
      auto walletId = req.wallet_id();
      if (walletId.empty() && !request.address().empty()) {
         try {
            const auto& wallet = getWalletByAddress(bs::Address::fromAddressString(request.address()));
            if (wallet) {
               walletId = wallet->walletId();
            }
         }
         catch (const std::exception&) {}
      }
      requests.push_back({ txHash, walletId, req.value() });
      initialHashes.insert(txHash);
   }
   ArmoryMessage msg;
   auto msgReq = msg.mutable_get_txs_by_hash();
   for (const auto &txHash : initialHashes) {
      msgReq->add_tx_hashes(txHash.toBinStr());
   }
   msgReq->set_disable_cache(!request.use_cache());
   Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   if (pushFill(envReq)) {
      initialHashes_[envReq.id] = { env, std::map<BinaryData, Tx>{}, requests };
      return true;
   }
   return false;
}

bool WalletsAdapter::processGetUTXOs(const bs::message::Envelope& env, const WalletsMessage_UtxoListRequest& request)
{
   auto utxoReq = std::make_shared<UTXORequest>();
   utxoReq->env = env;
   utxoReq->requireZC = !request.confirmed_only();
   utxoReq->id = request.id();
   utxoReq->walletId = request.wallet_id();
   const auto& hdWallet = getHDWalletById(request.wallet_id());
   if (hdWallet) {
      const auto& group = hdWallet->getGroup(hdWallet->getXBTGroupType());
      if (!group) {
         logger_->error("[{}] can't find XBT group in {}", __func__, hdWallet->walletId());
         return true;
      }
      for (const auto& leaf : group->getLeaves()) {
         utxoReq->walletIds.insert(leaf->walletId());
      }
   }
   else {
      const auto& wallet = getWalletById(request.wallet_id());
      if (!wallet) {
         logger_->error("[{}] wallet {} not found", __func__, request.wallet_id());
         return true;
      }
      utxoReq->walletIds.insert(wallet->walletId());
   }
   for (const auto& walletId : utxoReq->walletIds) {
      const auto& wallet = getWalletById(walletId);
      if (!wallet) {
         logger_->error("[{}] wallet {} not found", __func__, walletId);
         return true;
      }
      const auto& walletIds = wallet->internalIds();
      if (!request.confirmed_only()) {
         ArmoryMessage msgZC;
         auto msgReq = msgZC.mutable_get_zc_utxos();
         for (const auto& wltId : walletIds) {
            msgReq->add_wallet_ids(wltId);
         }
         Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}, msgZC.SerializeAsString(), true };
         if (pushFill(envReq)) {
            utxoZcReqs_[envReq.id] = utxoReq;
         }
      }
      ArmoryMessage msgSpendable;
      auto msgReq = msgSpendable.mutable_get_spendable_utxos();
      for (const auto& walletId : walletIds) {
         msgReq->add_wallet_ids(walletId);
      }
      Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}, msgSpendable.SerializeAsString(), true };
      if (pushFill(envReq)) {
         utxoSpendableReqs_[envReq.id] = utxoReq;
      }
   }
   return true;
}

void WalletsAdapter::processTransactions(uint64_t msgId
   , const ArmoryMessage_Transactions &response)
{
   const auto &convertTXs = [response]() -> std::vector<Tx>
   {
      std::vector<Tx> result;
      for (const auto &txData : response.transactions()) {
         const Tx tx(BinaryData::fromString(txData.tx()));
         tx.setTxHeight(txData.height());
         result.emplace_back(tx);
      }
      return result;
   };

   const auto& itPayin = payinTXsCbMap_.find(msgId);
   if (itPayin != payinTXsCbMap_.end()) {
      itPayin->second(convertTXs());
      payinTXsCbMap_.erase(itPayin);
      return;
   }

   auto itId = initialHashes_.find(msgId);
   if (itId != initialHashes_.end()) {
      auto data = itId->second;
      initialHashes_.erase(itId);
      const auto &initialTXs = convertTXs();
      for (const auto &tx : initialTXs) {
         data.allTXs[tx.getThisHash()] = tx;
      }
      std::set<BinaryData> prevHashes;
      for (const auto &tx : initialTXs) {
         for (size_t i = 0; i < tx.getNumTxIn(); i++) {
            TxIn in = tx.getTxInCopy(i);
            OutPoint op = in.getOutPoint();
            if (data.allTXs.find(op.getTxHash()) == data.allTXs.end()) {
               prevHashes.insert(op.getTxHash());
            }
         }
      }
      ArmoryMessage msg;
      auto msgReq = msg.mutable_get_txs_by_hash();
      for (const auto &txHash : prevHashes) {
         msgReq->add_tx_hashes(txHash.toBinStr());
      }
      Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      if (pushFill(envReq)) {
         prevHashes_[envReq.id] = data;
      }
      return;
   }

   itId = prevHashes_.find(msgId);
   if (itId != prevHashes_.end()) {
      for (const auto &tx : convertTXs()) {
         itId->second.allTXs[tx.getThisHash()] = tx;
      }
      
      WalletsMessage msg;
      auto msgResp = msg.mutable_tx_details_response();
      for (auto &req : itId->second.requests) {
         auto resp = msgResp->add_responses();
         resp->set_tx_hash(req.txHash.toBinStr());
         resp->set_wallet_id(req.walletId);
         const auto &itTx = itId->second.allTXs.find(req.txHash);
         if (itTx == itId->second.allTXs.end()) {
            logger_->warn("[{}] failed to find TX hash {}", __func__, req.txHash.toHexStr(true));
            continue;
         }
         std::string walletId = req.walletId;
         if (walletId.empty()) { // if no walletId is given, get it from output addresses
            for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
               const TxOut& out = itTx->second.getTxOutCopy((int)i);
               const auto& addr = bs::Address::fromTxOut(out);
               const auto& addrWallet = getWalletByAddress(addr);
               if (addrWallet) {
                  walletId = addrWallet->walletId();
                  break;
               }
            }
         }
         if (walletId.empty()) { // obtain walletId from input addresses if output ones were not found
            for (int i = 0; i < itTx->second.getNumTxIn(); ++i) {
               const auto& in = itTx->second.getTxInCopy(i);
               const auto& op = in.getOutPoint();
               const auto& itPrev = itId->second.allTXs.find(op.getTxHash());
               if (itPrev == itId->second.allTXs.end()) {
                  continue;
               }
               const TxOut& prevOut = itPrev->second.getTxOutCopy(op.getTxOutIndex());
               const auto &addr = bs::Address::fromTxOut(prevOut);
               const auto &addressWallet = getWalletByAddress(addr);
               if (addressWallet) {
                  walletId = addressWallet->walletId();
                  break;
               }
            }
         }
         if (req.value == 0) {
            req.value = itTx->second.getSumOfOutputs();
         }
         Transaction::Direction direction = bs::sync::Transaction::Direction::Unknown;
         const auto &wallet = getWalletById(walletId);
         if (wallet) {
            direction = getDirection(req.txHash, wallet, itId->second.allTXs);
            resp->set_wallet_name(wallet->name());
            resp->set_wallet_type((int)wallet->type());
            resp->set_wallet_symbol(wallet->displaySymbol().toStdString());
            resp->set_comment(wallet->getTransactionComment(req.txHash));
            resp->set_valid(wallet->isTxValid(req.txHash) == bs::sync::TxValidity::Valid);
            resp->set_amount(wallet->displayTxValue(req.value).toStdString());
            resp->set_direction((int)direction);

            resp->set_tx(itTx->second.serialize().toBinStr());
            const bool isReceiving = (req.value > 0);
            std::set<bs::Address> ownAddresses, foreignAddresses;
            for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
               const TxOut &out = itTx->second.getTxOutCopy((int)i);
               try {
                  const auto &addr = bs::Address::fromTxOut(out);
                  const auto &addrWallet = getWalletByAddress(addr);
                  if (addrWallet == wallet) {
                     ownAddresses.insert(addr);
                  } else {
                     foreignAddresses.insert(addr);
                  }
               } catch (const std::exception &) {
                  // address conversion failure - likely OP_RETURN - do nothing
               }
            }
            if (!isReceiving && (ownAddresses.size() == 1) && !foreignAddresses.empty()) {
               if (!wallet->isExternalAddress(*ownAddresses.cbegin())) {
                  ownAddresses.clear();   // treat the only own internal address as change and throw away
               }
            }
            const auto &setOutAddresses = [&resp](const std::set<bs::Address> &addrs)
            {
               for (const auto &addr : addrs) {
                  resp->add_out_addresses(addr.display());
               }
            };
            if (!ownAddresses.empty()) {
               setOutAddresses(ownAddresses);
            } else {
               setOutAddresses(foreignAddresses);
            }
         }
         else {
            logger_->warn("[{}] failed to find wallet {}", __func__, req.walletId);
         }
         std::set<std::shared_ptr<bs::sync::Wallet>> inputWallets;
         int64_t value = 0;
         for (int i = 0; i < itTx->second.getNumTxIn(); ++i) {
            bs::sync::AddressDetails addrDet;
            const auto &in = itTx->second.getTxInCopy(i);
            const auto &op = in.getOutPoint();
            const auto &itPrev = itId->second.allTXs.find(op.getTxHash());
            if (itPrev == itId->second.allTXs.end()) {
               continue;
            }
            const TxOut &prevOut = itPrev->second.getTxOutCopy(op.getTxOutIndex());
            value += prevOut.getValue();
            addrDet.address = bs::Address::fromTxOut(prevOut);
            addrDet.value = prevOut.getValue();
            addrDet.type = prevOut.getScriptType();
            addrDet.outHash = op.getTxHash();
            addrDet.outIndex = op.getTxOutIndex();
            const auto &addressWallet = getWalletByAddress(addrDet.address);
            if (addressWallet) {
               addrDet.walletName = addressWallet->name();
               addrDet.valueStr = "-" + addressWallet->displayTxValue(prevOut.getValue()).toStdString();
               const auto &rootWallet = getHDRootForLeaf(addressWallet->walletId());
               if (rootWallet) {
                  const auto &xbtLeaves = rootWallet->getGroup(rootWallet->getXBTGroupType())->getLeaves();
                  bool isXbtLeaf = false;
                  for (const auto &leaf : xbtLeaves) {
                     if (*leaf == *addressWallet) {
                        isXbtLeaf = true;
                        break;
                     }
                  }
                  if (isXbtLeaf) {
                     inputWallets.insert(xbtLeaves.cbegin(), xbtLeaves.cend());
                  } else {
                     inputWallets.insert(addressWallet);
                  }
               } else {
                  inputWallets.insert(addressWallet);
               }
            }
            else {
               addrDet.valueStr = fmt::format("-{:.8f}", prevOut.getValue() / BTCNumericTypes::BalanceDivider);
            }
            auto inAddr = resp->add_input_addresses();
            inAddr->set_address(addrDet.address.display());
            inAddr->set_value(addrDet.value);
            inAddr->set_value_string(addrDet.valueStr);
            inAddr->set_wallet_name(addrDet.walletName);
            inAddr->set_out_hash(addrDet.outHash.toBinStr());
            inAddr->set_out_index(addrDet.outIndex);
            inAddr->set_script_type((int)addrDet.type);
         }
         const auto fee = value - itTx->second.getSumOfOutputs();
         switch (direction) {
         case Transaction::Direction::Internal:
            resp->set_amount(wallet->displayTxValue(-fee).toStdString());
            break;
         case Transaction::Direction::Sent:
            resp->set_amount(wallet->displayTxValue(req.value + fee).toStdString());
            break;
         }

         std::vector<TxOut> allOutputs;
         for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
            const TxOut out = itTx->second.getTxOutCopy(i);
            allOutputs.push_back(out);
         }
         bs::sync::AddressDetails lastChange;
         std::vector<bs::sync::AddressDetails> outputAddrs;
         for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
            bs::sync::AddressDetails addrDet;
            TxOut out = itTx->second.getTxOutCopy(i);
            addrDet.address = bs::Address::fromTxOut(out);
            addrDet.value = out.getValue();
            addrDet.type = out.getScriptType();
            addrDet.outIndex = (int)out.getIndex();
            addrDet.outHash = out.getScript();
            const auto &addrWallet = getWalletByAddress(addrDet.address);
            if (addrWallet) {
               addrDet.valueStr = addrWallet->displayTxValue(out.getValue()).toStdString();
               addrDet.walletName = addrWallet->name();
               if ((allOutputs.size() > 1) && (inputWallets.find(addrWallet) != inputWallets.end())) {
                  lastChange = addrDet;
               }
            }
            else {
               addrDet.valueStr = fmt::format("{:.8f}", out.getValue() / BTCNumericTypes::BalanceDivider);
            }
            outputAddrs.push_back(addrDet);
         }
         if (!lastChange.address.empty()) {
            auto chgAddr = resp->mutable_change_address();
            chgAddr->set_address(lastChange.address.display());
            chgAddr->set_wallet_name(lastChange.walletName);
            chgAddr->set_value(lastChange.value);
            chgAddr->set_value_string(lastChange.valueStr);
            chgAddr->set_script_type((int)lastChange.type);
            chgAddr->set_out_hash(lastChange.outHash.toBinStr());
            chgAddr->set_out_index(lastChange.outIndex);
            const auto &itOut = std::find_if(outputAddrs.cbegin(), outputAddrs.cend()
               , [addr = lastChange.address](const bs::sync::AddressDetails &addrDet){
               return (addrDet.address == addr);
            });
            if (itOut != outputAddrs.end()) {
               outputAddrs.erase(itOut);
            }
         }
         for (const auto &addrDet : outputAddrs) {
            auto outAddr = resp->add_output_addresses();
            outAddr->set_address(addrDet.address.display());
            outAddr->set_wallet_name(addrDet.walletName);
            outAddr->set_value(addrDet.value);
            outAddr->set_value_string(addrDet.valueStr);
            outAddr->set_script_type((int)addrDet.type);
            outAddr->set_out_hash(addrDet.outHash.toBinStr());
            outAddr->set_out_index(addrDet.outIndex);
         }
      }
      Envelope envResp{ itId->second.env.id, ownUser_, itId->second.env.sender
         , {}, {}, msg.SerializeAsString() };
      pushFill(envResp);   //TODO: send TX details in portions to allow faster UI response
      prevHashes_.erase(itId);
   }
}

Transaction::Direction WalletsAdapter::getDirection(const BinaryData &txHash
   , const std::shared_ptr<Wallet> &wallet, const std::map<BinaryData, Tx> &allTXs) const
{
   const auto &itTx = allTXs.find(txHash);
   if (itTx == allTXs.end()) {
      return Transaction::Direction::Unknown;
   }
   if (wallet->type() == bs::core::wallet::Type::Authentication) {
      return Transaction::Auth;
   } else if (wallet->type() == bs::core::wallet::Type::ColorCoin) {
      return Transaction::Delivery;
   }
   const auto &group = getGroupByWalletId(wallet->walletId());
   bool ourOuts = false;
   bool otherOuts = false;
   bool ourIns = false;
   bool otherIns = false;
   bool ccTx = false;

   for (size_t i = 0; i < itTx->second.getNumTxIn(); ++i) {
      const TxIn &in = itTx->second.getTxInCopy((int)i);
      const OutPoint &op = in.getOutPoint();
      const auto &itPrevTx = allTXs.find(op.getTxHash());
      if (itPrevTx == allTXs.end()) {
         continue;
      }
      const auto &prevOut = itPrevTx->second.getTxOutCopy(op.getTxOutIndex());
      const auto &addr = bs::Address::fromTxOut(prevOut);
      const auto &addrWallet = getWalletByAddress(addr);
      const auto &addrGroup = addrWallet ? getGroupByWalletId(addrWallet->walletId())
         : nullptr;
      if ((addrWallet && (addrWallet == wallet)) || (group && (group == addrGroup))) {
         ourIns = true;
      }
      else {
         otherIns = true;
      }
      if (addrWallet && (addrWallet->type() == bs::core::wallet::Type::ColorCoin)) {
         ccTx = true;
      }
   }
   for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
      const TxOut &out = itTx->second.getTxOutCopy((int)i);
      const auto &addr = bs::Address::fromTxOut(out);
      const auto &addrWallet = getWalletByAddress(addr);
      const auto &addrGroup = addrWallet ? getGroupByWalletId(addrWallet->walletId())
         : nullptr;
      if ((addrWallet && (addrWallet == wallet)) || (group && (group == addrGroup))) {
         ourOuts = true;
      }
      else {
         otherOuts = true;
      }
      if (addrWallet && (addrWallet->type() == bs::core::wallet::Type::ColorCoin)) {
         ccTx = true;
         break;
      }
      else if (!ourOuts) {
         if ((group && addrGroup) && (group == addrGroup)) {
            ourOuts = true;
            otherOuts = false;
         }
      }
   }
   if (wallet->type() == bs::core::wallet::Type::Settlement) {
      if (ourOuts) {
         return Transaction::PayIn;
      }
      return Transaction::PayOut;
   }
   if (ccTx) {
      return Transaction::Payment;
   }
   if (ourOuts && ourIns && !otherOuts && !otherIns) {
      return Transaction::Internal;
   }
   if (!ourIns) {
      return Transaction::Received;
   }
   if (otherOuts) {
      return Transaction::Sent;
   }
   return Transaction::Direction::Unknown;
}

bool WalletsAdapter::processUTXOs(uint64_t msgId, const ArmoryMessage_UTXOs& response)
{
   std::vector<UTXO> utxos;
   utxos.reserve(response.utxos_size());
   try {
      for (const auto& serUtxo : response.utxos()) {
         UTXO utxo;
         utxo.unserialize(BinaryData::fromString(serUtxo));
         utxos.emplace_back(std::move(utxo));
      }
   } catch (const std::exception& e) {
      logger_->error("[{}] failed to deser UTXO: {}", __func__, e.what());
   }

   const auto& itReserve = utxoReserveReqs_.find(msgId);
   if (itReserve != utxoReserveReqs_.end()) {
      itReserve->second(utxos);
      utxoReserveReqs_.erase(itReserve);
      return true;
   }

   const auto& sendUTXOs = [this](std::shared_ptr<UTXORequest> utxoReq)
   {
      if (utxoReq->spendableUTXOs.size() < utxoReq->walletIds.size()) {
         return;
      }
      if (utxoReq->requireZC && (utxoReq->zcUTXOs.size() < utxoReq->walletIds.size())) {
         return;
      }
      WalletsMessage msg;
      auto msgResp = msg.mutable_utxos();
      msgResp->set_id(utxoReq->id);
      msgResp->set_wallet_id(utxoReq->walletId);
      for (const auto& perWallet : utxoReq->spendableUTXOs) {
         for (const auto& utxo : perWallet.second) {
            msgResp->add_utxos(utxo.serialize().toBinStr());
         }
      }
      for (const auto& perWallet : utxoReq->zcUTXOs) {
         for (const auto& utxo : perWallet.second) {
            msgResp->add_utxos(utxo.serialize().toBinStr());
         }
      }
      Envelope envResp{ utxoReq->env.id, ownUser_, utxoReq->env.sender
         , {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
   };
   const auto& filterUTXOs = [this]
      (const std::string &walletId, std::vector<UTXO> utxos)->std::vector<UTXO>
   {
      const auto& wallet = getWalletById(walletId);
      if (!wallet) {
         logger_->error("[WalletsAdapter::processUTXOs] can't find wallet {}", walletId);
         return {};
      }
      //TODO: apply per-wallet UTXO filtering if needed
      return std::move(utxos);
   };
   const auto& walletId = response.wallet_id();
   const auto& itSpendable = utxoSpendableReqs_.find(msgId);
   if (itSpendable == utxoSpendableReqs_.end()) {
      const auto& itZC = utxoZcReqs_.find(msgId);
      if (itZC == utxoZcReqs_.end()) {
         logger_->warn("[{}] unknown UTXO response {}", __func__, msgId);
         return true;
      }
      if (!itZC->second->requireZC) {
         logger_->warn("[{}] unrequested ZC UTXO response {}", __func__, msgId);
         return true;
      }
      itZC->second->zcUTXOs[walletId] = std::move(utxos);
      if (itZC->second->zcUTXOs.size() >= itZC->second->walletIds.size()) {
         sendUTXOs(itZC->second);
      }
      utxoZcReqs_.erase(itZC);
   }
   else {
      std::vector<UTXO> utxos;
      utxos.reserve(response.utxos_size());
      for (const auto& serUtxo : response.utxos()) {
         UTXO utxo;
         utxo.unserialize(BinaryData::fromString(serUtxo));
         utxos.emplace_back(std::move(utxo));
      }
      itSpendable->second->spendableUTXOs[walletId] = std::move(filterUTXOs(walletId, utxos));
      if (itSpendable->second->spendableUTXOs.size() == itSpendable->second->walletIds.size()) {
         sendUTXOs(itSpendable->second);
      }
      utxoSpendableReqs_.erase(itSpendable);
   }
   return true;
}

bool WalletsAdapter::processSetUserId(const std::string& userIdHex)
{
   const auto& userId = BinaryData::CreateFromHex(userIdHex);
   std::string primaryWalletId;
   for (const auto& hdWallet : hdWallets_) {
      hdWallet->setUserId(userId);
      if (primaryWalletId.empty() && hdWallet->isPrimary()) {
         primaryWalletId = hdWallet->walletId();
      }
   }
   signerClient_->setUserId(userId, primaryWalletId);
   return true;
}

bool WalletsAdapter::processAuthKey(const bs::message::Envelope& env
   , const std::string& address)
{
   bs::Address authAddr;
   try {
      authAddr = bs::Address::fromAddressString(address);
   }
   catch (const std::exception&) {
      logger_->error("[{}] failed to deser auth address {}", __func__, address);
      return true;
   }
   const auto& cbPubKey = [this, env, authAddr](const SecureBinaryData& pubKey)
   {
      WalletsMessage msg;
      auto msgResp = msg.mutable_auth_key();
      msgResp->set_auth_address(authAddr.display());
      msgResp->set_auth_key(pubKey.toBinStr());
      Envelope envResp{ env.id, ownUser_, env.sender, {}, {}
         , msg.SerializeAsString() };
      pushFill(envResp);
   };
   const auto &priWallet = getPrimaryWallet();
   if (!priWallet) {
      cbPubKey({});
      return true;
   }
   const auto& addrWallet = getWalletByAddress(authAddr);
   if (addrWallet->type() == bs::core::wallet::Type::Authentication) {
      const auto& group = priWallet->getGroup(bs::hd::BlockSettle_Settlement);
      std::shared_ptr<bs::sync::hd::SettlementLeaf> settlLeaf;
      if (group) {
         const auto settlGroup = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(group);
         if (!settlGroup) {
            logger_->error("[WalletsAdapter::processAuthKey] wrong settlement group type");
            return true;
         }
         settlLeaf = settlGroup->getLeaf(authAddr);
      }
      if (settlLeaf) {
         settlLeaf->getRootPubkey(cbPubKey);
      } else {
         const auto& cbWrap = [this, priWallet, cbPubKey](const SecureBinaryData& pubKey)
         {
            cbPubKey(pubKey);
            priWallet->synchronize([] {});
         };
         signerClient_->createSettlementWallet(authAddr, cbWrap);
      }
   }
   else if (addrWallet->type() == bs::core::wallet::Type::Bitcoin) { // easy settlement auth address
      signerClient_->getAddressPubkey(addrWallet->walletId(), address, cbPubKey);
   }
   else {
      logger_->error("[WalletsAdapter::processAuthKey] invalid wallet type {} "
         "for auth address {}", (int)addrWallet->type(), address);
      cbPubKey({});
      return true;
   }
   return true;
}

bool WalletsAdapter::processReserveUTXOs(const bs::message::Envelope& env
   , const WalletsMessage_ReserveUTXOs& request)
{
   const auto& sendResponse = [this, env, request](const std::vector<UTXO>& utxos)
   {
      WalletsMessage msg;
      auto msgResp = msg.mutable_reserved_utxos();
      msgResp->set_id(request.id());
      msgResp->set_sub_id(request.sub_id());
      for (const auto& utxo : utxos) {
         msgResp->add_utxos(utxo.serialize().toBinStr());
      }
      Envelope envResp{ env.id, ownUser_, env.sender, {}, {}
         , msg.SerializeAsString() };
      pushFill(envResp);
   };
   if (request.utxos_size()) {
      std::vector<UTXO> utxos;
      for (const auto& utxoSer : request.utxos()) {
         UTXO utxo;
         utxo.unserialize(BinaryData::fromString(utxoSer));
         utxos.push_back(std::move(utxo));
      }
      logger_->debug("[{}] reserved {} UTXOs for {}/{}", __func__, utxos.size()
         , request.id(), request.sub_id());
      utxoResMgr_->reserve(request.id(), utxos, request.sub_id());
      sendResponse(utxos);
   }
   else {
      if (!request.amount()) {
         logger_->error("[{}] {}/{} zero amount and no UTXOs", __func__
            , request.id(), request.sub_id());
         sendResponse({});
         return true;
      }
      auto wallet = getWalletById(request.sub_id());
      if (!wallet) {
         const auto& hdWallet = getHDWalletById(request.sub_id());
         if (hdWallet) {
            const auto& group = hdWallet->getGroup(hdWallet->getXBTGroupType());
            if (group) {
               wallet = group->getLeaf(bs::hd::Purpose::Native);
            }
         }
      }
      if (!wallet) {
         logger_->error("[{}] {}: no wallet found by {} and no UTXOs", __func__
            , request.id(), request.sub_id());
         sendResponse({});
         return true;
      }
      const auto& responded = std::make_shared<bool>(false);
      const auto& accUTXOs = std::make_shared<std::vector<UTXO>>();
      const auto& cbFilter = [this, sendResponse, request, responded, accUTXOs]
         (const std::vector<UTXO>& utxos)
      {
         if (*responded) {
            return;
         }
         auto utxosCopy = utxos;
         if (!accUTXOs->empty() && accUTXOs->at(0).isInitialized()) {
            utxosCopy.insert(utxosCopy.end(), accUTXOs->cbegin(), accUTXOs->cend());
         }
         decltype(utxosCopy) foo;
         utxoResMgr_->filter(utxosCopy, foo);
         const uint64_t amount = request.amount() + settlementFee_ * 230;  //FIXME: not sure if this is the right place to add fee
         const auto &filteredUTXOs = bs::selectUtxoForAmount(utxosCopy, amount);
         uint64_t utxoAmount = 0;
         for (const auto& utxo : filteredUTXOs) {
            utxoAmount += utxo.getValue();
         }
         if (utxoAmount < amount) {
            if (request.use_zc() && accUTXOs->empty()) { // 1 more invocation will follow
               if (!utxos.empty()) {
                  accUTXOs->insert(accUTXOs->end(), utxos.cbegin(), utxos.cend());
               }
               else {
                  accUTXOs->push_back({});
               }
               return;
            }
            logger_->warn("[WalletsAdapter::processReserveUTXOs] insufficient"
               " amount {} < {}", utxoAmount, amount);
            sendResponse({});
            *responded = true;
            return;
         }
         logger_->debug("[WalletsAdapter::processReserveUTXOs] reserved {} UTXOs"
            " {} amount={} ({}) for {}/{}", filteredUTXOs.size(), utxoAmount
            , amount, request.amount(), request.id(), request.sub_id());
         utxoResMgr_->reserve(request.id(), filteredUTXOs, request.sub_id());
         sendResponse(filteredUTXOs);
         *responded = true;
      };
      ArmoryMessage msgSpendable;
      auto msgReq = msgSpendable.mutable_get_spendable_utxos();
      for (const auto& walletId : wallet->internalIds()) {
         msgReq->add_wallet_ids(walletId);
      }
      Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}
         , msgSpendable.SerializeAsString(), true };
      if (pushFill(envReq)) {
         utxoReserveReqs_[envReq.id] = cbFilter;
      }
      if (request.use_zc()) {
         ArmoryMessage msgZC;
         auto msgReq = msgSpendable.mutable_get_zc_utxos();
         for (const auto& walletId : wallet->internalIds()) {
            msgReq->add_wallet_ids(walletId);
         }
         Envelope envReqZC{ 0, ownUser_, blockchainUser_, {}, {}
            , msgZC.SerializeAsString(), true };
         if (pushFill(envReqZC)) {
            utxoReserveReqs_[envReqZC.id] = cbFilter;
         }
      }
   }
   return true;
}

bool WalletsAdapter::processGetReservedUTXOs(const bs::message::Envelope& env
   , const WalletsMessage_ReservationKey& request)
{
   WalletsMessage msg;
   auto msgResp = msg.mutable_reserved_utxos();
   msgResp->set_id(request.id());
   msgResp->set_sub_id(request.sub_id());
   for (const auto& utxo : utxoResMgr_->get(request.id(), request.sub_id())) {
      msgResp->add_utxos(utxo.serialize().toBinStr());
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processUnreserveUTXOs(const WalletsMessage_ReservationKey& request)
{
   utxoResMgr_->unreserve(request.id(), request.sub_id());
   return true;
}

bool WalletsAdapter::processPayin(const bs::message::Envelope& env
   , const WalletsMessage_PayinRequest& request)
{
   const auto& sendResponse = [this, env](const bs::Address &settlementAddr
      , const bs::core::wallet::TXSignRequest &txReq, const std::string& errorMsg = {})
   {
      logger_->debug("[WalletsAdapter::processPayin::sendResponse] <{}> {}"
         , settlementAddr.display(), errorMsg);
      WalletsMessage msg;
      auto msgResp = msg.mutable_xbt_tx_response();
      if (!settlementAddr.empty()) {
         msgResp->set_settlement_address(settlementAddr.display());
      }
      if (txReq.isValid()) {
         *msgResp->mutable_tx_request() = bs::signer::coreTxRequestToPb(txReq);
      }
      if (!errorMsg.empty()) {
         msgResp->set_error_text(errorMsg);
      }
      Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
   };
   if (!settlementFee_) {
      logger_->warn("[{}] no settlement fee", __func__);
      sendResponse({}, {}, "no settlement fee");
      return true;
   }
   const auto& inputs = utxoResMgr_->get(request.reserve_id());
   if (inputs.empty()) {
      logger_->warn("[{}] inputs: {}", __func__, inputs.size());
      sendResponse({}, {}, "no inputs reserved for " + request.reserve_id());
      return true;
   }
   uint64_t inputAmount = 0;
   for (const auto& input : inputs) {
      inputAmount += input.getValue();
   }
   if (inputAmount < request.amount()) {
      sendResponse({}, {}, fmt::format("invalid inputs amount: {} < {}"
         , inputAmount, request.amount()));
      return true;
   }
   auto walletIds = utxoResMgr_->getSubIds(request.reserve_id());
   if (walletIds.empty() || walletIds[0].empty()) {
      for (const auto& input : inputs) {
         const auto& inputAddr = bs::Address::fromUTXO(input);
         const auto& wallet = getWalletByAddress(inputAddr);
         if (wallet) {
            walletIds.push_back(wallet->walletId());
            break;
         }
      }
   }
   const auto& priWallet = getPrimaryWallet();
   if (!priWallet) {
      sendResponse({}, {}, "no primary wallet");
      return true;
   }
   bs::Address ownAuthAddr;
   try {
      ownAuthAddr = bs::Address::fromAddressString(request.own_auth_address());
   }
   catch (const std::exception&) {
      sendResponse({}, {}, "invalid own auth address");
      return true;
   }
   const auto& settlementId = SecureBinaryData::fromString(request.settlement_id());
   const auto &group = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(
      priWallet->getGroup(bs::hd::BlockSettle_Settlement));
   std::shared_ptr<bs::sync::hd::SettlementLeaf> settlLeaf;
   if (group) {
      settlLeaf = group->getLeaf(ownAuthAddr);
   }

   const auto &cbSettlAddr = [this, sendResponse, inputs, request, walletIds]
      (const bs::Address& settlAddr)
   {
      if (settlAddr.empty()) {
         sendResponse({}, {}, "invalid settlement address");
         return;
      }
      auto utxos = bs::Address::decorateUTXOsCopy(inputs);
      std::map<unsigned, std::vector<std::shared_ptr<ArmorySigner::ScriptRecipient>>> recipientsMap;
      std::vector<std::shared_ptr<ArmorySigner::ScriptRecipient>> recVec({
         settlAddr.getRecipient(bs::XBTAmount{request.amount()}) });
      recipientsMap.emplace(0, recVec);
      const auto& payment = PaymentStruct(recipientsMap, 0, settlementFee_, 0);
      const auto& coinSelection = CoinSelection(nullptr, {}, request.amount(), topBlock_);
      uint64_t utxoAmount = 0;
      for (const auto& utxo : utxos) {
         utxoAmount += utxo.getValue();
      }
      logger_->debug("[WalletsAdapter::processPayin] UTXOs have {} for amount {}"
         , utxoAmount, request.amount());

      try { // since we always use reservation, all supplied inputs should be used
         UtxoSelection selection;
         selection = UtxoSelection(utxos);
         selection.fee_byte_ = settlementFee_;
         selection.computeSizeAndFee(payment);
         auto selectedInputs = selection.utxoVec_;
         auto fee = selection.fee_;
         bool needChange = true;

         uint64_t inputAmount = 0;
         for (const auto& utxo : selectedInputs) {
            inputAmount += utxo.getValue();
         }
         const int64_t changeAmount = inputAmount - request.amount() - fee;
         if (changeAmount < 0) {
            throw std::runtime_error("negative change amount");
         }
         if (changeAmount <= bs::Address::getNativeSegwitDustAmount()) {
            needChange = false;
            fee += changeAmount;
         }

         std::vector<std::shared_ptr<bs::sync::Wallet>> inputXbtWallets;
         for (const auto& walletId : walletIds) {
            const auto& wallet = getWalletById(walletId);
            if (wallet) {
               inputXbtWallets.push_back(wallet);
            } else {
               const auto& hdWallet = getHDWalletById(walletId);
               if (!hdWallet) {
                  logger_->warn("[WalletsAdapter::processPayin] failed to find "
                     "wallet {}", walletId);
                  sendResponse({}, {}, "invalid input wallets");
                  return;
               }
               const auto& xbtGroup = hdWallet->getGroup(hdWallet->getXBTGroupType());
               const auto& xbtLeaves = xbtGroup->getAllLeaves();
               inputXbtWallets.insert(inputXbtWallets.cend(), xbtLeaves.cbegin()
                  , xbtLeaves.cend());
            }
         }
         const auto& xbtWallet = inputXbtWallets[0];

         const auto& changeCb = [this, sendResponse, selectedInputs, fee
            , settlAddr, recVec, inputXbtWallets, xbtWallet]
            (const bs::Address& changeAddr)
         {
            auto txReq = std::make_shared<bs::core::wallet::TXSignRequest>(
               bs::sync::wallet::createTXRequest(inputXbtWallets, selectedInputs
                  , recVec, false, changeAddr, fee, false));

            const auto& cbResolvePubData = [this, settlAddr, sendResponse, txReq
               , xbtWallet, changeAddr]
               (bs::error::ErrorCode errCode, const Codec_SignerState::SignerState& state)
            {
               try {
                  txReq->armorySigner_.merge(state);
                  if (!changeAddr.empty()) {
                     xbtWallet->setAddressComment(changeAddr
                        , bs::sync::wallet::Comment::toString(bs::sync::wallet::Comment::ChangeAddress));
                  }
               } catch (const std::exception& e) {
                  sendResponse(settlAddr, {}, "signer merge failed");
                  return;
               }

               const auto& cbTXs = [sendResponse, txReq, settlAddr]
               (const std::vector<Tx>& txs)
               {
                  for (const auto& tx : txs) {
                     txReq->armorySigner_.addSupportingTx(tx);
                  }
                  if (!txReq->isValid()) {
                     sendResponse(settlAddr, {}, "invalid pay-in transaction");
                     return;
                  }
                  sendResponse(settlAddr, *txReq);
               };
               ArmoryMessage msg;
               auto msgReq = msg.mutable_get_txs_by_hash();
               for (unsigned i = 0; i < txReq->armorySigner_.getTxInCount(); i++) {
                  auto spender = txReq->armorySigner_.getSpender(i);
                  msgReq->add_tx_hashes(spender->getOutputHash().toBinStr());
               }
               Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}
                  , msg.SerializeAsString(), true };
               if (pushFill(envReq)) {
                  payinTXsCbMap_[envReq.id] = cbTXs;
               }
            };
            //resolve in all circumstances
            signerClient_->resolvePublicSpenders(*txReq, cbResolvePubData);
         };

         if (needChange) {
            xbtWallet->getNewIntAddress(changeCb);
         } else {
            changeCb({});
         }
      } catch (const std::exception& e) {
         sendResponse(settlAddr, {}, fmt::format("internal error: {}", e.what()));
         return;
      }
   };
#if 0 //old settlement code
   if (!group) {
      sendResponse({}, {}, "no settlement group in primary wallet");
      return true;
   }
   if (!settlLeaf) {
      sendResponse({}, {}, fmt::format("no settlement leaf for address {}"
         , ownAuthAddr.display()));
      return true;
   }
   settlLeaf->setSettlementID(settlementId, [this, cbSettlAddr](bool result)
   {
      if (!result) {
         sendResponse({}, {}, "failed to set settlement id");
         return;
      }
      const auto& counterPubKey = SecureBinaryData::fromString(request.counter_auth_pubkey());
      const bool myKeyFirst = false;
      priWallet->getSettlementPayinAddress(settlementId, counterPubKey, cbSettlAddr
         , myKeyFirst);
   });
#endif   //0
   const auto& counterPubKey = SecureBinaryData::fromString(request.counter_auth_pubkey());
   if (settlLeaf) {  // we're dealer
      settlLeaf->setSettlementID(settlementId, [sendResponse, cbSettlAddr, counterPubKey]
         (bool result, const SecureBinaryData& pubKey)
      {
         if (!result) {
            sendResponse({}, {}, "failed to set settlement id");
            return;
         }
         cbSettlAddr(bs::tradeutils::createEasySettlAddress(counterPubKey, pubKey));
      });
   }
   else {
      const auto& wallet = getWalletByAddress(ownAuthAddr);
      if (!wallet) {
         sendResponse({}, {}, "unknown auth address wallet");
         return true;
      }
      signerClient_->getAddressPubkey(wallet->walletId(), request.own_auth_address()
         , [sendResponse, cbSettlAddr, counterPubKey](const SecureBinaryData& pubKey)
      {
         if (pubKey.empty()) {
            sendResponse({}, {}, "no pubkey for auth address");
            return;
         }
         cbSettlAddr(bs::tradeutils::createEasySettlAddress(counterPubKey, pubKey));
      });
   }
   return true;
}

bool WalletsAdapter::processPayout(const bs::message::Envelope& env
   , const WalletsMessage_PayoutRequest& request)
{
   const auto& sendResponse = [this, env](const bs::Address& settlementAddr
      , const bs::core::wallet::TXSignRequest& txReq, const std::string& errorMsg = {})
   {
      WalletsMessage msg;
      auto msgResp = msg.mutable_xbt_tx_response();
      if (!settlementAddr.empty()) {
         msgResp->set_settlement_address(settlementAddr.display());
      }
      if (txReq.isValid()) {
         *msgResp->mutable_tx_request() = bs::signer::coreTxRequestToPb(txReq);
      }
      if (!errorMsg.empty()) {
         msgResp->set_error_text(errorMsg);
      }
      Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
   };

   const auto& priWallet = getPrimaryWallet();
   if (!priWallet) {
      sendResponse({}, {}, "no primary wallet");
      return true;
   }
   const auto group = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(
      priWallet->getGroup(bs::hd::BlockSettle_Settlement));
   if (!group) {
      sendResponse({}, {}, "no settlement group in primary wallet");
      return true;
   }
   bs::Address ownAuthAddr;
   try {
      ownAuthAddr = bs::Address::fromAddressString(request.own_auth_address());
   } catch (const std::exception&) {
      sendResponse({}, {}, "invalid own auth address");
      return true;
   }
   const auto& settlLeaf = group->getLeaf(ownAuthAddr);
   if (!settlLeaf) {
      sendResponse({}, {}, fmt::format("no settlement leaf for address {}"
         , ownAuthAddr.display()));
      return true;
   }

   const auto& settlementId = BinaryData::fromString(request.settlement_id());
   bs::Address recvAddr;
   if (request.recv_address().empty()) {
      const auto& xbtGroup = priWallet->getGroup(priWallet->getXBTGroupType());
      if (!xbtGroup) {
         logger_->error("[{}] no XBT group in primary wallet", __func__);
         return true;
      }
      const auto& leaves = xbtGroup->getAllLeaves();
      if (leaves.empty()) {
         logger_->error("[{}] no XBT leaves in primary wallet", __func__);
         return true;
      }
      const auto& xbtWallet = *leaves.cbegin();
      for (const auto& addr : xbtWallet->getIntAddressList()) {
         if (!isAddressUsed(addr, xbtWallet->walletIdInt())) {
            recvAddr = addr;
            break;
         }
      }
      if (recvAddr.empty()) {
         const auto& promRecv = std::make_shared<std::promise<bs::Address>>();
         auto futRecv = promRecv->get_future();
         xbtWallet->getNewIntAddress([promRecv](const bs::Address& addr) {
            promRecv->set_value(addr);
         });
         recvAddr = futRecv.get();
      }
      if (!recvAddr.empty()) {
         logger_->debug("[{}] obtain recvAddr: {}", __func__, recvAddr.display());
      }
   }
   else {
      try {
         recvAddr = bs::Address::fromAddressString(request.recv_address());
      } catch (const std::exception&) {
         logger_->error("[{}] invalid recv address", __func__);
         return true;
      }
   }
   if (recvAddr.empty()) {
      sendResponse({}, {}, "no receiving address");
      return true;
   }

#if 0 // old settlement code
   const auto& cbSettlAddr = [this, sendResponse, request, recvAddr]
      (const bs::Address& settlAddr)
   {
      if (settlAddr.empty()) {
         sendResponse({}, {}, "invalid settlement address");
         return;
      }

      const auto& payinTxHash = BinaryData::fromString(request.payin_hash());
      auto payinUTXO = bs::tradeutils::getInputFromTX(settlAddr, payinTxHash
         , 0, bs::XBTAmount{ request.amount() });

      const auto& txReq = bs::tradeutils::createPayoutTXRequest(
         payinUTXO, recvAddr, settlementFee_, topBlock_);
      sendResponse(settlAddr, txReq);
   };
   settlLeaf->setSettlementID(settlementId, [sendResponse, cbSettlAddr, priWallet
      , request, settlementId](bool result, const SecureBinaryData&)
   {
      if (!result) {
         sendResponse({}, {}, "failed to set settlement id");
         return;
      }
      const auto& cpAuthPubKey = BinaryData::fromString(request.counter_auth_pubkey());
      priWallet->getSettlementPayinAddress(settlementId, cpAuthPubKey, cbSettlAddr);
   });
#endif   //0

   const auto& counterPubKey = SecureBinaryData::fromString(request.counter_auth_pubkey());
   const auto& createPayout = [this, sendResponse, request, recvAddr]
      (const bs::Address& settlAddr, SecureBinaryData ownPubKey
         , const std::shared_ptr<bs::sync::Wallet>& authWallet = {})
   {
      if (settlAddr.empty()) {
         sendResponse({}, {}, "invalid settlement address");
         return;
      }
      const auto& payinTxHash = BinaryData::fromString(request.payin_hash());
      const auto& asset = std::make_shared<AssetEntry_Single>(0, BinaryData(), ownPubKey, nullptr);
      const auto& addrSingle = std::make_shared<AddressEntry_P2WPKH>(asset);
      const auto& addrP2shSingle = std::make_shared<AddressEntry_P2SH>(addrSingle);
      const auto& payoutUtxo = UTXO(request.amount(), UINT32_MAX, UINT32_MAX, 0
         , payinTxHash, addrP2shSingle->getPreimage());

      auto txReq = bs::tradeutils::createPayoutTXRequest(
         payoutUtxo, recvAddr, settlementFee_, topBlock_);
      if (authWallet) {
         txReq.walletIds = { authWallet->walletId() };
      }
      sendResponse(settlAddr, txReq);
   };

   if (settlLeaf) {  // we're dealer
      settlLeaf->setSettlementID(settlementId, [sendResponse, createPayout, counterPubKey]
      (bool result, const SecureBinaryData& pubKey)
      {
         if (!result) {
            sendResponse({}, {}, "failed to set settlement id");
            return;
         }
         createPayout(bs::tradeutils::createEasySettlAddress(counterPubKey, pubKey), pubKey);
      });
   } else {
      const auto& wallet = getWalletByAddress(ownAuthAddr);
      if (!wallet) {
         sendResponse({}, {}, "unknown auth address wallet");
         return true;
      }
      signerClient_->getAddressPubkey(wallet->walletId(), request.own_auth_address()
         , [sendResponse, createPayout, counterPubKey, wallet]
         (const SecureBinaryData& pubKey)
      {
         if (pubKey.empty()) {
            sendResponse({}, {}, "no pubkey for auth address");
            return;
         }
         createPayout(bs::tradeutils::createEasySettlAddress(counterPubKey, pubKey)
            , pubKey, wallet);
      });
   }
   return true;
}
