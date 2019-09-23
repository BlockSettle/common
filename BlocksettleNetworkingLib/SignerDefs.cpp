#include "SignerDefs.h"

NetworkType bs::sync::mapFrom(headless::NetworkType netType)
{
   switch (netType) {
   case headless::MainNetType:   return NetworkType::MainNet;
   case headless::TestNetType:   return NetworkType::TestNet;
   default:    return NetworkType::Invalid;
   }
}

bs::sync::WalletFormat bs::sync::mapFrom(headless::WalletFormat format)
{
   switch (format) {
   case headless::WalletFormatHD:         return bs::sync::WalletFormat::HD;
   case headless::WalletFormatPlain:      return bs::sync::WalletFormat::Plain;
   case headless::WalletFormatSettlement: return bs::sync::WalletFormat::Settlement;
   case headless::WalletFormatUnknown:
   default:    return bs::sync::WalletFormat::Unknown;
   }
}

bs::sync::WalletData bs::sync::WalletData::fromPbMessage(const headless::SyncWalletResponse &response)
{
   bs::sync::WalletData result;

   result.highestExtIndex = response.highest_ext_index();
   result.highestIntIndex = response.highest_int_index();

   for (int i = 0; i < response.addresses_size(); ++i) {
      const auto addrInfo = response.addresses(i);
      const bs::Address addr(addrInfo.address());
      if (addr.isNull()) {
         continue;
      }
      result.addresses.push_back({ addrInfo.index(), std::move(addr)
         , addrInfo.comment() });
   }
   for (int i = 0; i < response.addrpool_size(); ++i) {
      const auto addrInfo = response.addrpool(i);
      const bs::Address addr(addrInfo.address());
      if (addr.isNull()) {
         continue;
      }
      result.addrPool.push_back({ addrInfo.index(), std::move(addr), "" });
   }
   for (int i = 0; i < response.txcomments_size(); ++i) {
      const auto txInfo = response.txcomments(i);
      result.txComments.push_back({ txInfo.txhash(), txInfo.comment() });
   }

   return result;
}

std::vector<bs::sync::WalletInfo> bs::sync::WalletInfo::fromPbMessage(const headless::SyncWalletInfoResponse &response)
{
   std::vector<bs::sync::WalletInfo> result;
   for (int i = 0; i < response.wallets_size(); ++i) {
      const auto walletInfoPb = response.wallets(i);
      bs::sync::WalletInfo walletInfo;

      walletInfo.format = mapFrom(walletInfoPb.format());
      walletInfo.id = walletInfoPb.id();
      walletInfo.name = walletInfoPb.name();
      walletInfo.description = walletInfoPb.description();
      walletInfo.netType = mapFrom(walletInfoPb.nettype());
      walletInfo.watchOnly = walletInfoPb.watching_only();

      for (int i = 0; i < walletInfoPb.encryptiontypes_size(); ++i) {
         const auto encType = walletInfoPb.encryptiontypes(i);
         walletInfo.encryptionTypes.push_back(bs::sync::mapFrom(encType));
      }
      for (int i = 0; i < walletInfoPb.encryptionkeys_size(); ++i) {
         const auto encKey = walletInfoPb.encryptionkeys(i);
         walletInfo.encryptionKeys.push_back(encKey);
      }
      walletInfo.encryptionRank = { walletInfoPb.keyrank().m(), walletInfoPb.keyrank().n() };

      result.push_back(walletInfo);
   }
   return result;
}

bs::wallet::EncryptionType bs::sync::mapFrom(headless::EncryptionType encType)
{
   switch (encType) {
   case headless::EncryptionTypePassword: return bs::wallet::EncryptionType::Password;
   case headless::EncryptionTypeAutheID:  return bs::wallet::EncryptionType::Auth;
   case headless::EncryptionTypeUnencrypted:
   default:    return bs::wallet::EncryptionType::Unencrypted;
   }
}
