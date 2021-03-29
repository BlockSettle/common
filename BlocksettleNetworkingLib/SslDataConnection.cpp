/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SslDataConnection.h"

#include "StringUtils.h"

#include <libwebsockets.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <spdlog/spdlog.h>

using namespace bs::network;

namespace {

   int callback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
   {
      return SslDataConnection::callbackHelper(wsi, reason, user, in, len);
   }

   struct lws_protocols kProtocols[] = {
      { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

} // namespace

SslDataConnection::SslDataConnection(const std::shared_ptr<spdlog::logger> &logger
   , SslDataConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
{
   assert(params_.useSsl || (params_.caBundlePtr == nullptr));
   assert(params_.useSsl || (params_.caBundleSize == 0));
   assert(params_.useSsl || !params_.verifyCallback);
   assert(params_.useSsl || params_.cert.empty());
   assert(params_.useSsl || params_.privKey.empty());

   ws::globalInit(params_.useSsl);
}

SslDataConnection::~SslDataConnection()
{
   closeConnection();
}

bool SslDataConnection::openConnectionWithPath(const std::string& host, const std::string& port
                               , const std::string& path
                               , DataConnectionListener* listener)
{
   path_ = path;
   return openConnection(host, port, listener);
}

bool SslDataConnection::openConnection(const std::string &host, const std::string &port
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
   info.retry_and_idle_policy = bs::network::ws::defaultRetryAndIdlePolicy();
   info.options = params_.useSsl ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT : 0;
   info.user = this;
   info.ssl_options_set = params_.useSsl ? ws::sslOptionsSet() : 0;

   context_ = lws_create_context(&info);
   if (!context_) {
      SPDLOG_LOGGER_ERROR(logger_, "context create failed");
      return false;
   }

   vhost_ = lws_create_vhost(context_, &info);

   lws_init_vhost_client_ssl(&info, vhost_);

   listenThread_ = std::thread(&SslDataConnection::listenFunction, this);

   return true;
}

bool SslDataConnection::closeConnection()
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

bool SslDataConnection::send(const std::string &data)
{
   if (!context_) {
      return false;
   }
   {  std::lock_guard<std::recursive_mutex> lock(mutex_);
      newPackets_.push(WsRawPacket(data));
   }
   lws_cancel_service(context_);
   return true;
}

bool SslDataConnection::isActive() const
{
   return context_ != nullptr;
}

int SslDataConnection::callbackHelper(lws *wsi, int reason, void *user, void *in, size_t len)
{
   auto context = lws_get_context(wsi);
   auto client = static_cast<SslDataConnection*>(lws_context_user(context));
   return client->callback(wsi, reason, user, in, len);
}

int SslDataConnection::callback(lws *wsi, int reason, void *user, void *in, size_t len)
{
   switch (reason) {
      case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION: {
         if (!params_.verifyCallback) {
            return 0;
         }
         auto ctx = static_cast<X509_STORE_CTX*>(user);
         auto pubKey = ws::certPublicKey(logger_, ctx);
         if (pubKey.empty()) {
            SPDLOG_LOGGER_ERROR(logger_, "can't get public key");
            reportFatalError(DataConnectionListener::HandshakeFailed);
            return -1;
         }
         bool verifyResult = params_.verifyCallback(pubKey);
         if (!verifyResult) {
            reportFatalError(DataConnectionListener::HandshakeFailed);
            SPDLOG_LOGGER_DEBUG(logger_, "drop connection, pubKey: {}", bs::toHex(pubKey));
            return -1;
         }
         SPDLOG_LOGGER_DEBUG(logger_, "accept connection, pubKey: {}", bs::toHex(pubKey));
         return 0;
      }

      case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
         auto ctx = static_cast<SSL_CTX*>(user);

         if (!params_.cert.empty()) {
            if (!SSL_CTX_use_certificate_ASN1(ctx, static_cast<int>(params_.cert.size())
               , reinterpret_cast<const uint8_t*>(params_.cert.data()))) {
               SPDLOG_LOGGER_ERROR(logger_, "SSL_CTX_use_certificate_ASN1 failed");
               reportFatalError(DataConnectionListener::HandshakeFailed);
               return -1;
            }
            if (!SSL_CTX_use_PrivateKey_ASN1(EVP_PKEY_EC, ctx
               , reinterpret_cast<const uint8_t*>(params_.privKey.data()), static_cast<int>(params_.privKey.size()))) {
               SPDLOG_LOGGER_ERROR(logger_, "SSL_CTX_use_PrivateKey_ASN1 failed");
               reportFatalError(DataConnectionListener::HandshakeFailed);
               return -1;
            }
         }

         if (params_.caBundlePtr) {
            auto store = SSL_CTX_get_cert_store(ctx);
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
         }
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
         int rc = lws_write(wsi, packet.getPtr(), packet.getSize()
            , params_.sendAsText ? LWS_WRITE_TEXT : LWS_WRITE_BINARY);
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
         if (in) {
            SPDLOG_LOGGER_ERROR(logger_, "Connection error: {}", (const char*)in);
         } else {
            SPDLOG_LOGGER_ERROR(logger_, "undefined socket connection error");
         }
         reportFatalError(DataConnectionListener::UndefinedSocketError);
         break;
      }
   }

   return 0;
}

void SslDataConnection::listenFunction()
{
   struct lws_client_connect_info i;
   memset(&i, 0, sizeof(i));
   i.address = host_.c_str();
   i.host = i.address;
   i.port = port_;
   i.origin = i.address;
   i.path = path_.c_str();
   i.context = context_;
   i.protocol = kProtocolNameWs;
   i.userdata = this;
   i.vhost = vhost_;

   i.ssl_connection = 0;
   i.ssl_connection |= params_.useSsl ? LCCSCF_USE_SSL : 0;
   i.ssl_connection |= params_.allowSelfSigned ? LCCSCF_ALLOW_SELFSIGNED : 0;
   i.ssl_connection |= params_.skipHostNameChecks ? LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK : 0;

   wsi_ = lws_client_connect_via_info(&i);

   while (!stopped_.load()) {
      lws_service(context_, 0);
   }

   wsi_ = nullptr;
}

void SslDataConnection::reportFatalError(DataConnectionListener::DataConnectionError error)
{
   stopped_ = true;
   lws_cancel_service(context_);
   wsi_ = nullptr;
   listener_->OnError(error);
}
