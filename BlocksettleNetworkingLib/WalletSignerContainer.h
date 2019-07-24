#ifndef __WALLET_SIGNER_CONTAINER_H__
#define __WALLET_SIGNER_CONTAINER_H__

#include "SignContainer.h"

// WalletSignerContainer -  meant to be the interface used by Wallets Manager
// all leafs/wallets managin stuff
// All other signer users will stick to SignContainer interface for signing operations
class WalletSignerContainer : public SignContainer
{
Q_OBJECT

public:
   WalletSignerContainer(const std::shared_ptr<spdlog::logger> &, OpMode opMode);
   ~WalletSignerContainer() noexcept = default;

public:
   virtual void syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &) = 0;
   virtual void syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &) = 0;
   virtual void syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &) = 0;

signals:
   void AuthLeafAdded(const std::string &walletId);
   // Notified from remote/local signer when wallets list is updated
   void walletsListUpdated();
};

#endif // __WALLET_SIGNER_CONTAINER_H__
