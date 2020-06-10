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

   const auto kPeriodicCheckTimeout = std::chrono::seconds(120);

   int callback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
   {
      return WsDataConnection::callbackHelper(wsi, reason, user, in, len);
   }

   struct lws_protocols kProtocols[] = {
      { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

} // namespace

WsDataConnection::WsDataConnection(const std::shared_ptr<spdlog::logger> &logger
   , WsDataConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
{
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
   stopped_ = false;

   struct lws_context_creation_info info;
   memset(&info, 0, sizeof(info));

   info.port = CONTEXT_PORT_NO_LISTEN;
   info.protocols = kProtocols;
   info.gid = -1;
   info.uid = -1;
   info.ws_ping_pong_interval = kPingPongInterval / std::chrono::seconds(1);
   info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
   info.user = this;

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

   stopped_ = true;
   lws_cancel_service(context_);

   listenThread_.join();

   lws_context_destroy(context_);
   context_ = nullptr;
   listener_ = nullptr;
   newPackets_ = {};
   allPackets_ = {};
   currFragment_ = {};

   return true;
}

bool WsDataConnection::send(const std::string &data)
{
   {  std::lock_guard<std::recursive_mutex> lock(mutex_);
      newPackets_.push(WsPacket(data));
   }
   lws_cancel_service(context_);
   return true;
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
         if (params_.caBundleSize == 0) {
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
         {  std::lock_guard<std::recursive_mutex> lock(mutex_);
            while (!newPackets_.empty()) {
               allPackets_.push(std::move(newPackets_.front()));
               newPackets_.pop();
            }
         }
         if (!allPackets_.empty()) {
            if (wsi_) {
               lws_callback_on_writable(wsi_);
            }
         }
         break;
      }

      case LWS_CALLBACK_CLIENT_RECEIVE: {
         auto ptr = static_cast<const char*>(in);
         currFragment_.insert(currFragment_.end(), ptr, ptr + len);
         if (lws_remaining_packet_payload(wsi) > 0) {
            return 0;
         }
         if (!lws_is_final_fragment(wsi)) {
            SPDLOG_LOGGER_ERROR(logger_, "unexpected fragment");
            reportFatalError(DataConnectionListener::ProtocolViolation);
            return -1;
         }
         listener_->OnDataReceived(currFragment_);
         currFragment_.clear();
         break;
      }

      case LWS_CALLBACK_CLIENT_WRITEABLE: {
         if (allPackets_.empty()) {
            return 0;
         }
         auto packet = std::move(allPackets_.front());
         allPackets_.pop();
         int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
         if (rc == -1) {
            SPDLOG_LOGGER_ERROR(logger_, "write failed");
            reportFatalError(DataConnectionListener::UndefinedSocketError);
            return -1;
         }
         if (rc != static_cast<int>(packet.getSize())) {
             SPDLOG_LOGGER_ERROR(logger_, "write truncated");
             reportFatalError(DataConnectionListener::UndefinedSocketError);
             return -1;
         }
         if (!allPackets_.empty()) {
            lws_callback_on_writable(wsi);
         }
         break;
      }

      case LWS_CALLBACK_CLIENT_ESTABLISHED: {
         listener_->OnConnected();
         break;
      }

      case LWS_CALLBACK_CLIENT_CLOSED: {
         listener_->OnDisconnected();
         break;
      }

      case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
         listener_->OnError(DataConnectionListener::UndefinedSocketError);
         break;
      }
   }

   return 0;
}

void WsDataConnection::listenFunction()
{
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

   i.ssl_connection = LCCSCF_USE_SSL;

   wsi_ = lws_client_connect_via_info(&i);

   while (!stopped_.load()) {
      lws_service(context_, kPeriodicCheckTimeout / std::chrono::milliseconds(1));
   }

   wsi_ = nullptr;
}

void WsDataConnection::onRawDataReceived(const std::string &rawData)
{
}

void WsDataConnection::reportFatalError(DataConnectionListener::DataConnectionError error)
{
   stopped_ = true;
   lws_cancel_service(context_);
   wsi_ = nullptr;
   listener_->OnError(error);
}
