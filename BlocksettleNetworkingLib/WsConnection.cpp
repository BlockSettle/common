/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WsConnection.h"

#include <libwebsockets.h>
#include <spdlog/spdlog.h>

#include <botan/auto_rng.h>
#include <botan/data_src.h>
#include <botan/der_enc.h>
#include <botan/ecdsa.h>
#include <botan/pkcs8.h>
#include <botan/x509self.h>

#include "BinaryData.h"
#include "StringUtils.h"
#include "ZmqHelperFunctions.h"

using namespace bs::network;

const char *bs::network::kProtocolNameWs = "bs-ws-protocol";

namespace {

   constexpr size_t kLwsPrePaddingSize = LWS_PRE;

   constexpr auto kDefaultCurve = NID_X9_62_prime256v1;
   const Botan::EC_Group kDefaultDomain("secp256r1");
   const auto kDefaultHash = "SHA-256";

   class WsRawPacketBuilder
   {
   private:
      BinaryWriter w;

   public:
      WsRawPacketBuilder(WsPacket::Type type)
      {
         w.put_uint8_t(static_cast<uint8_t>(type));
      }

      WsRawPacketBuilder &putString(const std::string &data)
      {
         w.put_var_int(data.size());
         w.put_String(data);
         return *this;
      }

      WsRawPacketBuilder &putNumber(uint64_t n)
      {
         w.put_var_int(n);
         return *this;
      }

      WsRawPacket build()
      {
         return WsRawPacket(w.toString());
      }
   };

   constexpr auto kPingPongInterval = std::chrono::seconds(60);
   constexpr auto kHungupInterval = std::chrono::seconds(90);
   const lws_retry_bo kDefaultRetryAndIdlePolicy = { nullptr, 0, 0
      , kPingPongInterval / std::chrono::seconds(1), kHungupInterval / std::chrono::seconds(1), 0 };

}

WsRawPacket::WsRawPacket(const std::string &data)
{
   data_.resize(kLwsPrePaddingSize);
   data_.insert(data_.end(), data.begin(), data.end());
}

uint8_t *WsRawPacket::getPtr()
{
   return data_.data() + kLwsPrePaddingSize;
}

size_t WsRawPacket::getSize() const
{
   return data_.size() - kLwsPrePaddingSize;
}

using namespace bs::network;

WsRawPacket WsPacket::requestNew()
{
   return WsRawPacketBuilder(Type::RequestNew)
         .build();
}

WsRawPacket WsPacket::requestResumed(const std::string &cookie, uint64_t recvCounter)
{
   return WsRawPacketBuilder(Type::RequestResumed)
         .putNumber(recvCounter)
         .putString(cookie)
         .build();
}

WsRawPacket WsPacket::responseNew(const std::string &cookie)
{
   return WsRawPacketBuilder(Type::ResponseNew)
         .putString(cookie)
         .build();
}

WsRawPacket WsPacket::responseResumed(uint64_t recvCounter)
{
   return WsRawPacketBuilder(Type::ResponseResumed)
         .putNumber(recvCounter)
         .build();
}

WsRawPacket WsPacket::responseUnknown()
{
   return WsRawPacketBuilder(Type::ResponseUnknown)
         .build();
}

WsRawPacket WsPacket::data(const std::string &payload)
{
   return WsRawPacketBuilder(Type::Data)
         .putString(payload)
         .build();
}

WsRawPacket WsPacket::ack(uint64_t recvCounter)
{
   return WsRawPacketBuilder(Type::Ack)
         .putNumber(recvCounter)
         .build();
}

WsPacket WsPacket::parsePacket(const std::string &data, const std::shared_ptr<spdlog::logger> &logger)
{
   try {
      WsPacket result;
      BinaryRefReader r(reinterpret_cast<const uint8_t*>(data.data()), data.size());

      result.type = static_cast<Type>(r.get_uint8_t());
      if (result.type < Type::Min || result.type > Type::Max) {
         throw std::runtime_error("invalid packet type");
      }

      switch (result.type) {
         case Type::RequestResumed:
         case Type::ResponseResumed:
         case Type::Ack:
            result.recvCounter = r.get_var_int();
            break;
         default:
            break;
      }

      switch (result.type) {
         case Type::RequestResumed:
         case Type::ResponseNew:
         case Type::Data: {
            auto payloadSize = r.get_var_int();
            if (r.getSizeRemaining() < payloadSize) {
               throw std::runtime_error("invalid packet");
            }
            result.payload = r.get_String(static_cast<uint32_t>(payloadSize));
            break;
         }
         default:
            break;
      }

      if (!r.isEndOfStream()) {
         throw std::runtime_error("expecting end of stream");
      }

      return result;
   } catch (const std::exception &e) {
      SPDLOG_LOGGER_ERROR(logger, "invalid packet: {}", e.what());
      return {};
   }
}

const lws_retry_bo *ws::defaultRetryAndIdlePolicy()
{
   return &kDefaultRetryAndIdlePolicy;
}

std::string ws::connectedIp(lws *wsi)
{
   auto socket = lws_get_socket_fd(wsi);
   auto ipAddr = bs::network::peerAddressString(socket);
   return ipAddr;
}

std::string ws::forwardedIp(lws *wsi)
{
   int n = lws_hdr_total_length(wsi, WSI_TOKEN_X_FORWARDED_FOR);
   if (n < 0) {
      return "";
   }
   std::string value;
   value.resize(static_cast<size_t>(n + 1));
   n = lws_hdr_copy(wsi, &value[0], static_cast<int>(value.size()), WSI_TOKEN_X_FORWARDED_FOR);
   if (n < 0) {
      assert(false);
      return "";
   }
   value.resize(static_cast<size_t>(n));
   auto ipAddr = bs::trim(bs::split(value, ',').back());
   return ipAddr;
}

std::string ws::certPublicKey(const std::shared_ptr<spdlog::logger> &logger_, x509_store_ctx_st *ctx)
{
   auto currCert = X509_STORE_CTX_get_current_cert(ctx);
   if (!currCert) {
      SPDLOG_LOGGER_ERROR(logger_, "X509_STORE_CTX_get_current_cert failed");
      return {};
   }
   auto pubKey = X509_get0_pubkey(currCert);
   if (!pubKey) {
      SPDLOG_LOGGER_ERROR(logger_, "X509_get0_pubkey failed");
      return {};
   }
   auto ecKey = EVP_PKEY_get0_EC_KEY(pubKey);
   if (!ecKey) {
      SPDLOG_LOGGER_ERROR(logger_, "EVP_PKEY_get0_EC_KEY failed");
      return {};
   }
   auto point = EC_KEY_get0_public_key(ecKey);
   if (!point) {
      SPDLOG_LOGGER_ERROR(logger_, "EC_KEY_get0_public_key failed");
      return {};
   }
   auto group = EC_KEY_get0_group(ecKey);
   if (!group) {
      SPDLOG_LOGGER_ERROR(logger_, "EC_KEY_get0_group failed");
      return {};
   }
   int curveName = EC_GROUP_get_curve_name(group);
   if (curveName != kDefaultCurve) {
      SPDLOG_LOGGER_ERROR(logger_, "unexpected curve name: {}", curveName);
      return {};
   }
   auto size = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, nullptr, 0, nullptr);
   if (size == 0) {
      SPDLOG_LOGGER_ERROR(logger_, "EC_POINT_point2oct failed");
      return {};
   }
   std::string result(size, 0);
   size = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, reinterpret_cast<uint8_t*>(&result[0]), result.size(), nullptr);
   if (size != result.size()) {
      SPDLOG_LOGGER_ERROR(logger_, "EC_POINT_point2oct failed");
      return {};
   }
   return result;
}

ws::PrivateKey ws::generatePrivKey()
{
   Botan::AutoSeeded_RNG rng;
   auto privKey = Botan::ECDSA_PrivateKey(rng, kDefaultDomain);
   return Botan::PKCS8::BER_encode(privKey);
}

std::string ws::generateSelfSignedCert(const PrivateKey &privKey, const std::chrono::seconds &expireTime)
{
   Botan::AutoSeeded_RNG rng;
   Botan::DataSource_Memory privKeySrc(privKey);
   auto privKeyLoaded = Botan::PKCS8::load_key(privKeySrc);
   auto expireTimeSeconds = expireTime / std::chrono::seconds(1);
   Botan::X509_Cert_Options options("", static_cast<uint32_t>(expireTimeSeconds));
   auto cert = Botan::X509::create_self_signed_cert(options, *privKeyLoaded, kDefaultHash, rng);
   std::vector<uint8_t> output;
   Botan::DER_Encoder encoder(output);
   cert.encode_into(encoder);
   return std::string(output.begin(), output.end());
}

std::string ws::publicKey(const ws::PrivateKey &privKey)
{
   Botan::DataSource_Memory privKeySrc(privKey);
   auto privKeyLoaded = Botan::PKCS8::load_key(privKeySrc);
   auto privKeyEc = dynamic_cast<Botan::EC_PrivateKey*>(privKeyLoaded.get());
   auto publicKey = privKeyEc->public_point().encode(Botan::PointGFp::COMPRESSED);
   return std::string(publicKey.begin(), publicKey.end());
}

long ws::sslOptionsSet()
{
   // Allow TLSv1_3 only
   return
         SSL_OP_NO_SSLv2 |
         SSL_OP_NO_SSLv3 |
         SSL_OP_NO_TLSv1 |
         SSL_OP_NO_TLSv1_1 |
         SSL_OP_NO_TLSv1_2;
}
