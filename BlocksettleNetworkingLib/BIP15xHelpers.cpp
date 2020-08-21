/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BIP15xHelpers.h"

#include "EncryptionUtils.h"
#include "AuthorizedPeers.h"
#include "FutureValue.h"

using namespace bs::network;

BIP15xPeer::BIP15xPeer(const std::string &name, const BinaryData &pubKey)
   : name_(name)
   , pubKey_(pubKey)
{
   if (!bip15x::isValidPubKey(pubKey)) {
      throw std::runtime_error("invalid public key");
   }
}

// static
BinaryData bip15x::convertKey(const btc_pubkey &pubKey)
{
   return BinaryData(pubKey.pubkey, pubKey.compressed
                     ? BTC_ECKEY_COMPRESSED_LENGTH : BTC_ECKEY_UNCOMPRESSED_LENGTH);
}

BinaryData bip15x::convertCompressedKey(const btc_pubkey &pubKey)
{
   if (!pubKey.compressed) {
      return {};
   }
   return convertKey(pubKey);
}

bool bip15x::isValidPubKey(const BinaryData &pubKey)
{
   // Based on CryptoECDSA::VerifyPublicKeyValid
   btc_pubkey key;
   btc_pubkey_init(&key);

   switch (pubKey.getSize()) {
      case BTC_ECKEY_COMPRESSED_LENGTH:
         key.compressed = true;
         break;
      case BTC_ECKEY_UNCOMPRESSED_LENGTH:
         key.compressed = false;
         break;
      default:
         return false;
   }

   std::memcpy(key.pubkey, pubKey.getPtr(), pubKey.getSize());
   return btc_pubkey_is_valid(&key);
}

bool bip15x::addAuthPeer(AuthorizedPeers *authPeers, const BIP15xPeer &peer)
{
   authPeers->eraseName(peer.name());
   authPeers->addPeer(peer.pubKey(), std::vector<std::string>{peer.name()});
   return true;
}

void bip15x::updatePeerKeys(AuthorizedPeers *authPeers_, const BIP15xPeers &newPeers)
{
   // Make a copy of peers map!
   const auto oldPeers = authPeers_->getPeerNameMap();
   for (const auto &oldPeer : oldPeers) {
      // Own key pair is also stored here, we should preserve it
      if (oldPeer.first != "own") {
         authPeers_->eraseName(oldPeer.first);
      }
   }

   for (const auto &newPeer : newPeers) {
      authPeers_->addPeer(newPeer.pubKey(), std::vector<std::string>({newPeer.name()}));
   }
}
