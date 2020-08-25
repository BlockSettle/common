/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SslServerConnection.h"

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
      return SslServerConnection::callbackHelper(wsi, reason, user, in, len);
   }

   struct lws_protocols kProtocols[] = {
      { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

   const std::string kAllClientsId = "<TO_ALL>";

} // namespace

SslServerConnection::SslServerConnection(const std::shared_ptr<spdlog::logger>& logger, SslServerConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
{
   assert(params_.useSsl != params_.privKey.empty());
   assert(params_.useSsl != params_.cert.empty());
   assert(params_.useSsl || !params_.requireClientCert);
   assert(params_.useSsl || !params_.verifyCallback);
   assert(bool(params_.verifyCallback) == params_.requireClientCert);
}

SslServerConnection::~SslServerConnection()
{
   stopServer();
}

bool SslServerConnection::BindConnection(const std::string& host , const std::string& port
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
   info.options = 0;
   info.options |= LWS_SERVER_OPTION_VALIDATE_UTF8;
   info.options |= LWS_SERVER_OPTION_DISABLE_IPV6;
   info.options |= params_.useSsl ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT : 0;
   info.options |= params_.requireClientCert ? LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT : 0;
   info.ssl_options_set = params_.useSsl ? ws::sslOptionsSet() : 0;
   info.user = this;

   info.server_ssl_private_key_mem = params_.privKey.data();
   info.server_ssl_private_key_mem_len = static_cast<uint32_t>(params_.privKey.size());
   info.server_ssl_cert_mem = params_.cert.data();
   info.server_ssl_cert_mem_len = static_cast<uint32_t>(params_.cert.size());

   // Context creation will return nullptr if port binding failed
   context_ = lws_create_context(&info);
   if (context_ == nullptr) {
      SPDLOG_LOGGER_ERROR(logger_, "context create failed");
      return false;
   }

   stopped_ = false;
   listener_ = listener;

   listenThread_ = std::thread(&SslServerConnection::listenFunction, this);

   return true;
}

void SslServerConnection::listenFunction()
{
   bs::setCurrentThreadName("WsServer");

   while (!stopped_.load()) {
      lws_service(context_, 0);
   }
}

void SslServerConnection::stopServer()
{
   if (!listenThread_.joinable()) {
      return;
   }

   stopped_ = true;
   lws_cancel_service(context_);

   listenThread_.join();

   lws_context_destroy(context_);
   listener_ = nullptr;
   context_ = nullptr;
   packets_ = {};
   clients_ = {};
   socketToClientIdMap_ = {};
}

int SslServerConnection::callback(lws *wsi, int reason, void *user, void *in, size_t len)
{
   switch (reason) {
      case LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION: {
         if (!params_.verifyCallback) {
            return 0;
         }
         auto ctx = static_cast<X509_STORE_CTX*>(user);
         auto pubKey = ws::certPublicKey(logger_, ctx);
         if (pubKey.empty()) {
            SPDLOG_LOGGER_ERROR(logger_, "can't get public key");
            return -1;
         }
         bool verifyResult = params_.verifyCallback(pubKey);
         if (!verifyResult) {
            SPDLOG_LOGGER_DEBUG(logger_, "drop connection, pubKey: {}", bs::toHex(pubKey));
            return -1;
         }
         SPDLOG_LOGGER_DEBUG(logger_, "accept connection, pubKey: {}", bs::toHex(pubKey));
         return 0;
      }
      case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
         std::queue<WsServerDataToSend> packets;
         {  std::lock_guard<std::recursive_mutex> lock(mutex_);
            std::swap(packets, packets_);
         }
         while (!packets.empty()) {
            auto data = std::move(packets.front());
            packets.pop();

            if (data.clientId == kAllClientsId) {
               for (auto &item : clients_) {
                  auto &client = item.second;
                  client.packets.push(data.packet);
                  lws_callback_on_writable(client.wsi);
               }
               continue;
            }

            auto clientIt = clients_.find(data.clientId);
            if (clientIt == clients_.end()) {
               SPDLOG_LOGGER_DEBUG(logger_, "send failed, client {} already disconnected", bs::toHex(data.clientId));
               continue;
            }
            auto &client = clientIt->second;
            client.packets.push(std::move(data.packet));
            lws_callback_on_writable(client.wsi);
         }
         break;
      }

      case LWS_CALLBACK_ESTABLISHED: {
         auto clientId = nextClientId();
         auto &client = clients_[clientId];
         client.wsi = wsi;
         client.clientId = clientId;

         auto connIp = bs::network::ws::connectedIp(wsi);
         auto forwIp = bs::network::ws::forwardedIp(wsi);
         auto ipAddr = params_.trustForwardedForHeader && !forwIp.empty() ? forwIp : connIp;

         ServerConnectionListener::Details details;
         details[ServerConnectionListener::Detail::IpAddr] = ipAddr;

         socketToClientIdMap_[wsi] = clientId;
         SPDLOG_LOGGER_DEBUG(logger_, "wsi connected: {}, connected ip: {}, forwarded ip: {}"
            , static_cast<void*>(wsi), connIp, forwIp);
         listener_->OnClientConnected(clientId, details);
         break;
      }

      case LWS_CALLBACK_CLOSED: {
         auto clientId = socketToClientIdMap_.at(wsi);
         SPDLOG_LOGGER_DEBUG(logger_, "client disconnected: {}", bs::toHex(clientId));
         listener_->OnClientDisconnected(clientId);
         socketToClientIdMap_.erase(wsi);
         size_t count = clients_.erase(clientId);
         assert(count == 1);
         break;
      }

      case LWS_CALLBACK_RECEIVE: {
         auto clientId = socketToClientIdMap_.at(wsi);
         auto &client = clients_.at(clientId);
         auto ptr = static_cast<const char*>(in);
         client.currFragment.insert(client.currFragment.end(), ptr, ptr + len);
         if (lws_remaining_packet_payload(wsi) > 0) {
            return 0;
         }
         if (!lws_is_final_fragment(wsi)) {
            SPDLOG_LOGGER_ERROR(logger_, "unexpected fragment");
            return -1;
         }
         listener_->OnDataFromClient(clientId, client.currFragment);
         client.currFragment.clear();
         break;
      }

      case LWS_CALLBACK_SERVER_WRITEABLE: {
         auto clientId = socketToClientIdMap_.at(wsi);
         auto &client = clients_.at(clientId);
         if (client.packets.empty()) {
            return 0;
         }

         auto packet = std::move(client.packets.front());
         client.packets.pop();

         int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
         if (rc == -1) {
            SPDLOG_LOGGER_ERROR(logger_, "write failed");
            return -1;
         }
         if (rc != static_cast<int>(packet.getSize())) {
             SPDLOG_LOGGER_ERROR(logger_, "write truncated");
             return -1;
         }

         if (!client.packets.empty()) {
            lws_callback_on_writable(client.wsi);
         }

         break;
      }
   }

   return 0;
}

std::string SslServerConnection::nextClientId()
{
   nextClientId_ += 1;
   auto ptr = reinterpret_cast<char*>(&nextClientId_);
   return std::string(ptr, ptr + sizeof(nextClientId_));
}

bool SslServerConnection::SendDataToClient(const std::string &clientId, const std::string &data)
{
   WsServerDataToSend toSend{clientId, WsRawPacket(data)};
   {  std::lock_guard<std::recursive_mutex> lock(mutex_);
      packets_.push(std::move(toSend));
   }
   lws_cancel_service(context_);
   return true;
}

bool SslServerConnection::SendDataToAllClients(const std::string &data)
{
   return SendDataToClient(kAllClientsId, data);
}

int SslServerConnection::callbackHelper(lws *wsi, int reason, void *user, void *in, size_t len)
{
   auto context = lws_get_context(wsi);
   auto server = static_cast<SslServerConnection*>(lws_context_user(context));
   return server->callback(wsi, reason, user, in, len);
}
