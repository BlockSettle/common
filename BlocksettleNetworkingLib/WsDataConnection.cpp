/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WsDataConnection.h"

#include "WsConnection.h"

#include <libwebsockets.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <spdlog/spdlog.h>

using namespace bs::network;

namespace {

   const std::vector<uint32_t> kDefaultDelaysTableMs = { 10, 100, 200, 500, 3000, 10000 };

   int callback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
   {
      return WsDataConnection::callbackHelper(wsi, reason, user, in, len);
   }

   struct lws_protocols kProtocols[] = {
      { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

} // namespace

struct WsTimerStruct : lws_sorted_usec_list_t
{
   WsDataConnection *owner_;
};

WsDataConnection::WsDataConnection(const std::shared_ptr<spdlog::logger> &logger
   , WsDataConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
   , reconnectTimer_(new WsTimerStruct)
{
   // lws will use default value of 30% for jitter
   retryTable_ = std::make_unique<lws_retry_bo>();
   std::memset(retryTable_.get(), 0, sizeof(lws_retry_bo));
   const auto &retryTable = params_.delaysTableMs.empty() ? kDefaultDelaysTableMs : params_.delaysTableMs;
   retryTable_->retry_ms_table = retryTable.data();
   retryTable_->retry_ms_table_count = static_cast<uint16_t>(retryTable.size());

   std::memset(reconnectTimer_.get(), 0, sizeof(*reconnectTimer_));
   reconnectTimer_->owner_ = this;
}

WsDataConnection::~WsDataConnection()
{
   closeConnection();
}

bool WsDataConnection::openConnection(const std::string &host, const std::string &port
   , DataConnectionListener *listener)
{
   closeConnection();

   listener_ = listener;
   host_ = host;
   port_ = std::stoi(port);
   shuttingDown_ = false;

   lws_context_creation_info info;
   memset(&info, 0, sizeof(info));

   info.port = CONTEXT_PORT_NO_LISTEN;
   info.protocols = kProtocols;
   info.gid = -1;
   info.uid = -1;
   info.retry_and_idle_policy = bs::network::ws::defaultRetryAndIdlePolicy();
   info.options = params_.useSsl ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT : 0;
   info.user = this;
   info.ssl_options_set = params_.useSsl ? ws::sslOptionsSet() : 0;

   context_ = lws_create_context(&info);
   if (!context_) {
      SPDLOG_LOGGER_ERROR(logger_, "context create failed");
      return false;
   }

   listenThread_ = std::thread(&WsDataConnection::listenFunction, this);

   return true;
}

bool WsDataConnection::closeConnection()
{
   if (!listenThread_.joinable()) {
      return false;
   }

   shuttingDown_ = true;
   lws_cancel_service(context_);

   listenThread_.join();

   lws_context_destroy(context_);

   context_ = nullptr;
   listener_ = nullptr;
   newPackets_ = {};
   allPackets_ = {};
   currFragment_ = {};
   state_ = {};
   sentCounter_ = {};
   sentAckCounter_ = {};
   queuedCounter_ = {};
   recvCounter_ = {};
   recvAckCounter_ = {};
   cookie_ = {};
   std::memset(reconnectTimer_.get(), 0, sizeof(*reconnectTimer_));
   reconnectTimer_->owner_ = this;
   retryCounter_ = {};
   shuttingDownReceived_ = {};

   return true;
}

bool WsDataConnection::send(const std::string &data)
{
   if (!context_) {
      return false;
   }
   {
      std::lock_guard<std::mutex> lock(mutex_);
      newPackets_.push(filterRawPacket(WsPacket::data(data)));
   }
   lws_cancel_service(context_);
   return true;
}

bool WsDataConnection::isActive() const
{
   return context_ != nullptr;
}

int WsDataConnection::callbackHelper(lws *wsi, int reason, void *user, void *in, size_t len)
{
   auto context = lws_get_context(wsi);
   auto client = static_cast<WsDataConnection*>(lws_context_user(context));
   return client->callback(wsi, reason, user, in, len);
}

int WsDataConnection::callback(lws *wsi, int reason, void *user, void *in, size_t len)
{
   switch (reason) {
      case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
         if (!params_.useSsl || params_.caBundleSize == 0) {
            return 0;
         }
         auto sslCtx = static_cast<SSL_CTX*>(user);
         auto store = SSL_CTX_get_cert_store(sslCtx);
         auto bio = BIO_new_mem_buf(params_.caBundlePtr, static_cast<int>(params_.caBundleSize));
         while (true) {
            auto x = PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr);
            if (!x) {
               break;
            }
            int rc = X509_STORE_add_cert(store, x);
            assert(rc);
            X509_free(x);
         }
         BIO_free(bio);
         break;
      }

      case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
         {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!newPackets_.empty()) {
               allPackets_.insert(std::make_pair(queuedCounter_, std::move(newPackets_.front())));
               newPackets_.pop();
               queuedCounter_ += 1;
            }
         }
         if (!allPackets_.empty()) {
            if (state_ == State::Connected) {
               assert(wsi_);
               lws_callback_on_writable(wsi_);
            }
         }
         if (shuttingDown_.load() && !shuttingDownReceived_
             && ((wsi_ == nullptr) || (sentCounter_ == queuedCounter_) || (state_ != State::Connected))) {
            if (state_ == State::Connected && wsi_ != nullptr) {
               lws_close_reason(wsi_, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
               lws_set_timeout(wsi_, PENDING_TIMEOUT_USER_OK, LWS_TO_KILL_SYNC);
               state_ = State::Closing;
            } else {
               state_ = State::Closed;
            }
            shuttingDownReceived_ = true;
         }
         break;
      }

      case LWS_CALLBACK_CLIENT_RECEIVE: {
         if (wsi != wsi_) {
            return -1;
         }

         auto ptr = static_cast<const char*>(in);
         currFragment_.insert(currFragment_.end(), ptr, ptr + len);
         if (currFragment_.size() > params_.maximumPacketSize) {
            SPDLOG_LOGGER_ERROR(logger_, "maximum packet size reached");
            return -1;
         }
         if (lws_remaining_packet_payload(wsi) > 0) {
            return 0;
         }
         if (!lws_is_final_fragment(wsi)) {
            SPDLOG_LOGGER_ERROR(logger_, "unexpected fragment");
            processError();
            return -1;
         }

         auto packet = WsPacket::parsePacket(currFragment_, logger_);
         currFragment_.clear();

         switch (state_) {
            case State::Connecting:
            case State::Reconnecting: {
               SPDLOG_LOGGER_CRITICAL(logger_, "unexpected message");
               assert(false);
               return -1;
            }
            case State::WaitingNewResponse: {
               if (packet.type != WsPacket::Type::ResponseNew || packet.payload.empty()) {
                  SPDLOG_LOGGER_ERROR(logger_, "invalid response");
                  processError();
                  return -1;
               }
               SPDLOG_LOGGER_DEBUG(logger_, "connected");
               cookie_ = packet.payload;
               state_ = State::Connected;
               listener_->OnConnected();
               requestWriteIfNeeded();
               break;
            }
            case State::WaitingResumedResponse: {
               if (packet.type == WsPacket::Type::ResponseUnknown) {
                  SPDLOG_LOGGER_ERROR(logger_, "server responds that connection is not known or invalid");
                  processFatalError();
                  return -1;
               }
               if (packet.type != WsPacket::Type::ResponseResumed) {
                  SPDLOG_LOGGER_ERROR(logger_, "invalid response");
                  processError();
                  return -1;
               }
               if (!processSentAck(packet.recvCounter)) {
                  SPDLOG_LOGGER_DEBUG(logger_, "connection resuming failed");
                  return -1;
               }
               SPDLOG_LOGGER_DEBUG(logger_, "connection resumed succesfully");
               state_ = State::Connected;
               retryCounter_ = 0;
               sentCounter_ = packet.recvCounter;
               requestWriteIfNeeded();
               break;
            }
            case State::Closing:
            case State::Connected: {
               switch (packet.type) {
                  case WsPacket::Type::Ack: {
                     if (!processSentAck(packet.recvCounter)) {
                        return -1;
                     }
                     break;
                  }
                  case WsPacket::Type::Data: {
                     listener_->OnDataReceived(packet.payload);
                     recvCounter_ += 1;
                     break;
                  }
                  default: {
                     SPDLOG_LOGGER_ERROR(logger_, "unexpected packet");
                     processError();
                     return -1;
                  }
               }
               requestWriteIfNeeded();
               break;
            }
            case State::Closed: {
               return -1;
            }
         }

         break;
      }

      case LWS_CALLBACK_CLIENT_WRITEABLE: {
         if (wsi != wsi_) {
            return -1;
         }

         switch (state_) {
            case State::Reconnecting: {
               auto packet = filterRawPacket(WsPacket::requestResumed(cookie_, recvCounter_));
               int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
               if (rc == -1) {
                  SPDLOG_LOGGER_ERROR(logger_, "write failed");
                  processError();
                  return -1;
               }
               state_ = State::WaitingResumedResponse;
               break;
            }
            case State::Connecting: {
               auto packet = filterRawPacket(WsPacket::requestNew());
               int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
               if (rc == -1) {
                  SPDLOG_LOGGER_ERROR(logger_, "write failed");
                  processError();
                  return -1;
               }
               state_ = State::WaitingNewResponse;
               break;
            }
            case State::WaitingResumedResponse:
            case State::WaitingNewResponse: {
               // Nothing to do
               break;
            }
            case State::Closing:
            case State::Connected: {
               if (recvCounter_ != recvAckCounter_) {
                  auto packet = filterRawPacket(WsPacket::ack(recvCounter_));
                  int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
                  if (rc != static_cast<int>(packet.getSize())) {
                      SPDLOG_LOGGER_ERROR(logger_, "write failed");
                      processError();
                      return -1;
                  }
                  recvAckCounter_ = recvCounter_;
               } else if (sentCounter_ != queuedCounter_) {
                  // NOTE: Making packet copy here!
                  // LWS will mangle packet for WS masking purpose and thus packet won't be usable for retransmits after session resume.
                  auto packet = allPackets_.at(sentCounter_);
                  int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
                  if (rc == -1) {
                     SPDLOG_LOGGER_ERROR(logger_, "write failed");
                     processError();
                     return -1;
                  }
                  if (rc != static_cast<int>(packet.getSize())) {
                      SPDLOG_LOGGER_ERROR(logger_, "write truncated");
                      processError();
                      return -1;
                  }
                  sentCounter_ += 1;
               }
               requestWriteIfNeeded();
               break;
            }
            case State::Closed: {
               // Nothing to do
               break;
            }
         }
         break;
      }

      case LWS_CALLBACK_CLIENT_ESTABLISHED: {
         lws_callback_on_writable(wsi);
         break;
      }

      case LWS_CALLBACK_CLIENT_CLOSED: {
         if (wsi == wsi_) {
            processError();
         }
         return -1;
      }

      // LWS_CALLBACK_WSI_DESTROY is added to fix TestWebSocket.DISABLED_StressTest
      case LWS_CALLBACK_WSI_DESTROY:
      case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
         if (wsi == wsi_) {
            processError();
         }
         return -1;
      }

      case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: {
         uint16_t code;
         std::memcpy(&code, in, sizeof(code));
         code = htons(code);
         SPDLOG_LOGGER_DEBUG(logger_, "closing frame received with status code {}", code);
         if (code == LWS_CLOSE_STATUS_NORMAL) {
            switch (state_) {
               case State::Connected:
               case State::WaitingResumedResponse:
               case State::Reconnecting:
               case State::Closing:
                  listener_->OnDisconnected();
                  break;
               case State::Connecting:
               case State::WaitingNewResponse:
               case State::Closed:
                  listener_->OnError(DataConnectionListener::UndefinedSocketError);
                  break;
            }
            state_ = State::Closed;
         }
         return -1;
      }
   }

   return 0;
}

void WsDataConnection::reconnectCallback(lws_sorted_usec_list *list)
{
   auto data = static_cast<WsTimerStruct*>(list);
   data->owner_->reconnect();
}

void WsDataConnection::listenFunction()
{
   reconnect();

   while (state_ != State::Closed) {
      lws_service(context_, 0);
   }

   wsi_ = nullptr;
}

void WsDataConnection::scheduleReconnect()
{
   auto nextDelayMs = lws_retry_get_delay_ms(context_, retryTable_.get(), &retryCounter_, nullptr);
   SPDLOG_LOGGER_DEBUG(logger_, "schedule reconnect in {} ms, retry counter: {}", nextDelayMs, retryCounter_);
   lws_sul_schedule(context_, 0, reconnectTimer_.get(), reconnectCallback, static_cast<lws_usec_t>(nextDelayMs) * 1000);
}

void WsDataConnection::reconnect()
{
   SPDLOG_LOGGER_DEBUG(logger_, "try connect");
   assert(wsi_ == nullptr);

   struct lws_client_connect_info i;
   memset(&i, 0, sizeof(i));
   i.address = host_.c_str();
   i.host = i.address;
   i.port = port_;
   i.origin = i.address;
   i.path = "/";
   i.context = context_;
   i.protocol = kProtocolNameWs;
   i.userdata = this;
   i.ssl_connection = params_.useSsl ? LCCSCF_USE_SSL : 0;

   wsi_ = lws_client_connect_via_info(&i);
}

void WsDataConnection::processError()
{
   wsi_ = nullptr;

   if (retryCounter_ >= retryTable_->retry_ms_table_count) {
      SPDLOG_LOGGER_ERROR(logger_, "too many reconnect retries failed");
      processFatalError();
      return;
   }

   switch (state_) {
      case State::WaitingNewResponse:
      case State::Connecting: {
         state_ = State::Connecting;
         scheduleReconnect();
         break;
      }

      case State::Reconnecting:
      case State::Connected:
      case State::WaitingResumedResponse: {
         state_ = State::Reconnecting;
         scheduleReconnect();
         break;
      }
      case State::Closing: {
         listener_->OnDisconnected();
         state_ = State::Closed;
         break;
      }
      case State::Closed: {
         break;
      }
   }
}

void WsDataConnection::processFatalError()
{
   switch (state_) {
      case State::Connecting:
      case State::WaitingNewResponse:
         listener_->OnError(DataConnectionListener::UndefinedSocketError);
         break;

      case State::Closing:
         listener_->OnDisconnected();
         break;

      case State::Reconnecting:
      case State::Connected:
      case State::WaitingResumedResponse:
         listener_->OnDisconnected();
         listener_->OnError(DataConnectionListener::UndefinedSocketError);
         break;

      case State::Closed:
         break;
   }

   state_ = State::Closed;
   wsi_ = nullptr;
}

bool WsDataConnection::writeNeeded() const
{
   switch (state_) {
      case State::Connected:
         return sentCounter_ != queuedCounter_ || recvCounter_ != recvAckCounter_;
      case State::Connecting:
      case State::Reconnecting:
      case State::WaitingNewResponse:
      case State::WaitingResumedResponse:
      case State::Closed:
      case State::Closing:
         return false;
   }
   assert(false);
   return false;
}

void WsDataConnection::requestWriteIfNeeded()
{
   if (writeNeeded() && wsi_ != nullptr) {
      lws_callback_on_writable(wsi_);
   }
   if (shuttingDown_) {
      lws_cancel_service(context_);
   }
}

bool WsDataConnection::processSentAck(uint64_t sentAckCounter)
{
   if (sentAckCounter < sentAckCounter_ || sentAckCounter > sentCounter_) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid ack value from server");
      processFatalError();
      return false;
   }

   while (sentAckCounter_ < sentAckCounter) {
      size_t count = allPackets_.erase(sentAckCounter_);
      assert(count == 1);
      sentAckCounter_ += 1;
   }

   return true;
}

WsRawPacket WsDataConnection::filterRawPacket(WsRawPacket packet)
{
   return packet;
}
