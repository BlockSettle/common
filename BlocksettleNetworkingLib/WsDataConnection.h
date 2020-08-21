/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_DATA_CONNECTION_H
#define WS_DATA_CONNECTION_H

#include "DataConnection.h"
#include "WsConnection.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace spdlog {
   class logger;
}
struct WsTimerStruct;
struct lws_context;
struct lws_retry_bo;
struct lws_sorted_usec_list;

struct WsDataConnectionParams
{
   const void *caBundlePtr{};
   uint32_t caBundleSize{};
   bool useSsl{false};
   size_t maximumPacketSize{bs::network::ws::kDefaultMaximumWsPacketSize};
   std::vector<uint32_t> delaysTableMs;
};

class WsDataConnection : public DataConnection
{
public:
   WsDataConnection(const std::shared_ptr<spdlog::logger>& logger, WsDataConnectionParams params);
   ~WsDataConnection() override;

   WsDataConnection(const WsDataConnection&) = delete;
   WsDataConnection& operator = (const WsDataConnection&) = delete;
   WsDataConnection(WsDataConnection&&) = delete;
   WsDataConnection& operator = (WsDataConnection&&) = delete;

   bool openConnection(const std::string& host, const std::string& port
      , DataConnectionListener* listener) override;
   bool closeConnection() override;

   bool send(const std::string& data) override;
   bool isActive() const override;

   static int callbackHelper(struct lws *wsi, int reason, void *user, void *in, size_t len);

private:
   enum class State
   {
      Connecting,
      Reconnecting,
      WaitingNewResponse,
      WaitingResumedResponse,
      Connected,
      Closing,
      Closed,
   };

   int callback(struct lws *wsi, int reason, void *user, void *in, size_t len);

   static void reconnectCallback(lws_sorted_usec_list *list);

   void listenFunction();

   void scheduleReconnect();
   void reconnect();
   void processError();
   void processFatalError();
   bool writeNeeded() const;
   void requestWriteIfNeeded();
   bool processSentAck(uint64_t sentAckCounter);

   // For tests, default is noop
   virtual bs::network::WsRawPacket filterRawPacket(bs::network::WsRawPacket packet);

   std::shared_ptr<spdlog::logger> logger_;
   const WsDataConnectionParams params_;

   lws_context *context_{};
   std::string host_;
   int port_{};
   std::atomic_bool shuttingDown_{};

   std::thread listenThread_;

   std::mutex mutex_;
   std::queue<bs::network::WsRawPacket> newPackets_;

   // Fields accessible from listener thread only!
   std::map<uint64_t, bs::network::WsRawPacket> allPackets_;
   std::string currFragment_;
   lws *wsi_{};
   State state_{State::Connecting};
   uint64_t sentCounter_{};
   uint64_t sentAckCounter_{};
   uint64_t queuedCounter_{};
   uint64_t recvCounter_{};
   uint64_t recvAckCounter_{};
   std::string cookie_;
   std::unique_ptr<WsTimerStruct> reconnectTimer_;
   uint16_t retryCounter_{};
   bool shuttingDownReceived_{};
   std::unique_ptr<lws_retry_bo> retryTable_;

};


#endif // WS_DATA_CONNECTION_H
