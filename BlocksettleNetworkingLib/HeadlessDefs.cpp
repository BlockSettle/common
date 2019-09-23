#include "HeadlessDefs.h"
#include "CoreWalletsManager.h"
#include "CoreHDWallet.h"


headless::SyncWalletInfoResponse bs::sync::hd::exportHDWalletsInfoToPbMessage(const std::shared_ptr<bs::core::WalletsManager> &walletsMgr)
{
   headless::SyncWalletInfoResponse response;
   assert(walletsMgr);

   if (!walletsMgr) {
      return response;
   }

   for (size_t i = 0; i < walletsMgr->getHDWalletsCount(); ++i) {
      auto wallet = response.add_wallets();
      const auto hdWallet = walletsMgr->getHDWallet(i);
      wallet->set_format(headless::WalletFormatHD);
      wallet->set_id(hdWallet->walletId());
      wallet->set_name(hdWallet->name());
      wallet->set_description(hdWallet->description());
      wallet->set_nettype(mapFrom(hdWallet->networkType()));
      wallet->set_watching_only(hdWallet->isWatchingOnly());

      for (const auto &encType : hdWallet->encryptionTypes()) {
         wallet->add_encryptiontypes(bs::sync::hd::mapFrom(encType));
      }
      for (const auto &encKey : hdWallet->encryptionKeys()) {
         wallet->add_encryptionkeys(encKey.toBinStr());
      }
      auto keyrank = wallet->mutable_keyrank();
      keyrank->set_m(hdWallet->encryptionRank().m);
      keyrank->set_n(hdWallet->encryptionRank().n);
   }
   return response;
}

headless::EncryptionType bs::sync::hd::mapFrom(bs::wallet::EncryptionType encType)
{
   switch (encType) {
   case bs::wallet::EncryptionType::Password:   return headless::EncryptionTypePassword;
   case bs::wallet::EncryptionType::Auth:       return headless::EncryptionTypeAutheID;
   case bs::wallet::EncryptionType::Unencrypted:
   default:       return headless::EncryptionTypeUnencrypted;
   }
}

headless::NetworkType bs::sync::hd::mapFrom(NetworkType netType)
{
   switch (netType) {
   case NetworkType::MainNet: return headless::MainNetType;
   case NetworkType::TestNet:
   default:    return headless::TestNetType;
   }
}

headless::SyncWalletResponse bs::sync::hd::exportHDLeafToPbMessage(const std::shared_ptr<bs::core::hd::Leaf> &leaf)
{
   headless::SyncWalletResponse response;
   response.set_walletid(leaf->walletId());

   response.set_highest_ext_index(leaf->getExtAddressCount());
   response.set_highest_int_index(leaf->getIntAddressCount());

   for (const auto &addr : leaf->getUsedAddressList()) {
      const auto comment = leaf->getAddressComment(addr);
      const auto index = leaf->getAddressIndex(addr);
      auto addrData = response.add_addresses();
      addrData->set_address(addr.display());
      addrData->set_index(index);
      if (!comment.empty()) {
         addrData->set_comment(comment);
      }
   }
   const auto &pooledAddresses = leaf->getPooledAddressList();
   for (const auto &addr : pooledAddresses) {
      const auto index = leaf->getAddressIndex(addr);
      auto addrData = response.add_addrpool();
      addrData->set_address(addr.display());
      addrData->set_index(index);
   }
   for (const auto &txComment : leaf->getAllTxComments()) {
      auto txCommData = response.add_txcomments();
      txCommData->set_txhash(txComment.first.toBinStr());
      txCommData->set_comment(txComment.second);
   }
   return response;
}
