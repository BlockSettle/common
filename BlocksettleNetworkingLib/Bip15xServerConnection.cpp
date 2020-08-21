/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Bip15xServerConnection.h"

#include "StringUtils.h"
#include "ThreadName.h"
#include "Transport.h"
#include "WsConnection.h"

#include <spdlog/spdlog.h>

class Bip15xServerListener : public ServerConnectionListener
{
public:
   Bip15xServerConnection *owner_{};

   void OnDataFromClient(const std::string& clientId, const std::string& data) override
   {
      owner_->transport_->processIncomingData(data, clientId);
   }

   void OnClientConnected(const std::string& clientId, const Details &details) override
   {
      owner_->transport_->addClient(clientId, details);
   }

   void OnClientDisconnected(const std::string& clientId) override
   {
      bool wasConnected;
      { std::lock_guard<std::mutex> lock(owner_->mutex_);
         wasConnected = owner_->clients_.erase(clientId) == 1;
      }
      if (wasConnected) {
         owner_->listener_->OnClientDisconnected(clientId);
      }
      owner_->transport_->closeClient(clientId);
   }

   void onClientError(const std::string &clientId, ClientError error
      , const ServerConnectionListener::Details &details) override
   {
      OnClientDisconnected(clientId);
      owner_->listener_->onClientError(clientId, error, details);
   }
};


Bip15xServerConnection::Bip15xServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , std::unique_ptr<ServerConnection> server
   , const std::shared_ptr<bs::network::TransportServer> &tr)
   : logger_(logger)
   , server_(std::move(server))
   , transport_(tr)
{
   transport_->setClientErrorCb([this](const std::string &id
      , ServerConnectionListener::ClientError err
      , const ServerConnectionListener::Details &details) {
      listener_->onClientError(id, err, details);
   });

   transport_->setDataReceivedCb([this](const std::string &clientId, const std::string &data) {
      listener_->OnDataFromClient(clientId, data);
   });

   transport_->setSendDataCb([this](const std::string &clientId, const std::string &data) {
      return server_->SendDataToClient(clientId, data);
   });

   transport_->setConnectedCb([this](const std::string &clientId, const ServerConnectionListener::Details &details) {
      {
         std::lock_guard<std::mutex> lock(mutex_);
         clients_.insert(clientId);
      }
      listener_->OnClientConnected(clientId, details);
   });

   transport_->setDisconnectedCb([this](const std::string &clientId) {
      bool wasConnected;
      { std::lock_guard<std::mutex> lock(mutex_);
         wasConnected = clients_.erase(clientId) == 1;
      }
      if (wasConnected) {
         listener_->OnClientDisconnected(clientId);
      }
   });
}

Bip15xServerConnection::~Bip15xServerConnection()
{
   server_.reset();
}

bool Bip15xServerConnection::BindConnection(const std::string& host , const std::string& port
   , ServerConnectionListener* listener)
{
   ownListener_ = std::make_unique<Bip15xServerListener>();
   ownListener_->owner_ = this;
   bool result = server_->BindConnection(host, port, ownListener_.get());
   listener_ = listener;
   return result;
}

bool Bip15xServerConnection::SendDataToClient(const std::string &clientId, const std::string &data)
{
   return transport_->sendData(clientId, data);
}

bool Bip15xServerConnection::SendDataToAllClients(const std::string &data)
{
   std::lock_guard<std::mutex> lock(mutex_);
   size_t cntClients = clients_.size();
   for (const auto &item : clients_) {
      if (SendDataToClient(item, data)) {
         cntClients--;
      }
   }
   return (cntClients == 0);
}
