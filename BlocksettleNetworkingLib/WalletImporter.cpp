#include "WalletImporter.h"
#include "AssetManager.h"
#include "AuthAddressManager.h"
#include "HDWallet.h"
#include "SignContainer.h"
#include "WalletsManager.h"


WalletImporter::WalletImporter(const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<WalletsManager> &walletsMgr, const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<AssetManager> &assetMgr, const std::shared_ptr<AuthAddressManager> &authMgr
   , const QString &walletsPath
   , const bs::hd::Wallet::cb_scan_read_last &cbr, const bs::hd::Wallet::cb_scan_write_last &cbw)
   : QObject(nullptr), signingContainer_(container), walletsMgr_(walletsMgr), armory_(armory)
   , assetMgr_(assetMgr), authMgr_(authMgr), walletsPath_(walletsPath)
   , cbReadLast_(cbr), cbWriteLast_(cbw)
{
   connect(signingContainer_.get(), &SignContainer::HDWalletCreated, this, &WalletImporter::onHDWalletCreated);
   connect(signingContainer_.get(), &SignContainer::HDLeafCreated, this, &WalletImporter::onHDLeafCreated);
   connect(signingContainer_.get(), &SignContainer::Error, this, &WalletImporter::onHDWalletError);

   connect(walletsMgr_.get(), &WalletsManager::walletImportFinished, this, &WalletImporter::onImportComplete);
}

void WalletImporter::onImportComplete(const std::string &id)
{
   if (rootWallet_ && (rootWallet_->getWalletId() == id)) {
      signingContainer_->SyncAddresses(walletsMgr_->GetAddressesInAllWallets());
   }
}

void WalletImporter::onWalletScanComplete(bs::hd::Group *grp, bs::hd::Path::Elem wallet, bool isValid)
{
   if (!rootWallet_) {
      return;
   }
   if (isValid) {
      if (grp && (grp->getIndex() == rootWallet_->getXBTGroupType())) {
         const bs::hd::Path::Elem nextWallet = (wallet == UINT32_MAX) ? 0 : wallet + 1;
         bs::hd::Path path;
         path.append(bs::hd::purpose, true);
         path.append(grp->getIndex(), true);
         path.append(nextWallet, true);
         const auto createNextWalletReq = signingContainer_->CreateHDLeaf(rootWallet_, path, pwdData_);
         if (createNextWalletReq) {
            createNextWalletReqs_[createNextWalletReq] = path;
         }
      }
   }
   else {
      if (!((grp->getIndex() == rootWallet_->getXBTGroupType()) && (wallet == 0))) {
         signingContainer_->DeleteHD(grp->getLeaf(wallet));
         grp->deleteLeaf(wallet);
      }
   }
}

void WalletImporter::onHDWalletCreated(unsigned int id, std::shared_ptr<bs::hd::Wallet> newWallet)
{
   if (!createWalletReq_ || (createWalletReq_ != id)) {
      return;
   }
   createWalletReq_ = 0;

   const auto &ccList = assetMgr_->privateShares();
   rootWallet_ = newWallet;
   rootWallet_->SetArmory(armory_);
   walletsMgr_->AdoptNewWallet(newWallet, walletsPath_);

   pwdData_.resize(keyRank_.first);

   if (rootWallet_->isPrimary()) {
      authMgr_->CreateAuthWallet(pwdData_, false);
   }
   if (!rootWallet_->isPrimary() || ccList.empty()) {
      if (armory_->state() == ArmoryConnection::State::Ready) {
         rootWallet_->startRescan([this](bs::hd::Group *grp, bs::hd::Path::Elem wallet, bool isValid) {
            onWalletScanComplete(grp, wallet, isValid);
         }, cbReadLast_, cbWriteLast_);
      }
      emit walletCreated(rootWallet_->getWalletId());
   }
   else {
      for (const auto &cc : ccList) {
         bs::hd::Path path;
         path.append(bs::hd::purpose, true);
         path.append(bs::hd::CoinType::BlockSettle_CC, true);
         path.append(cc, true);
         const auto reqId = signingContainer_->CreateHDLeaf(rootWallet_, path, pwdData_);
         if (reqId) {
            createCCWalletReqs_[reqId] = cc;
         }
      }
   }
}

void WalletImporter::onHDLeafCreated(unsigned int id, BinaryData pubKey, BinaryData chainCode, std::string walletId)
{
   if (!createCCWalletReqs_.empty() && (createCCWalletReqs_.find(id) != createCCWalletReqs_.end())) {
      const auto cc = createCCWalletReqs_[id];
      createCCWalletReqs_.erase(id);

      const auto leafNode = std::make_shared<bs::hd::Node>(pubKey, chainCode, rootWallet_->networkType());
      const auto group = rootWallet_->createGroup(bs::hd::CoinType::BlockSettle_CC);
      const auto &leaf = group->createLeaf(bs::hd::Path::keyToElem(cc), leafNode);
      leaf->setData(assetMgr_->getCCGenesisAddr(cc).display<std::string>());
      leaf->setData(assetMgr_->getCCLotSize(cc));

      if (createCCWalletReqs_.empty()) {
         if (armory_->state() == ArmoryConnection::State::Ready) {
            rootWallet_->startRescan([this](bs::hd::Group *grp, bs::hd::Path::Elem wallet, bool isValid) {
               onWalletScanComplete(grp, wallet, isValid);
            }, cbReadLast_, cbWriteLast_);
         }
         emit walletCreated(rootWallet_->getWalletId());
      }
   }
   else if (!createNextWalletReqs_.empty() && (createNextWalletReqs_.find(id) != createNextWalletReqs_.end())) {
      const auto path = createNextWalletReqs_[id];
      createNextWalletReqs_.erase(id);

      const auto leafNode = std::make_shared<bs::hd::Node>(pubKey, chainCode, rootWallet_->networkType());
      const auto group = rootWallet_->getGroup(static_cast<bs::hd::CoinType>(path.get(-2)));
      const auto leaf = group->createLeaf(path.get(-1), leafNode);
      if (!leaf) {
         return;
      }
      if (armory_->state() == ArmoryConnection::State::Ready) {
         leaf->setScanCompleteCb([this, group](bs::hd::Path::Elem wlt, bool status) {
            onWalletScanComplete(group.get(), wlt, status);
         });
         leaf->scanAddresses();
      }
   }
}

void WalletImporter::onHDWalletError(unsigned int id, std::string errMsg)
{
   if (!createWalletReq_ || (createWalletReq_ != id)) {
      return;
   }
   createWalletReq_ = 0;
   emit error(QString::fromStdString(errMsg));
}

void WalletImporter::Import(const std::string &name, const std::string &description
   , bs::wallet::Seed seed, bool primary, const std::vector<bs::wallet::PasswordData> &pwdData
   , bs::wallet::KeyRank keyRank)
{
   if (!signingContainer_ || signingContainer_->isOffline()) {
      emit error(tr("Can't start import with missing or offline signer"));
      return;
   }
   pwdData_ = pwdData;
   keyRank_ = keyRank;
   createWalletReq_ = signingContainer_->CreateHDWallet(name, description, primary, seed, pwdData, keyRank);
   if (!createWalletReq_) {
      emit error(tr("Failed to create HD wallet"));
   }
}
