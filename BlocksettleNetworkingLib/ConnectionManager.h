/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CONNECTION_MANAGER_H__
#define __CONNECTION_MANAGER_H__

#include <memory>
#include <vector>
#include <string>
#include <spdlog/logger.h>

#include "BIP15xHelpers.h"
#include "TransportBIP15x.h"

namespace bs {
   namespace network {
      class TransportBIP15xClient;
   }
}
class ArmoryServersProvider;
class DataConnection;
class PublisherConnection;
class ServerConnection;
class SubscriberConnection;
class ZmqContext;
class QNetworkAccessManager;

class ConnectionManager
{
public:
   ConnectionManager(const std::shared_ptr<spdlog::logger>& logger);
   ConnectionManager(const std::shared_ptr<spdlog::logger>& logger
      , const bs::network::BIP15xPeers &zmqTrustedTerminals);
   ConnectionManager(const std::shared_ptr<spdlog::logger>& logger
      , std::shared_ptr<ArmoryServersProvider> armoryServers);
   virtual ~ConnectionManager() noexcept;

   ConnectionManager(const ConnectionManager&) = delete;
   ConnectionManager& operator = (const ConnectionManager&) = delete;
   ConnectionManager(ConnectionManager&&) = delete;
   ConnectionManager& operator = (ConnectionManager&&) = delete;

   bool IsInitialized() const { return isInitialized_; }
   void setCaBundle(const void *caBundlePtr, size_t caBundleSize);

   std::shared_ptr<spdlog::logger>     GetLogger() const;

   std::shared_ptr<ServerConnection>   CreateGenoaAPIServerConnection() const;
   virtual std::shared_ptr<ServerConnection>   CreateCelerAPIServerConnection() const;

   virtual std::shared_ptr<DataConnection>     CreateCelerClientConnection() const;
   std::shared_ptr<DataConnection>     CreateGenoaClientConnection(
      bool monitored = false) const;

   std::shared_ptr<ServerConnection>   CreatePubBridgeServerConnection() const;

   std::shared_ptr<ServerConnection>   CreateMDRestServerConnection() const;

   std::shared_ptr<PublisherConnection>   CreatePublisherConnection() const;
   std::shared_ptr<SubscriberConnection>  CreateSubscriberConnection() const;

   const std::shared_ptr<QNetworkAccessManager> &GetNAM();

   std::shared_ptr<ZmqContext> zmqContext() const { return zmqContext_; }

   std::shared_ptr<DataConnection>  CreateInsecureWsConnection() const;
   std::shared_ptr<DataConnection>  CreateSecureWsConnection() const;

private:
   bool InitNetworkLibs();
   void DeinitNetworkLibs();
private:
   bool isInitialized_;

   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ZmqContext>            zmqContext_;
   std::shared_ptr<QNetworkAccessManager> nam_;
   std::shared_ptr<ArmoryServersProvider> armoryServers_;
   bs::network::BIP15xPeers               zmqTrustedTerminals_;

   const void *caBundlePtr_{};
   size_t caBundleSize_{};
};

#endif // __CONNECTION_MANAGER_H__
