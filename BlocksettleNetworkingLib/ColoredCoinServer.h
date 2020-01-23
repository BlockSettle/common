/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
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

#include "DataConnectionListener.h"
#include "ServerConnectionListener.h"
#include "DispatchQueue.h"
#include "ZMQ_BIP15X_Helpers.h"

namespace spdlog {
   class logger;
}

class ArmoryConnection;
class CcTrackerImpl;
class CcTrackerSrvImpl;
class ColoredCoinTrackerInterface;
class ZmqBIP15XDataConnection;
class ZmqBIP15XServerConnection;
class ZmqContext;

namespace bs {
   namespace tracker_server {
      class Request_RegisterCc;
      class Response_UpdateCcSnapshot;
      class Response_UpdateCcZcSnapshot;
   }
}

class CcTrackerClient : public DataConnectionListener
{
public:
   CcTrackerClient(const std::shared_ptr<spdlog::logger> &logger);

   ~CcTrackerClient() override;

   std::unique_ptr<ColoredCoinTrackerInterface> createClient(uint64_t coinsPerShare);

   void openConnection(const std::string &host, const std::string &port, ZmqBipNewKeyCb newKeyCb);

   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

private:
   friend class CcTrackerImpl;

   void registerClient(CcTrackerImpl *client);
   void removeClient(CcTrackerImpl *client);

   void processUpdateCcSnapshot(const bs::tracker_server::Response_UpdateCcSnapshot &response);
   void processUpdateCcZcSnapshot(const bs::tracker_server::Response_UpdateCcZcSnapshot &response);

   std::shared_ptr<spdlog::logger> logger_;

   std::set<CcTrackerImpl*> clients_;
   std::map<int, CcTrackerImpl*> clientsById_;

   std::unique_ptr<ZmqBIP15XDataConnection> connection_;

   std::atomic_int nextId_{};

   DispatchQueue dispatchQueue_;
   std::thread dispatchThread_;

};

class CcTrackerServer : public ServerConnectionListener
{
public:
   CcTrackerServer(const std::shared_ptr<spdlog::logger> &logger, const std::shared_ptr<ArmoryConnection> &armory);

   ~CcTrackerServer() override;

   bool startServer(const std::string &host, const std::string &port
      , const std::shared_ptr<ZmqContext> &context, const std::string &ownKeyFileDir
      , const std::string &ownKeyFileName);

   void OnDataFromClient(const std::string& clientId, const std::string& data) override;

   void OnClientConnected(const std::string& clientId) override;
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

   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<ArmoryConnection> armory_;

   std::unique_ptr<ZmqBIP15XServerConnection> server_;

   std::map<std::string, ClientData> connectedClients_;

   // key is serialized bs.tracker_server.TrackerKey (must be valid)
   std::map<std::string, std::weak_ptr<CcTrackerSrvImpl>> trackers_;

   DispatchQueue dispatchQueue_;
   std::thread dispatchThread_;

   uint64_t startedTrackerCount_{};

};

#endif
