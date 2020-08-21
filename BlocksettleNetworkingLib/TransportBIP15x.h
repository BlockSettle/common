/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __TRANSPORT_BIP15X_H__
#define __TRANSPORT_BIP15X_H__

#include <deque>
#include <functional>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>

#include "AuthorizedPeers.h"
#include "BIP150_151.h"
#include "BIP15xHelpers.h"
#include "BIP15xMessage.h"
#include "Transport.h"

// DESIGN NOTES: Remote data connections must have a callback for when unknown
// server keys are seen. The callback should ask the user if they'll accept
// the new key, or otherwise properly handle new keys arriving.
//
// Cookies are used for local connections, and are the default unless remote
// callbacks are added. When the server is invoked by a binary containing a
// client connection, the binary must be invoked with the client connection's
// public BIP 150 ID key. In turn, the binary with the server connection must
// generate a cookie with its public BIP 150 ID key. The client will read the
// cookie and get the server key. This allows both sides to verify each other.
//
// When adding authorized keys to a connection, the name needs to be the
// IP:Port of the server connection. This is the only reliable information
// available to the connection that can be used to ID who's on the other side.
// It's okay to use other names in the GUI and elsewhere. However, the IP:Port
// must be used when searching for keys.
//
// The key acceptance functionality is as follows:
//
// LOCAL SIGNER
// Accept only a single key from the server cookie.
//
// REMOTE SIGNER
// New key + No callbacks - Reject the new keys.
// New key + Callbacks - Depends on what the user wants.
// Previously verified key - Accept the key and skip the callbacks.

template<typename T> class FutureValue;

namespace bs {
   namespace network {

      enum class BIP15xCookie
      {
         // Cookie won't be used
         NotUsed,

         // Connection will make a key cookie
         MakeClient,

         // Connection will read a key cookie (server's public key)
         ReadServer,
      };

      struct BIP15xParams
      {
         // The directory containing the file with the non-ephemeral key
         std::string ownKeyFileDir;

         // The file name with the non-ephemeral key
         std::string ownKeyFileName;

         // File where cookie will be stored or read from.
         // Must be set cookie is used.
         std::string cookiePath{};

         // Ephemeral peer usage. Not recommended
         bool ephemeralPeers{ false };

         BinaryData serverPublicKey;
         BIP15xCookie cookie{ BIP15xCookie::NotUsed };

         std::chrono::milliseconds connectionTimeout{ std::chrono::seconds(10) };
      };


      class TransportBIP15x
      {
      public:
         TransportBIP15x(const std::shared_ptr<spdlog::logger> &
            , const std::string &cookiePath);
         virtual ~TransportBIP15x() noexcept = default;

         TransportBIP15x(const TransportBIP15x&) = delete;
         TransportBIP15x& operator= (const TransportBIP15x&) = delete;
         TransportBIP15x(TransportBIP15x&&) = delete;
         TransportBIP15x& operator= (TransportBIP15x&&) = delete;

         BinaryData getOwnPubKey() const;
         void addAuthPeer(const BIP15xPeer &);
         void updatePeerKeys(const BIP15xPeers &);

         static BinaryData getOwnPubKey(const std::string &ownKeyFileDir, const std::string &ownKeyFileName);
         static BinaryData getOwnPubKey(const AuthorizedPeers &authPeers);

      protected:
         virtual bool getCookie(BinaryData &cookieBuf);
         virtual bool createCookie();  // Cookie file is held opened for writing
                                       // for the whole object lifetime
         bool rmCookieFile();

         bool addCookieToPeers(const std::string &id, const BinaryData &pubKey = {});
         AuthPeersLambdas getAuthPeerLambda();

         using WriteDataCb = std::function<bool(bip15x::MsgType, const BinaryData &
            , bool encrypt)>;
         bool processAEAD(const bip15x::Message &, const std::unique_ptr<BIP151Connection> &
            , const WriteDataCb &, bool requesterSent);

         bool fail();
         bool isValid() const { return isValid_; }

      protected:
         std::shared_ptr<spdlog::logger>  logger_;
         std::unique_ptr<AuthorizedPeers> authPeers_;
         mutable std::mutex authPeersMutex_;
         std::string cookiePath_;

      private:
         bool isValid_{ true };
         std::unique_ptr<std::ofstream>   cookieFile_;   //Need to keep opened for the whole object lifetime
      };


      class TransportBIP15xClient : public TransportBIP15x, public TransportClient
      {
      public:
         TransportBIP15xClient(const std::shared_ptr<spdlog::logger> &, const BIP15xParams &);
         ~TransportBIP15xClient() noexcept override;

         TransportBIP15xClient(const TransportBIP15xClient&) = delete;
         TransportBIP15xClient& operator= (const TransportBIP15xClient&) = delete;
         TransportBIP15xClient(TransportBIP15xClient&&) = delete;
         TransportBIP15xClient& operator= (TransportBIP15xClient&&) = delete;

         void setKeyCb(const BIP15xNewKeyCb &);
         bool getCookie(BinaryData &cookieBuf) override;

         std::string listenThreadName() const override { return "listenBIP15x"; }

         // thread-safe (could be called from callbacks too)
         void onRawDataReceived(const std::string &) override;

         void openConnection(const std::string &host, const std::string &port) override;

         void closeConnection() override;

         bool sendData(const std::string &data) override;

         void startHandshake() override
         {
            startBIP151Handshake();
         }

         // Is public only for tests
         void rekey();

      private:
         bool createCookie() override;
         bool startBIP151Handshake();

         void processIncomingData(const BinaryData &payload);
         bool processAEADHandshake(const bip15x::Message &);
         bool verifyNewIDKey(const BinaryDataRef &newKey, const std::string &srvId);
         void rekeyIfNeeded(size_t dataSize);
         bool sendPacket(const BinaryData &, bool encrypted = true);

      private:
         const BIP15xParams   params_;

         std::shared_ptr<FutureValue<bool>> serverPubkeyProm_;
         std::string host_, port_;
         std::unique_ptr<BIP151Connection> bip151Connection_;
         std::chrono::time_point<std::chrono::steady_clock> outKeyTimePoint_;
         uint32_t outerRekeyCount_ = 0;
         uint32_t innerRekeyCount_ = 0;
         bool bip150HandshakeCompleted_ = false;
         bool bip151HandshakeCompleted_ = false;

         BIP15xNewKeyCb cbNewKey_;
      };

   }  // namespace network
}  // namespace bs

#endif // __TRANSPORT_BIP15X_H__
