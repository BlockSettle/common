/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WsServerConnection.h"

#include "BinaryData.h"
#include "EncryptionUtils.h"
#include "StringUtils.h"
#include "ThreadName.h"
#include "WsConnection.h"

#include <random>
#include <libwebsockets.h>
#include <spdlog/spdlog.h>

using namespace bs::network;

namespace {

   int callback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
   {
      return WsServerConnection::callbackHelper(wsi, reason, in, len);
   }

   struct lws_protocols kProtocols[] = {
      { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

   // Make sure that regular clientId can't clash with that
   const auto kAllClientsId = std::string({'\0'});

   std::string generateNewCookie()
   {
      return CryptoPRNG::generateRandom(32).toBinStr();
   }

} // namespace

struct WsServerTimer : lws_sorted_usec_list_t
{
   WsServerConnection *owner_{};
   uint64_t timerId{};
   WsServerConnection::TimerCallback callback;
};

WsServerConnection::WsServerConnection(const std::shared_ptr<spdlog::logger>& logger, WsServerConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
{
}

WsServerConnection::~WsServerConnection()
{
   stopServer();
}

bool WsServerConnection::BindConnection(const std::string& host , const std::string& port
   , ServerConnectionListener* listener)
{
   stopServer();

   std::random_device rd;
   std::mt19937_64 gen(rd());
   std::uniform_int_distribution<uint64_t> dis;
   nextClientId_ = dis(gen);

   struct lws_context_creation_info info;
   memset(&info, 0, sizeof(info));
   info.port = std::stoi(port);
   info.protocols = kProtocols;
   info.gid = -1;
   info.uid = -1;
   info.retry_and_idle_policy = bs::network::ws::defaultRetryAndIdlePolicy();
   info.options = LWS_SERVER_OPTION_VALIDATE_UTF8 | LWS_SERVER_OPTION_DISABLE_IPV6;
   info.user = this;

   // Context creation will return nullptr if port binding failed
   context_ = lws_create_context(&info);
   if (context_ == nullptr) {
      SPDLOG_LOGGER_ERROR(logger_, "context create failed");
      return false;
   }

   shuttingDown_ = false;
   listener_ = listener;

   listenThread_ = std::thread(&WsServerConnection::listenFunction, this);

   return true;
}

void WsServerConnection::listenFunction()
{
   bs::setCurrentThreadName("WsServer");

   while (!done()) {
      lws_service(context_, 0);
   }
}

void WsServerConnection::stopServer()
{
   if (!listenThread_.joinable()) {
      return;
   }

   shuttingDown_ = true;
   lws_cancel_service(context_);

   listenThread_.join();

   lws_context_destroy(context_);
   listener_ = nullptr;
   context_ = nullptr;

   packets_ = {};
   clients_ = {};
   connections_ = {};
   cookieToClientIdMap_ = {};
   nextTimerId_ = {};
   shuttingDownReceived_ = {};
   timers_.clear();
}

int WsServerConnection::callback(lws *wsi, int reason, void *in, size_t len)
{
   switch (reason) {
      case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
         std::queue<DataToSend> packets;
         {
            std::lock_guard<std::mutex> lock(mutex_);
            std::swap(packets, packets_);
         }
         while (!packets.empty()) {
            auto data = std::move(packets.front());
            packets.pop();

            if (data.clientId == kAllClientsId) {
               for (auto &item : clients_) {
                  auto &client = item.second;
                  client.allPackets.insert(std::make_pair(client.queuedCounter, data.packet));
                  client.queuedCounter += 1;
                  requestWriteIfNeeded(client);
               }
               continue;
            }

            auto clientIt = clients_.find(data.clientId);
            if (clientIt == clients_.end()) {
               SPDLOG_LOGGER_DEBUG(logger_, "send failed, client {} already disconnected", bs::toHex(data.clientId));
               continue;
            }
            auto &client = clientIt->second;
            client.allPackets.insert(std::make_pair(client.queuedCounter, data.packet));
            client.queuedCounter += 1;
            requestWriteIfNeeded(client);
         }

         if (shuttingDown_.load() && !shuttingDownReceived_) {
            shuttingDownReceived_ = true;
            for (const auto &connection : connections_) {
               lws_close_reason(connection.first, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
               lws_set_timeout(connection.first, PENDING_TIMEOUT_USER_OK, LWS_TO_KILL_SYNC);
            }
         }

         break;
      }

      case LWS_CALLBACK_ESTABLISHED: {
         auto &connection = connections_[wsi];
         auto connIp = bs::network::ws::connectedIp(wsi);
         auto forwIp = bs::network::ws::forwardedIp(wsi);
         connection.ipAddr = params_.trustForwardedForHeader && !forwIp.empty() ? forwIp : connIp;
         SPDLOG_LOGGER_DEBUG(logger_, "wsi connected: {}, connected ip: {}, forwarded ip: {}"
            , static_cast<void*>(wsi), connIp, forwIp);
         if (shuttingDown_) {
            connection.state = State::Closed;
            return -1;
         }
         if (params_.filterCallback && !params_.filterCallback(connection.ipAddr)) {
            SPDLOG_LOGGER_DEBUG(logger_, "drop connection because filterCallback returns false");
            // NOTE: Simply returning -1 is not enough here for some reasons!
            connection.state = State::Closed;
            return -1;
         }
         break;
      }

      case LWS_CALLBACK_CLOSED: {
         SPDLOG_LOGGER_DEBUG(logger_, "wsi disconnected: {}", static_cast<void*>(wsi));
         auto connectionIt = connections_.find(wsi);
         assert(connectionIt != connections_.end());
         auto &connection = connectionIt->second;
         switch (connection.state) {
            case State::Connected:
            case State::SendingHandshakeResumed: {
               const auto &clientId = connection.clientId;
               auto &client = clients_.at(connection.clientId);
               SPDLOG_LOGGER_DEBUG(logger_, "connection closed unexpectedly, clientId: {}", bs::toHex(connection.clientId));
               client.wsi = nullptr;
               scheduleCallback(params_.clientTimeout, [this, clientId] {
                  auto clientIt = clients_.find(clientId);
                  if (clientIt == clients_.end()) {
                     return;
                  }
                  auto &client = clientIt->second;
                  if (client.wsi == nullptr) {
                     SPDLOG_LOGGER_ERROR(logger_, "connection removed by timeout");
                     closeConnectedClient(clientId);
                     listener_->OnClientDisconnected(clientId);
                     listener_->onClientError(clientId, ServerConnectionListener::Timeout, {});
                  }
               });
               break;
            }
            case State::SendingHandshakeNew:
            case State::SendingHandshakeNotFound:
            case State::WaitHandshake:
            case State::Closed: {
               // Nothing to do
               break;
            }
         }
         connections_.erase(connectionIt);
         break;
      }

      case LWS_CALLBACK_RECEIVE: {
         auto &connection = connections_.at(wsi);

         auto ptr = static_cast<const char*>(in);
         connection.currFragment.insert(connection.currFragment.end(), ptr, ptr + len);
         if (connection.currFragment.size() > params_.maximumPacketSize) {
            SPDLOG_LOGGER_ERROR(logger_, "maximum packet size reached");
            processError(wsi);
            return -1;
         }
         if (lws_remaining_packet_payload(wsi) > 0) {
            return 0;
         }
         if (!lws_is_final_fragment(wsi)) {
            SPDLOG_LOGGER_ERROR(logger_, "unexpected fragment");
            processError(wsi);
            return -1;
         }

         auto packet = WsPacket::parsePacket(connection.currFragment, logger_);
         connection.currFragment.clear();

         switch (connection.state) {
            case State::Connected: {
               auto &client = clients_.at(connection.clientId);
               switch (packet.type) {
                  case WsPacket::Type::Data: {
                     client.recvCounter += 1;
                     listener_->OnDataFromClient(connection.clientId, packet.payload);
                     break;
                  }
                  case WsPacket::Type::Ack: {
                     if (!processSentAck(client, packet.recvCounter)) {
                        SPDLOG_LOGGER_ERROR(logger_, "invalid ack");
                        processError(wsi);
                        return -1;
                     }
                     break;
                  }
                  default: {
                     SPDLOG_LOGGER_ERROR(logger_, "unexpected packet");
                     processError(wsi);
                     return -1;
                  }
               }
               requestWriteIfNeeded(client);
               return 0;
            }
            case State::WaitHandshake: {
               switch (packet.type) {
                  case WsPacket::Type::RequestNew: {
                     connection.state = State::SendingHandshakeNew;
                     lws_callback_on_writable(wsi);
                     return 0;
                  }
                  case WsPacket::Type::RequestResumed: {
                     auto cookieIt = cookieToClientIdMap_.find(packet.payload);
                     if (cookieIt == cookieToClientIdMap_.end()) {
                        connection.state = State::SendingHandshakeNotFound;
                        lws_callback_on_writable(wsi);
                        SPDLOG_LOGGER_ERROR(logger_, "resume cookie not found");
                        return 0;
                     }
                     const auto &clientId = cookieIt->second;
                     auto &client = clients_.at(clientId);
                     if (!processSentAck(client, packet.recvCounter)) {
                        SPDLOG_LOGGER_ERROR(logger_, "resuming connection failed");
                        processError(wsi);
                        return -1;
                     }
                     if (client.wsi != nullptr) {
                        auto &oldConnection = connections_.at(client.wsi);
                        oldConnection.state = State::Closed;
                        lws_callback_on_writable(client.wsi);
                     }
                     connection.state = State::SendingHandshakeResumed;
                     connection.clientId = clientId;
                     client.wsi = wsi;
                     client.sentCounter = packet.recvCounter;
                     lws_callback_on_writable(wsi);
                     return 0;
                  }
                  default: {
                     SPDLOG_LOGGER_ERROR(logger_, "unexpected packet");
                     processError(wsi);
                     return -1;
                  }
               }
            }
            case State::SendingHandshakeResumed: {
               auto &client = clients_.at(connection.clientId);
               client.wsi = nullptr;
               SPDLOG_LOGGER_ERROR(logger_, "unexpected packet");
               processError(wsi);
               return -1;
            }
            case State::SendingHandshakeNotFound:
            case State::SendingHandshakeNew: {
               SPDLOG_LOGGER_ERROR(logger_, "unexpected packet");
               processError(wsi);
               return -1;
            }
            case State::Closed: {
               processError(wsi);
               return -1;
            }
         }
         break;
      }

      case LWS_CALLBACK_SERVER_WRITEABLE: {
         auto &connection = connections_.at(wsi);

         switch (connection.state) {
            case State::Connected: {
               auto &client = clients_.at(connection.clientId);

               if (client.recvCounter != client.recvAckCounter) {
                  auto packet = WsPacket::ack(client.recvCounter);
                  int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
                  if (rc != static_cast<int>(packet.getSize())) {
                      SPDLOG_LOGGER_ERROR(logger_, "write failed");
                      processError(wsi);
                      return -1;
                  }
                  client.recvAckCounter = client.recvCounter;
               } else if (client.sentCounter != client.queuedCounter) {
                  auto &packet = client.allPackets.at(client.sentCounter);
                  int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
                  if (rc == -1) {
                     SPDLOG_LOGGER_ERROR(logger_, "write failed");
                     processError(wsi);
                     return -1;
                  }
                  if (rc != static_cast<int>(packet.getSize())) {
                      SPDLOG_LOGGER_ERROR(logger_, "write truncated");
                      processError(wsi);
                      return -1;
                  }
                  client.sentCounter += 1;
               }
               requestWriteIfNeeded(client);
               return 0;
            }
            case State::WaitHandshake: {
               return 0;
            }
            case State::SendingHandshakeNotFound: {
               auto packet = WsPacket::responseUnknown();
               lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
               connection.state = State::Closed;
               return -1;
            }
            case State::SendingHandshakeResumed: {
               auto &client = clients_.at(connection.clientId);
               auto packet = WsPacket::responseResumed(client.recvCounter);
               int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
               if (rc == -1) {
                  SPDLOG_LOGGER_ERROR(logger_, "write failed");
                  processError(wsi);
                  return -1;
               }
               connection.state = State::Connected;
               lws_callback_on_writable(wsi);
               SPDLOG_LOGGER_DEBUG(logger_, "session resumed for client {}", bs::toHex(connection.clientId));
               return 0;
            }
            case State::SendingHandshakeNew: {
               auto cookie = generateNewCookie();
               auto packet = WsPacket::responseNew(cookie);
               int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
               if (rc == -1) {
                  SPDLOG_LOGGER_ERROR(logger_, "write failed");
                  processError(wsi);
                  return -1;
               }
               auto clientId = nextClientId();
               connection.state = State::Connected;
               cookieToClientIdMap_[cookie] = clientId;
               auto &client = clients_[clientId];
               client.cookie = cookie;
               client.wsi = wsi;
               connection.clientId = clientId;
               ServerConnectionListener::Details details;
               details[ServerConnectionListener::Detail::IpAddr] = connection.ipAddr;
               SPDLOG_LOGGER_DEBUG(logger_, "new session started for client {}", bs::toHex(clientId));
               listener_->OnClientConnected(clientId, details);
               lws_callback_on_writable(wsi);
               return 0;
            }
            case State::Closed: {
               return -1;
            }
         }
         break;
      }

      case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: {
         uint16_t code;
         std::memcpy(&code, in, sizeof(code));
         code = htons(code);
         auto &connection = connections_.at(wsi);
         SPDLOG_LOGGER_DEBUG(logger_, "closing frame received with status code {}", code);
         switch (connection.state) {
            case State::Connected: {
               if (code == LWS_CLOSE_STATUS_NORMAL) {
                  connection.state = State::Closed;
                  closeConnectedClient(connection.clientId);
                  listener_->OnClientDisconnected(connection.clientId);
               }
               return -1;
            }
            default:
               break;
         }
         break;
      }

      default:
         break;
   }

   return 0;
}

void WsServerConnection::timerCallback(lws_sorted_usec_list *list)
{
   auto data = static_cast<WsServerTimer*>(list);
   data->callback();
   auto count = data->owner_->timers_.erase(data->timerId);
   assert(count == 1);
}

std::string WsServerConnection::nextClientId()
{
   nextClientId_ += 1;
   auto ptr = reinterpret_cast<char*>(&nextClientId_);
   return std::string(ptr, ptr + sizeof(nextClientId_));
}

bool WsServerConnection::done() const
{
   return shuttingDown_ && connections_.empty();
}

bool WsServerConnection::writeNeeded(const ClientData &client) const
{
   if (client.wsi == nullptr) {
      return false;
   }
   return client.sentCounter != client.queuedCounter || client.recvCounter != client.recvAckCounter;
}

void WsServerConnection::requestWriteIfNeeded(const ClientData &client)
{
   if (writeNeeded(client)) {
      lws_callback_on_writable(client.wsi);
   }
}

bool WsServerConnection::processSentAck(WsServerConnection::ClientData &client, uint64_t sentAckCounter)
{
   if (sentAckCounter < client.sentAckCounter || sentAckCounter > client.sentCounter) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid ack value from client");
      return false;
   }

   while (client.sentAckCounter < sentAckCounter) {
      size_t count = client.allPackets.erase(client.sentAckCounter);
      assert(count == 1);
      client.sentAckCounter += 1;
   }

   return true;
}

void WsServerConnection::scheduleCallback(std::chrono::milliseconds timeout, WsServerConnection::TimerCallback callback)
{
   auto timerId = nextTimerId_;
   nextTimerId_ += 1;

   auto timer = std::make_unique<WsServerTimer>();
   std::memset(static_cast<lws_sorted_usec_list_t*>(timer.get()), 0, sizeof(lws_sorted_usec_list_t));
   timer->owner_ = this;
   timer->timerId = timerId;
   timer->callback = std::move(callback);

   lws_sul_schedule(context_, 0, timer.get(), timerCallback, static_cast<lws_usec_t>(timeout / std::chrono::microseconds(1)));

   timers_.insert(std::make_pair(timerId, std::move(timer)));
}

void WsServerConnection::processError(lws *wsi)
{
   auto &connection = connections_.at(wsi);
   switch (connection.state) {
      case State::SendingHandshakeResumed:
      case State::Connected: {
         auto &client = clients_.at(connection.clientId);
         assert(client.wsi == wsi);
         client.wsi = nullptr;
         break;
      }
      default: {
         assert(connection.clientId.empty());
         break;
      }
   }
   connection.state = State::Closed;
}

void WsServerConnection::closeConnectedClient(const std::string &clientId)
{
   auto &client = clients_.at(clientId);
   auto count = cookieToClientIdMap_.erase(client.cookie);
   assert(count == 1);
   clients_.erase(clientId);
}

bool WsServerConnection::SendDataToClient(const std::string &clientId, const std::string &data)
{
   DataToSend toSend{clientId, WsPacket::data(data)};
   {
      std::lock_guard<std::mutex> lock(mutex_);
      packets_.push(std::move(toSend));
   }
   lws_cancel_service(context_);
   return true;
}

bool WsServerConnection::SendDataToAllClients(const std::string &data)
{
   return SendDataToClient(kAllClientsId, data);
}

int WsServerConnection::callbackHelper(lws *wsi, int reason, void *in, size_t len)
{
   auto context = lws_get_context(wsi);
   auto server = static_cast<WsServerConnection*>(lws_context_user(context));
   return server->callback(wsi, reason, in, len);
}
