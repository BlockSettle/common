#ifndef HEADLESSDEFS_H
#define HEADLESSDEFS_H

#include "BtcDefinitions.h"
#include "WalletEncryption.h"

#include "headless.pb.h"

using namespace Blocksettle::Communication;

namespace bs {
   namespace core {
      class WalletsManager;
      namespace hd {
         class Leaf;
      }
   }
   namespace sync {
      namespace hd {
         headless::EncryptionType mapFrom(bs::wallet::EncryptionType encType);
         headless::NetworkType mapFrom(NetworkType netType);

         headless::SyncWalletInfoResponse exportHDWalletsInfoToPbMessage(const std::shared_ptr<bs::core::WalletsManager> &walletsMgr);
         headless::SyncWalletResponse     exportHDLeafToPbMessage(const std::shared_ptr<bs::core::hd::Leaf> &leaf);
      }
   }
}

#endif // HEADLESSDEFS_H
