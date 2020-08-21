/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BIP15X_SERVER_CONNECTION_H
#define BIP15X_SERVER_CONNECTION_H

#include "ServerConnection.h"

#include <memory>
#include <mutex>
#include <set>

namespace spdlog {
   class logger;
}
namespace bs{
   namespace network {
      class TransportServer;
   }
}

class Bip15xServerListener;

class Bip15xServerConnection : public ServerConnection
{
public:
   Bip15xServerConnection(const std::shared_ptr<spdlog::logger> &
      , std::unique_ptr<ServerConnection>
      , const std::shared_ptr<bs::network::TransportServer> &);
   ~Bip15xServerConnection() override;

   Bip15xServerConnection(const Bip15xServerConnection&) = delete;
   Bip15xServerConnection& operator = (const Bip15xServerConnection&) = delete;
   Bip15xServerConnection(Bip15xServerConnection&&) = delete;
   Bip15xServerConnection& operator = (Bip15xServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   bool SendDataToClient(const std::string& clientId, const std::string& data) override;
   bool SendDataToAllClients(const std::string&) override;

protected:
   friend class Bip15xServerListener;

   std::shared_ptr<spdlog::logger> logger_;
   std::unique_ptr<ServerConnection> server_;
   std::shared_ptr<bs::network::TransportServer> transport_;
   std::unique_ptr<Bip15xServerListener> ownListener_;

   ServerConnectionListener *listener_{};

   std::mutex mutex_;
   std::set<std::string> clients_;
};

#endif // BIP15X_SERVER_CONNECTION_H
