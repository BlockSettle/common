/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BIP15X_DATA_CONNECTION_H
#define BIP15X_DATA_CONNECTION_H

#include "DataConnection.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>

namespace spdlog {
   class logger;
}
namespace bs {
   namespace network {
      class TransportClient;
   }
}

class Bip15xDataListener;

class Bip15xDataConnection : public DataConnection
{
public:
   Bip15xDataConnection(const std::shared_ptr<spdlog::logger> &
      , std::unique_ptr<DataConnection> conn
      , const std::shared_ptr<bs::network::TransportClient> &);
   ~Bip15xDataConnection() override;

   Bip15xDataConnection(const Bip15xDataConnection&) = delete;
   Bip15xDataConnection& operator = (const Bip15xDataConnection&) = delete;
   Bip15xDataConnection(Bip15xDataConnection&&) = delete;
   Bip15xDataConnection& operator = (Bip15xDataConnection&&) = delete;

   bool openConnection(const std::string& host, const std::string& port
      , DataConnectionListener* listener) override;
   bool closeConnection() override;

   bool send(const std::string& data) override;
   bool isActive() const override { return conn_->isActive(); }

private:
   friend class Bip15xDataListener;

   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<bs::network::TransportClient> transport_;
   std::unique_ptr<DataConnection> conn_;
   std::unique_ptr<Bip15xDataListener> ownListener_;
   std::atomic_bool connected_{};

   std::mutex mutex_;
   std::queue<std::string> sendQueue_;
};


#endif // BIP15X_DATA_CONNECTION_H
