/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_SERVER_CONNECTION_H
#define WS_SERVER_CONNECTION_H

#include "ServerConnection.h"
#include "WsConnection.h"

#include <atomic>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <thread>

namespace spdlog {
   class logger;
}

struct WsServerTimer;
struct lws;
struct lws_context;
struct lws_sorted_usec_list;

namespace bs {
   namespace network {
      struct WsPacket;
   }
}

struct WsServerConnectionParams
{
   size_t maximumPacketSize{bs::network::ws::kDefaultMaximumWsPacketSize};

   // If set, client's IP address will be read from x-forwarded-for header value if possible.
   // Last IP address in a list will be used - https://en.wikipedia.org/wiki/X-Forwarded-For#Format
   bool trustForwardedForHeader{};

   // If set, filterCallback must return true if connection is allowed and false if connection should be dropped
   using FilterCallback = std::function<bool(const std::string &ip)>;
   FilterCallback filterCallback;

   std::chrono::milliseconds clientTimeout{std::chrono::seconds(30)};
};

class WsServerConnection : public ServerConnection
{
public:
   WsServerConnection(const std::shared_ptr<spdlog::logger>& logger, WsServerConnectionParams params);
   ~WsServerConnection() override;

   WsServerConnection(const WsServerConnection&) = delete;
   WsServerConnection& operator = (const WsServerConnection&) = delete;
   WsServerConnection(WsServerConnection&&) = delete;
   WsServerConnection& operator = (WsServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   bool SendDataToClient(const std::string& clientId, const std::string& data) override;
   bool SendDataToAllClients(const std::string&) override;

   static int callbackHelper(struct lws *wsi, int reason, void *in, size_t len);

   using TimerCallback = std::function<void()>;

private:
   enum class State
   {
      WaitHandshake,
      SendingHandshakeNew,
      SendingHandshakeResumed,
      SendingHandshakeNotFound,
      Connected,
      Closed,
   };

   struct DataToSend
   {
      std::string clientId;
      bs::network::WsRawPacket packet;
   };

   struct ConnectionData
   {
      std::string currFragment;
      State state{State::WaitHandshake};
      std::string clientId; // only for State::Connected and State::SendingHandshakeResumed
      std::string ipAddr;
   };

   struct ClientData
   {
      std::map<uint64_t, bs::network::WsRawPacket> allPackets;
      std::string cookie;
      lws *wsi{};
      uint64_t sentCounter{};
      uint64_t sentAckCounter{};
      uint64_t queuedCounter{};
      uint64_t recvCounter{};
      uint64_t recvAckCounter{};
   };

   void listenFunction();

   void stopServer();

   int callback(lws *wsi, int reason, void *in, size_t len);

   static void timerCallback(lws_sorted_usec_list *list);

   // Methods accessible from listener thread only
   std::string nextClientId();
   bool done() const;
   bool writeNeeded(const ClientData &client) const;
   void requestWriteIfNeeded(const ClientData &client);
   bool processSentAck(ClientData &client, uint64_t sentAckCounter);
   void scheduleCallback(std::chrono::milliseconds timeout, TimerCallback callback);
   void processError(lws *wsi);
   void closeConnectedClient(const std::string &clientId);

   std::shared_ptr<spdlog::logger>  logger_;
   const WsServerConnectionParams params_;

   std::thread listenThread_;
   ServerConnectionListener *listener_{};
   std::atomic_bool shuttingDown_{};
   lws_context *context_{};

   mutable std::mutex mutex_;
   std::queue<DataToSend> packets_;

   // Fields accessible from listener thread only
   std::map<lws*, ConnectionData> connections_;
   std::map<std::string, ClientData> clients_;
   std::map<std::string, std::string> cookieToClientIdMap_;
   uint64_t nextClientId_{};
   std::map<uint64_t, std::unique_ptr<WsServerTimer>> timers_;
   uint64_t nextTimerId_{};
   bool shuttingDownReceived_{};

};

#endif // WS_SERVER_CONNECTION_H
