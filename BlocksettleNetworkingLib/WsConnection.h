/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_CONNECTION_H
#define WS_CONNECTION_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <botan/secmem.h>

namespace spdlog {
   class logger;
}

struct lws;
struct lws_retry_bo;
struct x509_store_ctx_st;

namespace bs {
   namespace network {
      namespace ws {

         using PrivateKey = Botan::SecureVector<uint8_t>;

         constexpr size_t kDefaultMaximumWsPacketSize = 100 * 1024 * 1024;

         const lws_retry_bo *defaultRetryAndIdlePolicy();

         std::string connectedIp(lws *wsi);

         // NOTE: Not available after LWS_CALLBACK_ESTABLISHED
         std::string forwardedIp(lws *wsi);

         std::string certPublicKey(const std::shared_ptr<spdlog::logger> &logger, x509_store_ctx_st *ctx);

         long sslOptionsSet();

         // Generates secp256r1 private key (in DER format)
         PrivateKey generatePrivKey();

         // Returns compressed public key (33 bytes)
         std::string publicKey(const PrivateKey &privKey);

         // Generate self-signed cerificate for the privKey
         std::string generateSelfSignedCert(const PrivateKey &privKey
            , const std::chrono::seconds &expireTime = std::chrono::hours(20 * 365 * 24));

      }
      class WsRawPacket
      {
      private:
         // Actual data padded by LWS_PRE
         std::vector<uint8_t> data_;

      public:
         explicit WsRawPacket(const std::string &data);

         uint8_t *getPtr();

         size_t getSize() const;
      };

      extern const char *kProtocolNameWs;

      constexpr size_t kRxBufferSize = 16 * 1024;
      constexpr size_t kTxPacketSize = 16 * 1024;
      constexpr int kId = 0;

      struct WsPacket
      {
         enum class Type : uint8_t
         {
            Invalid = 0,
            RequestNew = 0x11,
            RequestResumed = 0x12,
            ResponseNew = 0x13,
            ResponseResumed = 0x14,
            ResponseUnknown = 0x15,
            Data = 0x16,
            Ack = 0x17,

            Min = RequestNew,
            Max = Ack,
         };

         Type type{};
         std::string payload;
         uint64_t recvCounter{};

         static WsRawPacket requestNew();
         static WsRawPacket requestResumed(const std::string &cookie, uint64_t recvCounter);
         static WsRawPacket responseNew(const std::string &cookie);
         static WsRawPacket responseResumed(uint64_t recvCounter);
         static WsRawPacket responseUnknown();
         static WsRawPacket data(const std::string &payload);
         static WsRawPacket ack(uint64_t recvCounter);

         static WsPacket parsePacket(const std::string &payload
            , const std::shared_ptr<spdlog::logger> &logger);
      };

   }
}

#endif // WS_CONNECTION_H
