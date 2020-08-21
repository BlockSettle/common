/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BIP15X_HELPERS_H
#define BIP15X_HELPERS_H

#include <functional>
#include <future>
#include <string>
#include "BinaryData.h"

class AuthorizedPeers;
struct btc_pubkey_;
template<typename T> class FutureValue;

namespace bs {
   namespace network {

      // Immutable BIP15X peer public key, guaranteed to be valid (no need to check pubKey over and over)
      class BIP15xPeer
      {
      public:
         // Will throw if pubKey is invalid
         BIP15xPeer(const std::string &name, const BinaryData &pubKey);

         const std::string &name() const { return name_; }
         const BinaryData &pubKey() const { return pubKey_; }
      private:
         // key name
         std::string name_;

         // EC public key (33 bytes if compressed)
         BinaryData pubKey_;
      };

      using BIP15xPeers = std::vector<BIP15xPeer>;

      namespace bip15x {
         using PubKey = btc_pubkey_;

         BinaryData convertKey(const PubKey &pubKey);

         // Convert to BinaryData, return empty result if key is not compressed
         BinaryData convertCompressedKey(const PubKey &pubKey);
         bool isValidPubKey(const BinaryData &pubKey);
         bool addAuthPeer(AuthorizedPeers *authPeers, const BIP15xPeer &);
         void updatePeerKeys(AuthorizedPeers *authPeers, const BIP15xPeers &);
      }  // namespace bip15x

      using BIP15xNewKeyCb = std::function<void(const std::string &oldKey, const std::string &newKey
         , const std::string& srvAddrPort, const std::shared_ptr<FutureValue<bool>> &prompt)>;
   }  //namespace network
}  // namespace bs

#endif   // BIP15X_HELPERS_H
