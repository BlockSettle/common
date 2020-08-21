/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "RouterServerConnection.h"

#include "ThreadName.h"

#include <random>
#include <spdlog/spdlog.h>

namespace {

   std::string getRouterClientId(uint8_t index, const std::string &clientId)
   {
      std::string result;
      result.push_back(static_cast<char>(index));
      result += clientId;
      return result;
   }

   uint8_t getIndexFromRouterClientId(const std::string &clientId)
   {
      return static_cast<uint8_t>(clientId.at(0));
   }

   std::string getClientIdFromRouterClientId(const std::string &clientId)
   {
      return clientId.substr(1);
   }

} // namespace

class RouterServerListener : public ServerConnectionListener
{
public:
   void OnDataFromClient(const std::string& clientId, const std::string& data) override
   {
      listener_->OnDataFromClient(getRouterClientId(index_, clientId), data);
   }

   void OnClientConnected(const std::string& clientId, const Details &details) override
   {
      listener_->OnClientConnected(getRouterClientId(index_, clientId), details);
   }

   void OnClientDisconnected(const std::string& clientId) override
   {
      listener_->OnClientDisconnected(getRouterClientId(index_, clientId));
   }

   void onClientError(const std::string &clientId, ClientError error, const Details &details) override
   {
      listener_->onClientError(getRouterClientId(index_, clientId), error, details);
   }

   ServerConnectionListener *listener_{};
   uint8_t index_{};
};

RouterServerConnection::RouterServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , RouterServerConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
{
}

RouterServerConnection::~RouterServerConnection() = default;

bool RouterServerConnection::BindConnection(const std::string& host , const std::string& port
   , ServerConnectionListener* listener)
{
   listener_ = listener;

   bool result = true;
   uint8_t index = 0;
   for (auto &item : params_.servers) {
      auto routerListener = std::make_shared<RouterServerListener>();
      routerListener->index_ = index;
      routerListener->listener_ = listener;
      result = result && item.server->BindConnection(item.host, item.port, routerListener.get());
      listeners_.push_back(routerListener);
      index += 1;
   }
   return result;
}

bool RouterServerConnection::SendDataToClient(const std::string &clientId, const std::string &data)
{
   auto index = getIndexFromRouterClientId(clientId);
   const auto &server = params_.servers.at(index).server;
   return server->SendDataToClient(getClientIdFromRouterClientId(clientId), data);
}

bool RouterServerConnection::SendDataToAllClients(const std::string &data)
{
   bool result = true;
   for (auto &item : params_.servers) {
      result = result && item.server->SendDataToAllClients(data);
   }
   return result;
}
