/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef SSL_DATA_CONNECTION_H
#define SSL_DATA_CONNECTION_H

#include "DataConnection.h"
#include "WsConnection.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace spdlog {
   class logger;
}

struct lws_context;
struct lws_vhost;

struct SslDataConnectionParams
{
   // If not set, plain TCP connection is used
   bool useSsl{false};

   // If set server's certificate will be checked against
   const void *caBundlePtr{};
   uint32_t caBundleSize{};

   // If set, server's cerificate will not be checked against CA bundle
   bool allowSelfSigned{false};

   // If set, server's cerificate hostname will not be checked
   bool skipHostNameChecks{false};

   // If set, client's cerificate and key will be loaded and used for SSL connection
   // Could be in DER format only!
   std::string cert;
   bs::network::ws::PrivateKey privKey;

   // If set, verifyCallback must return true if connection is allowed and false if connection should be dropped.
   // publicKey is compressed public key from server's certificate (33 bytes)
   // (only P256 allowed, all other connections rejected if verifyCallback is set).
   // Callback should not block and useSsl must be set.
   using VerifyCallback = std::function<bool(const std::string &publicKey)>;
   VerifyCallback verifyCallback;
};

class SslDataConnection : public DataConnection
{
public:
   SslDataConnection(const std::shared_ptr<spdlog::logger>& logger, SslDataConnectionParams params);
   ~SslDataConnection() override;

   SslDataConnection(const SslDataConnection&) = delete;
   SslDataConnection& operator = (const SslDataConnection&) = delete;
   SslDataConnection(SslDataConnection&&) = delete;
   SslDataConnection& operator = (SslDataConnection&&) = delete;

   bool openConnection(const std::string& host, const std::string& port
      , DataConnectionListener* listener) override;
   bool closeConnection() override;

   bool send(const std::string& data) override;

   bool isActive() const override;

   static int callbackHelper(struct lws *wsi, int reason, void *user, void *in, size_t len);

private:
   int callback(struct lws *wsi, int reason, void *user, void *in, size_t len);

   void listenFunction();

   void reportFatalError(DataConnectionListener::DataConnectionError error);

   std::shared_ptr<spdlog::logger> logger_;
   const SslDataConnectionParams params_;

   lws_context *context_{};
   lws_vhost *vhost_{};

   std::string host_;
   int port_{};
   std::atomic_bool stopped_{};

   std::thread listenThread_;

   std::recursive_mutex mutex_;
   std::queue<bs::network::WsRawPacket> newPackets_;

   // Fields accessible from listener thread only!
   std::queue<bs::network::WsRawPacket> allPackets_;
   std::string currFragment_;
   lws *wsi_{};

};


#endif // SSL_DATA_CONNECTION_H
