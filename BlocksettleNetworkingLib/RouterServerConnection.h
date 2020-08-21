/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef ROUTER_SERVER_CONNECTION_H
#define ROUTER_SERVER_CONNECTION_H

#include "ServerConnection.h"

#include <mutex>
#include <map>
#include <vector>

namespace spdlog {
   class logger;
}

struct RouterServerConnectionParams
{
   struct Server
   {
      std::string host;
      std::string port;
      std::shared_ptr<ServerConnection> server;
   };

   std::vector<Server> servers;
};

class RouterServerListener;

class RouterServerConnection : public ServerConnection
{
public:
   RouterServerConnection(const std::shared_ptr<spdlog::logger>& logger, RouterServerConnectionParams params);
   ~RouterServerConnection() override;

   RouterServerConnection(const RouterServerConnection&) = delete;
   RouterServerConnection& operator = (const RouterServerConnection&) = delete;
   RouterServerConnection(RouterServerConnection&&) = delete;
   RouterServerConnection& operator = (RouterServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   bool SendDataToClient(const std::string& clientId, const std::string& data) override;
   bool SendDataToAllClients(const std::string&) override;

private:
   friend class RouterServerListener;

   std::shared_ptr<spdlog::logger>  logger_;
   const RouterServerConnectionParams params_;

   ServerConnectionListener *listener_{};

   std::recursive_mutex mutex_;

   std::vector<std::shared_ptr<RouterServerListener>> listeners_;

};

#endif // ROUTER_SERVER_CONNECTION_H
