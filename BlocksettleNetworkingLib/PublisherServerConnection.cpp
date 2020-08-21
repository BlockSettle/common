/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "PublisherServerConnection.h"

class PublisherServerConnectionListener : public ServerConnectionListener
{
public:
   PublisherServerConnection *owner_{};

   void OnDataFromClient(const std::string& clientId, const std::string& data) override
   {
   }

   void OnClientConnected(const std::string &clientId, const Details &details) override
   {
      std::lock_guard<std::mutex> lock(owner_->mutex_);
      owner_->conn_->SendDataToClient(clientId, owner_->welcomeMsg_);
   }

   void OnClientDisconnected(const std::string& clientId) override
   {
   }

};

PublisherServerConnection::PublisherServerConnection(const std::shared_ptr<spdlog::logger> &logger
   , std::unique_ptr<ServerConnection> conn)
   : logger_(logger)
   , conn_(std::move(conn))
{
}

PublisherServerConnection::~PublisherServerConnection()
{
   conn_.reset();
}

bool PublisherServerConnection::BindConnection(const std::string &host, const std::string &port)
{
   listener_ = std::make_unique<PublisherServerConnectionListener>();
   listener_->owner_ = this;
   return conn_->BindConnection(host, port, listener_.get());
}

bool PublisherServerConnection::publishData(const std::string &data)
{
   return conn_->SendDataToAllClients(data);
}

void PublisherServerConnection::setWelcomeMessage(const std::string &data)
{
   std::lock_guard<std::mutex> lock(mutex_);
   welcomeMsg_ = data;
}
