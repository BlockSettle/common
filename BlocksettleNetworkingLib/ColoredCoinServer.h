/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#ifndef COLORED_COIN_SERVER_H
#define COLORED_COIN_SERVER_H

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "ColoredCoinLogic.h"
#include "DataConnectionListener.h"
#include "ServerConnectionListener.h"
#include "DispatchQueue.h"
#include "BIP15xHelpers.h"

namespace spdlog {
   class logger;
}
class ArmoryConnection;
class CcTrackerImpl;
class CcTrackerSrvImpl;
class DataConnection;
class ServerConnection;

namespace bs {
   namespace tracker_server {
      class Request_RegisterCc;
      class Request_ParseCcTxCandidate;
      class Response_ParseCcCandidateTxResult;
      class Response_UpdateCcSnapshot;
      class Response_UpdateCcZcSnapshot;
   }
}

class CcTrackerClient : public DataConnectionListener
{
public:
   CcTrackerClient(const std::shared_ptr<spdlog::logger> &logger);

   ~CcTrackerClient() override;

   static std::unique_ptr<ColoredCoinTrackerInterface> createClient(
      const std::shared_ptr<CcTrackerClient> &parent, uint64_t coinsPerShare);

   void openConnection(const std::string &host, const std::string &port
      , const bs::network::BIP15xNewKeyCb &);

   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

private:
   friend class CcTrackerImpl;

   enum class State
   {
      Offline,
      Connecting,
      Connected,
      Restarting,
   };

   static std::string stateName(State state);
   void setState(State state);

   void addClient(CcTrackerImpl *client);
   void removeClient(CcTrackerImpl *client);

   void registerClient(CcTrackerImpl *client);
   void registerClients();
   void scheduleRestart();
   void reconnect();

   void parseCcCandidateTx(const std::shared_ptr<ColoredCoinSnapshot> &
      , const std::shared_ptr<ColoredCoinZCSnapshot>&, const Tx &, int id);

   void processUpdateCcSnapshot(const bs::tracker_server::Response_UpdateCcSnapshot &response);
   void processUpdateCcZcSnapshot(const bs::tracker_server::Response_UpdateCcZcSnapshot &response);
   void processParseCcCandidateTx(const bs::tracker_server::Response_ParseCcCandidateTxResult &);

   std::shared_ptr<spdlog::logger> logger_;
   std::unique_ptr<DataConnection> connection_;

   std::set<CcTrackerImpl*> clients_;
   std::map<int, CcTrackerImpl*> clientsById_;

   DispatchQueue dispatchQueue_;
   std::thread dispatchThread_;
   std::atomic_int nextId_{};

   std::string host_;
   std::string port_;
   bs::network::BIP15xNewKeyCb   newKeyCb_{ nullptr };
   State state_{State::Offline};
   std::chrono::steady_clock::time_point nextRestart_{};
};

class CcTrackerServer : public ServerConnectionListener
{
public:
   CcTrackerServer(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<ArmoryConnection> &armory, const std::shared_ptr<ServerConnection> &server);

   ~CcTrackerServer() override;

   void OnDataFromClient(const std::string& clientId, const std::string& data) override;

   void OnClientConnected(const std::string& clientId, const Details &details) override;
   void OnClientDisconnected(const std::string& clientId) override;

private:
   friend class CcTrackerSrvImpl;

   struct ClientData
   {
      // key is id from registration request
      std::map<int, std::shared_ptr<CcTrackerSrvImpl>> trackers;

      std::string clientId;
   };

   void processRegisterCc(ClientData &client, const bs::tracker_server::Request_RegisterCc &request);
   void processParseTxCandidate(ClientData &client, const bs::tracker_server::Request_ParseCcTxCandidate &);

   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<ArmoryConnection> armory_;

   std::shared_ptr<ServerConnection> server_{};

   std::map<std::string, ClientData> connectedClients_;

   // key is serialized bs.tracker_server.TrackerKey (must be valid)
   std::map<std::string, std::weak_ptr<CcTrackerSrvImpl>> trackers_;

   DispatchQueue dispatchQueue_;
   std::thread dispatchThread_;

   uint64_t startedTrackerCount_{};
};


class CCTrackerClientFactoryConnected : public CCTrackerClientFactory
{
public:
   CCTrackerClientFactoryConnected(const std::shared_ptr<spdlog::logger> &
      , const std::string &host, const std::string &port
      , const std::string &pubKey);
   ~CCTrackerClientFactoryConnected() override;

   std::shared_ptr<ColoredCoinTrackerClientIface> createClient(uint32_t lotSize) override;

private:
   std::shared_ptr<CcTrackerClient> trackerClient_;
};

#endif
