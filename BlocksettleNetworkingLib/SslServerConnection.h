/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef SSL_SERVER_CONNECTION_H
#define SSL_SERVER_CONNECTION_H

#include "ServerConnection.h"
#include "WsConnection.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <map>

namespace spdlog {
   class logger;
}

struct lws_context;
struct lws;

struct SslServerConnectionParams
{
   bool useSsl{false};

   // If set, client's IP address will be read from x-forwarded-for header value if possible.
   // Last IP address in a list will be used - https://en.wikipedia.org/wiki/X-Forwarded-For#Format
   bool trustForwardedForHeader{false};

   // If set, clients connection must use client certificate
   bool requireClientCert{false};

   // Use this cerificate and key to server SSL connection.
   // Must be set if useSsl is set. Could be in DER or PEM format.
   std::string cert;
   bs::network::ws::PrivateKey privKey;

   // If set, verifyCallback must return true if connection is allowed and false if connection should be dropped.
   // publicKey is compressed public key from server's certificate (33 bytes).
   // (only P256 allowed, all other connections rejected if verifyCallback is set).
   // Callback should not block and useSsl must be set.
   using VerifyCallback = std::function<bool(const std::string &publicKey)>;
   VerifyCallback verifyCallback;

   bool sendAsText{false};
};

class SslServerConnection : public ServerConnection
{
public:
   SslServerConnection(const std::shared_ptr<spdlog::logger>& logger, SslServerConnectionParams params);
   ~SslServerConnection() override;

   SslServerConnection(const SslServerConnection&) = delete;
   SslServerConnection& operator = (const SslServerConnection&) = delete;
   SslServerConnection(SslServerConnection&&) = delete;
   SslServerConnection& operator = (SslServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   bool SendDataToClient(const std::string& clientId, const std::string& data) override;
   bool SendDataToAllClients(const std::string&) override;

   bool closeClient(const std::string& clientId) override;

   static int callbackHelper(struct lws *wsi, int reason, void *user, void *in, size_t len);

private:
   struct WsServerDataToSend
   {
      std::string clientId;
      bs::network::WsRawPacket packet;
   };

   struct WsServerClientData
   {
      std::string clientId;
      lws *wsi{};
      std::queue<bs::network::WsRawPacket> packets;
      std::string currFragment;
   };

   void listenFunction();

   void stopServer();

   int callback(struct lws *wsi, int reason, void *user, void *in, size_t len);

   std::string nextClientId();

   std::shared_ptr<spdlog::logger>  logger_;
   const SslServerConnectionParams params_;

   std::thread listenThread_;
   ServerConnectionListener *listener_{};
   std::atomic_bool stopped_{};
   lws_context *context_{};

   std::recursive_mutex             mutex_;
   std::queue<WsServerDataToSend>   packets_;
   std::queue<std::string>          forceClosingClients_;

   // Fields accessible from listener thread only
   std::map<std::string, WsServerClientData> clients_;
   std::map<lws*, std::string> socketToClientIdMap_;
   uint64_t nextClientId_{};

};

#endif // SSL_SERVER_CONNECTION_H
