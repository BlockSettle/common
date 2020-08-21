/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef PUBLISHER_SERVER_CONNECTION_H
#define PUBLISHER_SERVER_CONNECTION_H

#include "ServerConnection.h"

#include <memory>
#include <mutex>

namespace spdlog {
   class logger;
}

class ServerConnection;
class ServerConnectionListener;
class PublisherServerConnectionListener;

class PublisherServerConnection
{
public:
   PublisherServerConnection(const std::shared_ptr<spdlog::logger>& logger, std::unique_ptr<ServerConnection> conn);
   ~PublisherServerConnection();

   PublisherServerConnection(const PublisherServerConnection&) = delete;
   PublisherServerConnection& operator = (const PublisherServerConnection&) = delete;
   PublisherServerConnection(PublisherServerConnection&&) = delete;
   PublisherServerConnection& operator = (PublisherServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port);

   bool publishData(const std::string& data);
   void setWelcomeMessage(const std::string& data);

private:
   friend class PublisherServerConnectionListener;

   std::shared_ptr<spdlog::logger> logger_;
   std::unique_ptr<ServerConnection> conn_;
   std::unique_ptr<PublisherServerConnectionListener> listener_;

   mutable std::mutex mutex_;
   std::string welcomeMsg_;

};

#endif // PUBLISHER_SERVER_CONNECTION_H
