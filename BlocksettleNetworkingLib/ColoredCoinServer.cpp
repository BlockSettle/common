/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#include "ColoredCoinServer.h"

#include <spdlog/spdlog.h>

#include "Bip15xDataConnection.h"
#include "ColoredCoinCache.h"
#include "ColoredCoinLogic.h"
#include "DataConnection.h"
#include "DispatchQueue.h"
#include "FutureValue.h"
#include "ServerConnection.h"
#include "StringUtils.h"
#include "TransportBIP15x.h"
#include "TransportBIP15xServer.h"
#include "WsDataConnection.h"

#include "tracker_server.pb.h"

namespace {

   bool isValidTrackerAddr(const std::string &str)
   {
      try {
         auto addr = bs::Address::fromAddressString(str);
         // Limit to P2WPKH only
         if (addr.getType() != AddressEntryType_P2WPKH) {
            return false;
         }
      } catch (...) {
         return false;
      }
      return true;
   }

   bool isValidTrackerKey(const bs::tracker_server::TrackerKey &trackerKey)
   {
      if (trackerKey.coins_per_share() <= 0 || trackerKey.coins_per_share() > 100000000) {
         return false;
      }
      if (trackerKey.origin_addresses().empty()) {
         return false;
      }
      bool result = std::is_sorted(trackerKey.origin_addresses().begin(), trackerKey.origin_addresses().end());
      if (!result) {
         return false;
      }
      result = std::is_sorted(trackerKey.revoked_addresses().begin(), trackerKey.revoked_addresses().end());
      if (!result) {
         return false;
      }
      for (const auto &addr : trackerKey.origin_addresses()) {
         if (!isValidTrackerAddr(addr)) {
            return false;
         }
      }
      for (const auto &addr : trackerKey.revoked_addresses()) {
         if (!isValidTrackerAddr(addr)) {
            return false;
         }
      }
      return true;
   }

} // namespace

class CcTrackerImpl : public ColoredCoinTrackerInterface
{
public:
   ~CcTrackerImpl() override
   {
      auto parent = parent_.lock();
      if (parent) {
         parent->removeClient(this);
      }
   }

   void addOriginAddress(const bs::Address &addr) override
   {
      if (isOnline_.load()) {
         throw std::logic_error("addOriginAddress is not implemented after goOnline call");
      }
      originAddresses_.push_back(addr);
   }

   void addRevocationAddress(const bs::Address &addr) override
   {
      if (isOnline_.load()) {
         throw std::logic_error("addOriginAddress is not implemented after goOnline call");
      }
      revocationAddresses_.push_back(addr);
   }

   bool goOnline() override
   {
      auto parent = parent_.lock();
      assert(parent);
      if (isOnline_.load()) {
         throw std::logic_error("goOnline was already called");
      }
      isOnline_ = true;
      parent->addClient(this);
      return true;
   }

   std::shared_ptr<ColoredCoinSnapshot> snapshot() const override
   {
      return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
   }

   std::shared_ptr<ColoredCoinZCSnapshot> zcSnapshot() const override
   {
      return std::atomic_load_explicit(&zcSnapshot_, std::memory_order_acquire);
   }

   void setSnapshotUpdatedCb(SnapshotUpdatedCb cb) override
   {
      snapshotUpdatedCb_ = std::move(cb);
   }

   void setZcSnapshotUpdatedCb(SnapshotUpdatedCb cb) override
   {
      zcSnapshotUpdatedCb_ = std::move(cb);
   }

   void setReadyCb(SnapshotUpdatedCb cb) override
   {
      readyCb_ = std::move(cb);
   }

   void parseCcCandidateTx(const std::shared_ptr<ColoredCoinSnapshot> &s
      , const std::shared_ptr<ColoredCoinZCSnapshot>&zcS, const Tx &tx
      , const CcTxCandidateCb &cb) const override
   {
      parseCcTxCb_ = cb;
      auto parent = parent_.lock();
      assert(parent);
      parent->parseCcCandidateTx(s, zcS, tx, id_);
   }

   bool ready() const override
   {
      return ready_.load();
   }

   void parseCcCandidateTxResult(const CcTxCandidate &result)
   {
      if (parseCcTxCb_ != nullptr) {
         parseCcTxCb_(result);
      }
      parseCcTxCb_ = nullptr;
   }

   std::weak_ptr<CcTrackerClient> parent_;
   uint64_t coinsPerShare_;
   std::atomic_bool isOnline_{false};
   std::vector<bs::Address> originAddresses_;
   std::vector<bs::Address> revocationAddresses_;
   bool registered_{false};
   int id_{};

   std::shared_ptr<ColoredCoinSnapshot> snapshot_;
   std::shared_ptr<ColoredCoinZCSnapshot> zcSnapshot_;

   SnapshotUpdatedCb snapshotUpdatedCb_;
   SnapshotUpdatedCb zcSnapshotUpdatedCb_;
   SnapshotUpdatedCb readyCb_;
   mutable CcTxCandidateCb parseCcTxCb_{ nullptr };
   std::atomic_bool ready_{false};
};

class CcTrackerSrvImpl : public ColoredCoinTracker
{
public:
   struct Client
   {
      std::string clientId;
      int id{};

      bool operator<(const Client &other) const {
         if (clientId != other.clientId) {
            return clientId < other.clientId;
         }
         return id < other.id;
      }
   };

   CcTrackerSrvImpl(CcTrackerServer *parent
      , uint64_t coinsPerShare
      , std::shared_ptr<ArmoryConnection> connPtr
      , uint64_t index)
      : ColoredCoinTracker(coinsPerShare, connPtr)
      , parent_(parent)
      , index_(index)
   {
      SPDLOG_LOGGER_DEBUG(parent_->logger_, "starting new tracker ({}), coinsPerShare: {}", index_, coinsPerShare);
   }

   ~CcTrackerSrvImpl() override
   {
      SPDLOG_LOGGER_DEBUG(parent_->logger_, "stopping new tracker ({})", index_);
   }

   void snapshotUpdated() override
   {
      SPDLOG_LOGGER_DEBUG(parent_->logger_, "snapshots updated, {}", index_);

      parent_->dispatchQueue_.dispatch([this] {
         auto s = snapshot();
         ccSnapshot_ = serializeColoredCoinSnapshot(s);
         for (const auto &client : clients_) {
            sendSnapshot(client);
         }
      });
   }

   virtual void zcSnapshotUpdated() override
   {
      SPDLOG_LOGGER_DEBUG(parent_->logger_, "zc snapshots updated, {}", index_);

      parent_->dispatchQueue_.dispatch([this] {
         auto s = zcSnapshot();
         ccZcSnapshot_ = serializeColoredCoinZcSnapshot(s);
         for (const auto &client : clients_) {
            sendZcSnapshot(client);
         }
      });
   }

   void sendSnapshot(const Client &client) {
      bs::tracker_server::Response response;
      auto d = response.mutable_update_cc_snapshot();
      d->set_id(client.id);
      d->set_data(ccSnapshot_);
      parent_->server_->SendDataToClient(client.clientId, response.SerializeAsString());
   }

   void sendZcSnapshot(const Client &client) {
      bs::tracker_server::Response response;
      auto d = response.mutable_update_cc_zc_snapshot();
      d->set_id(client.id);
      d->set_data(ccZcSnapshot_);
      parent_->server_->SendDataToClient(client.clientId, response.SerializeAsString());
   }

   std::string ccSnapshot_;
   std::string ccZcSnapshot_;
   std::set<Client> clients_;
   CcTrackerServer *parent_{};
   const uint64_t index_;
};


CcTrackerClient::CcTrackerClient(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{
   dispatchThread_ = std::thread([this]{
      while (!dispatchQueue_.done()) {
         dispatchQueue_.tryProcess(std::chrono::seconds(10));

         if (state_ == State::Restarting && std::chrono::steady_clock::now() > nextRestart_) {
            reconnect();
         }
      }
   });
}

CcTrackerClient::~CcTrackerClient()
{
   dispatchQueue_.quit();
   dispatchThread_.join();
   connection_.reset();
}

std::unique_ptr<ColoredCoinTrackerInterface> CcTrackerClient::createClient(
   const std::shared_ptr<CcTrackerClient> &parent, uint64_t coinsPerShare)
{
   auto id = ++parent->nextId_;

   auto client = std::make_unique<CcTrackerImpl>();
   client->parent_ = std::weak_ptr<CcTrackerClient>(parent);
   client->coinsPerShare_ = coinsPerShare;
   client->id_ = id;

   return std::move(client);
}

void CcTrackerClient::openConnection(const std::string &host, const std::string &port
   , const bs::network::BIP15xNewKeyCb &newKeyCb)
{
   dispatchQueue_.dispatch([this, host, port, newKeyCb = std::move(newKeyCb)] {
      assert(state_ == State::Offline);
      host_ = host;
      port_ = port;
      newKeyCb_ = std::move(newKeyCb);
      reconnect();
   });
}

void CcTrackerClient::OnDataReceived(const std::string &data)
{
   bs::tracker_server::Response response;
   bool result = response.ParseFromString(data);
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "can't parse bs::tracker_server::Response");
      return;
   }

   dispatchQueue_.dispatch([this, response = std::move(response)] {
      switch (response.data_case()) {
         case bs::tracker_server::Response::kUpdateCcSnapshot:
            processUpdateCcSnapshot(response.update_cc_snapshot());
            return;
         case bs::tracker_server::Response::kUpdateCcZcSnapshot:
            processUpdateCcZcSnapshot(response.update_cc_zc_snapshot());
            return;
         case bs::tracker_server::Response::kParseTxCandidate:
            processParseCcCandidateTx(response.parse_tx_candidate());
            return;
         case bs::tracker_server::Response::DATA_NOT_SET:
            SPDLOG_LOGGER_ERROR(logger_, "got invalid empty response from server");
            return;
      }
      SPDLOG_LOGGER_CRITICAL(logger_, "unhandled request detected!");
   });
}

void CcTrackerClient::OnConnected()
{
   dispatchQueue_.dispatch([this] {
      setState(State::Connected);
      registerClients();
   });
}

void CcTrackerClient::OnDisconnected()
{
   SPDLOG_LOGGER_DEBUG(logger_, "disconnected from CC server");
   scheduleRestart();
}

void CcTrackerClient::OnError(DataConnectionListener::DataConnectionError errorCode)
{
   SPDLOG_LOGGER_ERROR(logger_, "connection to CC server failed");
   scheduleRestart();
}

std::string CcTrackerClient::stateName(CcTrackerClient::State state)
{
   switch (state) {
      case State::Offline:       return "Offline";
      case State::Connecting:    return "Connecting";
      case State::Connected:     return "Connected";
      case State::Restarting:    return "Restarting";
   }
   assert(false);
   return "Unknown";
}

void CcTrackerClient::setState(CcTrackerClient::State state)
{
   state_ = state;
   SPDLOG_LOGGER_DEBUG(logger_, "switch state to {}", stateName(state));
}

void CcTrackerClient::addClient(CcTrackerImpl *client)
{
   dispatchQueue_.dispatch([this, client] {
      clients_.insert(client);
      clientsById_[client->id_] = client;

      if (state_ == State::Connected) {
         registerClient(client);
      }
   });
}

void CcTrackerClient::removeClient(CcTrackerImpl *client)
{
   dispatchQueue_.dispatch([this, client, id = client->id_] {
      // Do not dereference client here as it's already dead!
      clientsById_.erase(id);
      clients_.erase(client);
   });
}

void CcTrackerClient::registerClient(CcTrackerImpl *client)
{
   assert(!client->registered_ && state_ == State::Connected);
   client->registered_ = true;

   bs::tracker_server::Request request;
   auto d = request.mutable_register_cc();

   d->set_id(client->id_);
   d->mutable_tracker_key()->set_coins_per_share(client->coinsPerShare_);
   std::set<std::string> originAddresses;
   for (const auto &addr : client->originAddresses_) {
      originAddresses.insert(addr.display());
   }
   std::set<std::string> revocationAddresses;
   for (const auto &addr : client->revocationAddresses_) {
      revocationAddresses.insert(addr.display());
   }
   for (const auto &addr : originAddresses) {
      d->mutable_tracker_key()->add_origin_addresses(addr);
   }
   for (const auto &addr : revocationAddresses) {
      d->mutable_tracker_key()->add_revoked_addresses(addr);
   }

   assert(isValidTrackerKey(d->tracker_key()));

   connection_->send(request.SerializeAsString());
}

void CcTrackerClient::registerClients()
{
   for (const auto &client : clients_) {
      if (!client->registered_) {
         registerClient(client);
      }
   }
}

void CcTrackerClient::scheduleRestart()
{
   dispatchQueue_.dispatch([this] {
      SPDLOG_LOGGER_DEBUG(logger_, "schedule restart in next 30 seconds...");
      nextRestart_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);

      if (state_ != State::Restarting) {
         setState(State::Restarting);
         for (const auto &item : clients_) {
            item->registered_ = false;
         }
      }
   });
}

void CcTrackerClient::reconnect()
{
   SPDLOG_LOGGER_DEBUG(logger_, "reconnect...");
   setState(State::Connecting);

   bs::network::BIP15xParams params;
   params.ephemeralPeers = true;
   const auto &transport = std::make_shared<bs::network::TransportBIP15xClient>(logger_, params);
   transport->setKeyCb(newKeyCb_);
   auto wsConn = std::make_unique<WsDataConnection>(logger_, WsDataConnectionParams{});
   connection_ = std::make_unique<Bip15xDataConnection>(logger_, std::move(wsConn), transport);
   bool result = connection_->openConnection(host_, port_, this);
   assert(result);
}

void CcTrackerClient::parseCcCandidateTx(const std::shared_ptr<ColoredCoinSnapshot> &s
   , const std::shared_ptr<ColoredCoinZCSnapshot> &zcS, const Tx &tx, int id)
{
   bs::tracker_server::Request request;
   auto d = request.mutable_tx_candidate();
   d->set_id(id);
   d->set_cc_snapshot(serializeColoredCoinSnapshot(s));
   d->set_zc_snapshot(serializeColoredCoinZcSnapshot(zcS));
   d->set_tx(tx.serialize().toBinStr());
   connection_->send(request.SerializeAsString());
}

void CcTrackerClient::processUpdateCcSnapshot(const bs::tracker_server::Response_UpdateCcSnapshot &response)
{
   auto it = clientsById_.find(response.id());
   if (it == clientsById_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unknown id");
      return;
   }
   CcTrackerImpl *tracker = it->second;
   auto snapshot = deserializeColoredCoinSnapshot(response.data());
   std::atomic_store_explicit(&tracker->snapshot_, snapshot, std::memory_order_release);
   if (tracker->snapshotUpdatedCb_) {
      tracker->snapshotUpdatedCb_();
   }

   if (!tracker->ready_.load()) {
      tracker->ready_.store(true);
      if (tracker->readyCb_) {
         tracker->readyCb_();
      }
   }
}

void CcTrackerClient::processUpdateCcZcSnapshot(const bs::tracker_server::Response_UpdateCcZcSnapshot &response)
{
   auto it = clientsById_.find(response.id());
   if (it == clientsById_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unknown id");
      return;
   }
   CcTrackerImpl *tracker = it->second;
   auto snapshot = deserializeColoredCoinZcSnapshot(response.data());
   std::atomic_store_explicit(&tracker->zcSnapshot_, snapshot, std::memory_order_release);
   if (tracker->zcSnapshotUpdatedCb_) {
      tracker->zcSnapshotUpdatedCb_();
   }
}

void CcTrackerClient::processParseCcCandidateTx(const bs::tracker_server::Response_ParseCcCandidateTxResult &response)
{
   auto it = clientsById_.find(response.id());
   if (it == clientsById_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unknown id");
      return;
   }
   CcTrackerImpl *tracker = it->second;
   CcTxCandidate result;
   for (int i = 0; i < response.cc_by_addr_size(); ++i) {
      const auto &avPair = response.cc_by_addr(i);
      result.ccPerAddr_[BinaryData::fromString(avPair.address())] = avPair.value();
   }
   for (int i = 0; i < response.xbt_by_addr_size(); ++i) {
      const auto &avPair = response.xbt_by_addr(i);
      result.xbtPerAddr_[BinaryData::fromString(avPair.address())] = avPair.value();
   }
   result.totalCcRedeemed_ = response.total_cc_redeemed();
   result.totalCcSpent_ = response.total_cc_spent();
   result.totalXbtSpent_ = response.total_xbt_spent();
   result.isValidCcTx_ = response.is_valid();
   tracker->parseCcCandidateTxResult(result);
}


CcTrackerServer::CcTrackerServer(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ArmoryConnection> &armory, const std::shared_ptr<ServerConnection> &server)
   : logger_(logger)
   , armory_(armory)
   , server_(server)
{
   dispatchThread_ = std::thread([this]{
      while (!dispatchQueue_.done()) {
         dispatchQueue_.tryProcess();
      }
   });
}

CcTrackerServer::~CcTrackerServer()
{
   dispatchQueue_.quit();
   dispatchThread_.join();
}

void CcTrackerServer::OnDataFromClient(const std::string &clientId, const std::string &data)
{
   bs::tracker_server::Request request;
   bool result = request.ParseFromString(data);
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "can't parse bs::tracker_server::Request from client {}", bs::toHex(clientId));
      return;
   }

   dispatchQueue_.dispatch([this, clientId, request = std::move(request)] {
      auto &client = connectedClients_.at(clientId);

      switch (request.data_case()) {
         case bs::tracker_server::Request::kRegisterCc:
            processRegisterCc(client, request.register_cc());
            return;
         case bs::tracker_server::Request::kTxCandidate:
            processParseTxCandidate(client, request.tx_candidate());
            return;
         case bs::tracker_server::Request::DATA_NOT_SET:
            SPDLOG_LOGGER_ERROR(logger_, "invalid request from client {}", bs::toHex(clientId));
            return;
      }

      // Should not happen unless there is some new data_case
      SPDLOG_LOGGER_CRITICAL(logger_, "unhandled request detected!");
   });
}

void CcTrackerServer::OnClientConnected(const std::string &clientId, const Details &details)
{
   SPDLOG_LOGGER_INFO(logger_, "new client connected: {}", bs::toHex(clientId));
   dispatchQueue_.dispatch([this, clientId] {
      auto &client = connectedClients_[clientId];
      client.clientId = clientId;
   });
}

void CcTrackerServer::OnClientDisconnected(const std::string &clientId)
{
   SPDLOG_LOGGER_INFO(logger_, "client disconnected: {}", bs::toHex(clientId));
   dispatchQueue_.dispatch([this, clientId] {
      auto it = connectedClients_.find(clientId);
      assert(it != connectedClients_.end());

      auto &client = it->second;
      for (const auto &item : client.trackers) {
         auto c = CcTrackerSrvImpl::Client{client.clientId, item.first};
         size_t count = item.second->clients_.erase(c);
         assert(count == 1);
      }

      connectedClients_.erase(it);
   });
}

void CcTrackerServer::processRegisterCc(CcTrackerServer::ClientData &client, const bs::tracker_server::Request_RegisterCc &request)
{
   auto it = client.trackers.find(request.id());
   if (it != client.trackers.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "request with duplicated tracker id is ignored");
      return;
   }

   if (!isValidTrackerKey(request.tracker_key())) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid tracker_key");
      return;
   }

   auto trackerKey = request.tracker_key().SerializeAsString();
   auto &trackerWeakPtr = trackers_[trackerKey];
   auto trackerPtr = trackerWeakPtr.lock();

   auto c = CcTrackerSrvImpl::Client{client.clientId, request.id()};

   if (!trackerPtr) {
      startedTrackerCount_ += 1;
      auto startedTrackerIndex = startedTrackerCount_;

      trackerPtr = std::make_shared<CcTrackerSrvImpl>(this, request.tracker_key().coins_per_share(), armory_, startedTrackerIndex);
      SPDLOG_LOGGER_INFO(logger_, "create new tracker {}...", trackerPtr->index_);
      for (const auto &addr : request.tracker_key().origin_addresses()) {
         SPDLOG_LOGGER_INFO(logger_, "add origin address {}", addr);
         trackerPtr->addOriginAddress(bs::Address::fromAddressString(addr));
      }
      for (const auto &addr : request.tracker_key().revoked_addresses()) {
         SPDLOG_LOGGER_INFO(logger_, "add revoked address {}", addr);
         trackerPtr->addRevocationAddress(bs::Address::fromAddressString(addr));
      }
      SPDLOG_LOGGER_INFO(logger_, "new tracker ({}) created", trackerPtr->index_);

      std::thread regThread([this, trackerPtr] {
         SPDLOG_LOGGER_INFO(logger_, "activating new tracker ({}) in background...", trackerPtr->index_);
         bool result = trackerPtr->goOnline();

         if (!result) {
            // quit server for now, clients expected to reconnect to server
            SPDLOG_LOGGER_CRITICAL(logger_, "goOnline failed for tracker {}, quit!", trackerPtr->index_);
            std::exit(EXIT_FAILURE);
         }

         SPDLOG_LOGGER_INFO(logger_, "new tracker ({}) started successfully", trackerPtr->index_);
      });
      regThread.detach();

      trackers_[trackerKey] = trackerPtr;
   } else {
      SPDLOG_LOGGER_INFO(logger_, "reuse active tracker {} for new client", trackerPtr->index_);
   }

   client.trackers[request.id()] = trackerPtr;

   trackerPtr->clients_.insert(c);
   trackerPtr->sendSnapshot(c);
   trackerPtr->sendZcSnapshot(c);
}

void CcTrackerServer::processParseTxCandidate(ClientData &client
   , const bs::tracker_server::Request_ParseCcTxCandidate &request)
{
   auto it = client.trackers.find(request.id());
   if (it == client.trackers.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to find tracker for id {}", request.id());
      return;
   }
   const auto &tracker = it->second;
   const auto &ccSnap = deserializeColoredCoinSnapshot(request.cc_snapshot());
   const auto &zcSnap = deserializeColoredCoinZcSnapshot(request.zc_snapshot());
   const auto &result = tracker->parseCcCandidateTx(ccSnap, zcSnap, Tx(BinaryData::fromString(request.tx())));

   bs::tracker_server::Response response;
   auto d = response.mutable_parse_tx_candidate();
   d->set_id(request.id());
   for (const auto &addrPair : result.ccPerAddr_) {
      auto avPair = d->add_cc_by_addr();
      avPair->set_address(addrPair.first.toBinStr());
      avPair->set_value(addrPair.second);
   }
   for (const auto &addrPair : result.xbtPerAddr_) {
      auto avPair = d->add_xbt_by_addr();
      avPair->set_address(addrPair.first.toBinStr());
      avPair->set_value(addrPair.second);
   }
   d->set_total_cc_redeemed(result.totalCcRedeemed_);
   d->set_total_cc_spent(result.totalCcSpent_);
   d->set_total_xbt_spent(result.totalXbtSpent_);
   d->set_is_valid(result.isValidCcTx_);
   server_->SendDataToClient(client.clientId, response.SerializeAsString());
}


CCTrackerClientFactoryConnected::CCTrackerClientFactoryConnected(const std::shared_ptr<spdlog::logger> &logger
   , const std::string &host, const std::string &port, const std::string &pubKey)
{
   auto ccTrackerKeyCb = [pubKey](const std::string &oldKey, const std::string &newKey
      , const std::string& srvAddrPort, const std::shared_ptr<FutureValue<bool>> &prompt)
   {
      prompt->setValue((newKey == pubKey));
   };
   trackerClient_ = std::make_shared<CcTrackerClient>(logger);
   trackerClient_->openConnection(host, port, ccTrackerKeyCb);
}

CCTrackerClientFactoryConnected::~CCTrackerClientFactoryConnected() = default;

std::shared_ptr<ColoredCoinTrackerClientIface> CCTrackerClientFactoryConnected::createClient(uint32_t lotSize)
{
   auto trackerClient = CcTrackerClient::createClient(trackerClient_, lotSize);
   return std::make_shared<ColoredCoinTrackerClient>(std::move(trackerClient));
}
