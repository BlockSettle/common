/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __TRANSPORT_BIP15X_SERVER_H__
#define __TRANSPORT_BIP15X_SERVER_H__

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <spdlog/spdlog.h>
#include "AuthorizedPeers.h"
#include "BIP15xHelpers.h"
#include "BIP15xMessage.h"
#include "TransportBIP15x.h"
#include "EncryptionUtils.h"
#include "Transport.h"

// DESIGN NOTES: Cookies are used for local connections. When the client is
// invoked by a binary containing a server connection, the binary must be
// invoked with the client connection's public BIP 150 ID key. In turn, the
// binary with the client connection must generate a cookie with its public BIP
// 150 ID key. The server will read the cookie and get the client key. This
// allows both sides to verify each other.
//
// When adding authorized keys to a connection, the name needs to be the
// client ID. This is because the ID is the only reliable information that's
// available and can be used to ID who's on the other side of a connection. It's
// okay to use other names in the GUI and elsewhere. However, the client ID must
// be used when searching for keys.

namespace bs {
   namespace network {
      // A struct containing the data required per-connection with clients.
      struct BIP15xPerConnData
      {
      public:
         void reset();

         std::unique_ptr<BIP151Connection> encData_;
         std::chrono::time_point<std::chrono::steady_clock> outKeyTimePoint_;
         ServerConnectionListener::Details details;
         bool     isValid{ true };
         std::string clientId;
      };

      struct BIP15xServerParams
      {
         using AddPeerFunc = std::function<void(const BIP15xPeer&)>;
         const unsigned listenPort_;
         const BinaryData publicKey_;
         const AddPeerFunc addPeer_;

         BIP15xServerParams(unsigned listenPort
            , const BinaryData& publicKey
            , const AddPeerFunc& addPeer) :
            listenPort_(listenPort), publicKey_(publicKey), addPeer_(addPeer)
         {}
      };

      // The class establishing BIP 150/151 handshakes before encrypting/decrypting
      // the on-the-wire data using BIP 150/151. Used by the server in a connection.
      class TransportBIP15xServer : public TransportBIP15x, public TransportServer
      {
      public:
         using TrustedClientsCallback = std::function<BIP15xPeers()>;

         TransportBIP15xServer(const std::shared_ptr<spdlog::logger> &
            , const TrustedClientsCallback& trustedClients
            , bool ephemeralPeers
            , BIP15xAuthMode authMode
            , const std::string& ownKeyFileDir = ""
            , const std::string& ownKeyFileName = ""
            , bool makeServerCookie = false
            , bool readClientCookie = false
            , const std::string& cookiePath = "");

         TransportBIP15xServer(const std::shared_ptr<spdlog::logger> &
            , const TrustedClientsCallback& cbTrustedClients
            , BIP15xAuthMode authMode
            , const std::string& ownKeyFileDir = ""
            , const std::string& ownKeyFileName = ""
            , bool makeServerCookie = false
            , bool readClientCookie = false
            , const std::string& cookiePath = "");

         ~TransportBIP15xServer() noexcept override;

         TransportBIP15xServer(const TransportBIP15xServer&) = delete;
         TransportBIP15xServer& operator= (const TransportBIP15xServer&) = delete;
         TransportBIP15xServer(TransportBIP15xServer&&) = delete;
         TransportBIP15xServer& operator= (TransportBIP15xServer&&) = delete;

         // Is public only for tests
         void rekey(const std::string &clientId);

         // If set only selected trusted clients will be able connect to the server.
         // This will work even if startupBIP150CTX was called with publicRequester set to true.
         // This must be called before starting accepting connections.
         // Only compressed public keys are supported.
         // If empty (default) trusted clients are not enforced.
         void forceTrustedClients(const BIP15xPeers &);

         // Could be called only from IO thread callbacks - used only in tests atm.
         // Returns null if clientId is not known or was not yet authenticated.
         std::unique_ptr<BIP15xPeer> getClientKey(const std::string &clientId) const;

         bool handshakeComplete(const std::string &clientId) override;
         BIP15xServerParams getParams(unsigned) const;

      private:
         bool createCookie(void);
         bool rmCookieFile(void);
         bool usesCookie(void) const override;
         bool areAuthKeysEphemeral(void) const override;

         void startHandshake(const std::string& clientID);

         void processIncomingData(const std::string &encData
            , const std::string &clientID) override;
         bool processAEADHandshake(const bip15x::Message &
            , const std::string &clientID);

         std::shared_ptr<BIP15xPerConnData> GetConnection(const std::string& clientId);

         bool sendData(const std::string &clientId, const std::string &) override;

         void closeClient(const std::string &clientId) override;
         void addClient(const std::string &clientId, const ServerConnectionListener::Details &details) override;

         void reportFatalError(const std::shared_ptr<BIP15xPerConnData> &conn);

      private:
         std::map<std::string, std::shared_ptr<BIP15xPerConnData>>   socketConnMap_;
         
         const bool ephemeralPeers_;
         const BIP15xAuthMode authMode_;
         TrustedClientsCallback cbTrustedClients_;
         const bool useClientIDCookie_;
         const bool makeServerIDCookie_;

         BIP15xPeers forcedTrustedClients_;

         //Need to be kept opened for the whole object lifetime         
         std::unique_ptr<std::ofstream>   cookieFile_;   
         const std::string cookiePath_;
      };

   }  // namespace network
}  // namespace bs

#endif // __TRANSPORT_BIP15X_SERVER_H__
