#include "disable_warnings.h"
#include <spdlog/spdlog.h>
#include <btc/ecc.h>
#include "enable_warnings.h"
#include <ctime>
#include "BIP150_151.h"
#include "CoreWalletsManager.h"
#include "CoreHDWallet.h"
#include "CorePlainWallet.h"
#include "SystemFileUtils.h"

using namespace bs::core;

WalletsManager::WalletsManager(const std::shared_ptr<spdlog::logger> &logger, unsigned int nbBackups)
   : logger_(logger), nbBackupFilesToKeep_(nbBackups)
{}

WalletsManager::~WalletsManager() noexcept
{
   // These really ought to be called elsewhere, when the appropriate binaries
   // shut down.
   shutdownBIP151CTX();
   btc_ecc_stop();
}

void WalletsManager::reset()
{
   walletsLoaded_ = false;
   wallets_.clear();
   hdWallets_.clear();
   walletNames_.clear();
   walletsId_.clear();
   hdWalletsId_.clear();
   settlementWallet_.reset();
}

void WalletsManager::loadWallets(NetworkType netType, const std::string &walletsPath
   , const CbProgress &cbProgress)
{
   if (walletsPath.empty()) {
      return;
   }
   if (!SystemFileUtils::pathExist(walletsPath)) {
      logger_->debug("Creating wallets path {}", walletsPath);
      SystemFileUtils::mkPath(walletsPath);
   }

   const auto fileList = SystemFileUtils::readDir(walletsPath, "*.lmdb");
   const size_t totalCount = fileList.size();
   size_t current = 0;

   for (const auto &file : fileList) {
      if (file.find(SettlementWallet::fileNamePrefix()) == 0) {
         if (settlementWallet_) {
            logger_->warn("Can't load more than 1 settlement wallet from {}", file);
            continue;
         }
         logger_->debug("Loading settlement wallet from {} ({})", file, (int)netType);
         try {
            settlementWallet_ = std::make_shared<SettlementWallet>(netType
               , walletsPath + "/" + file);

            current++;
            if (cbProgress) {
               cbProgress(current, totalCount);
            }
         }
         catch (const std::exception &e) {
            logger_->error("Failed to load settlement wallet: {}", e.what());
         }
      }
      if (!isWalletFile(file)) {
         continue;
      }
      try {
         logger_->debug("Loading BIP44 wallet from {}", file);
         const auto wallet = std::make_shared<hd::Wallet>(walletsPath + "/" + file
                                                               , logger_);
         current++;
         if (cbProgress) {
            cbProgress(current, totalCount);
         }
         if ((netType != NetworkType::Invalid) && (netType != wallet->networkType())) {
            logger_->warn("[{}] Network type mismatch: loading {}, wallet has {}", __func__, (int)netType, (int)wallet->networkType());
         }

         saveWallet(wallet);
      }
      catch (const std::exception &e) {
         logger_->warn("Failed to load BIP44 wallet: {}", e.what());
      }
   }
   walletsLoaded_ = true;
}

WalletsManager::HDWalletPtr WalletsManager::loadWoWallet(const std::string &walletsPath
   , const std::string &fileName)
{
   if (walletsPath.empty()) {
      return nullptr;
   }
   if (!SystemFileUtils::pathExist(walletsPath)) {
      logger_->debug("Creating wallets path {}", walletsPath);
      SystemFileUtils::mkPath(walletsPath);
   }

   try {
      logger_->debug("Loading BIP44 WO-wallet from {}", fileName);
      const auto wallet = std::make_shared<hd::Wallet>(walletsPath + "/" + fileName
         , logger_);
      if (!wallet->isWatchingOnly()) {
         logger_->error("Wallet {} is not watching-only", fileName);
         return nullptr;
      }

      saveWallet(wallet);
      return wallet;
   } catch (const std::exception &e) {
      logger_->warn("Failed to load WO-wallet: {}", e.what());
   }
   return nullptr;
}

void WalletsManager::backupWallet(const HDWalletPtr &wallet, const std::string &targetDir) const
{
   if (wallet->isWatchingOnly()) {
      logger_->info("No need to backup watching-only wallet {}", wallet->name());
      return;
   }
   if (!SystemFileUtils::pathExist(targetDir)) {
      if (!SystemFileUtils::mkPath(targetDir)) {
         logger_->error("Failed to create backup directory {}", targetDir);
         return;
      }
   }
   const auto &lockFiles = SystemFileUtils::readDir(targetDir, "*.lmdb-lock");
   for (const auto &file : lockFiles) {
      SystemFileUtils::rmFile(targetDir + "/" + file);
   }
   const auto files = SystemFileUtils::readDir(targetDir
      , hd::Wallet::fileNamePrefix(false) + wallet->walletId() + "_*.lmdb");
   if (!files.empty() && (files.size() >= (int)nbBackupFilesToKeep_)) {
      for (int i = 0; i <= files.size() - (int)nbBackupFilesToKeep_; i++) {
         logger_->debug("Removing old backup file {}", files[i]);
         SystemFileUtils::rmFile(targetDir + "/" + files[i]);
      }
   }
   const auto tm = std::time(nullptr);
   char tmStr[32];
   std::strftime(tmStr, sizeof(tmStr), "%Y%j%H%M%S", std::localtime(&tm));
   const auto backupFile = targetDir + "/" + hd::Wallet::fileNamePrefix(false)
      + wallet->walletId()  + "_" + tmStr + ".lmdb";
   wallet->copyToFile(backupFile);
}

bool WalletsManager::isWalletFile(const std::string &fileName) const
{
   if ((fileName.find(SettlementWallet::fileNamePrefix()) == 0)
      || (fileName.find(PlainWallet::fileNamePrefix(false)) == 0)) {
      return false;
   }
   return true;
}

WalletsManager::WalletPtr WalletsManager::createSettlementWallet(NetworkType netType, const std::string &walletsPath)
{
   logger_->debug("Creating settlement wallet");
   try {
      settlementWallet_ = std::make_shared<SettlementWallet>(netType, logger_);
      if (!walletsPath.empty()) {
         settlementWallet_->saveToDir(walletsPath);
      }
   }
   catch (const std::exception &e) {
      logger_->error("Failed to create Settlement wallet: {}", e.what());
   }
   return settlementWallet_;
}

WalletsManager::WalletPtr WalletsManager::getAuthWallet() const
{
   const auto priWallet = getPrimaryWallet();
   if (!priWallet) {
      return nullptr;
   }
   const auto group = priWallet->getGroup(bs::hd::CoinType::BlockSettle_Auth);
   if (!group) {
      return nullptr;
   }
   return group->getLeaf(0u);
}

WalletsManager::HDWalletPtr WalletsManager::getPrimaryWallet() const
{
   for (const auto &hdWallet : hdWallets_) {
      if (hdWallet.second->isPrimary()) {
         return hdWallet.second;
      }
   }
   return nullptr;
}

void WalletsManager::saveWallet(const WalletPtr &newWallet, NetworkType netType)
{
   addWallet(newWallet);
}

void WalletsManager::addWallet(const WalletPtr &wallet)
{
   walletsId_.emplace_back(wallet->walletId());
   wallets_.emplace(wallet->walletId(), wallet);
}

void WalletsManager::saveWallet(const HDWalletPtr &wallet)
{
   if (!chainCode_.isNull()) {
      wallet->setChainCode(chainCode_);
   }
   hdWalletsId_.emplace_back(wallet->walletId());
   hdWallets_[wallet->walletId()] = wallet;
   walletNames_.insert(wallet->name());
   for (const auto &leaf : wallet->getLeaves()) {
      addWallet(leaf);
   }
}

void WalletsManager::setChainCode(const BinaryData &chainCode)
{
   chainCode_ = chainCode;
   for (const auto &hdWallet : hdWallets_) {
      hdWallet.second->setChainCode(chainCode);
   }
}

const WalletsManager::HDWalletPtr WalletsManager::getHDWallet(const unsigned int index) const
{
   if (index >= hdWalletsId_.size()) {
      return nullptr;
   }
   return getHDWalletById(hdWalletsId_[index]);
}

const WalletsManager::HDWalletPtr WalletsManager::getHDWalletById(const std::string& walletId) const
{
   auto it = hdWallets_.find(walletId);
   if (it != hdWallets_.end()) {
      return it->second;
   }
   return nullptr;
}

const WalletsManager::HDWalletPtr WalletsManager::getHDRootForLeaf(const std::string& walletId) const
{
   for (const auto &hdWallet : hdWallets_) {
      if (hdWallet.second->getLeaf(walletId)) {
         return hdWallet.second;
      }
   }
   return nullptr;
}

WalletsManager::WalletPtr WalletsManager::getWalletById(const std::string& walletId) const
{
   if (!wallets_.empty()) {
      const auto &walletIt = wallets_.find(walletId);
      if (walletIt != wallets_.end()) {
         return walletIt->second;
      }
   }
   if (settlementWallet_ && (settlementWallet_->walletId() == walletId)) {
      return settlementWallet_;
   }
   return nullptr;
}

WalletsManager::WalletPtr WalletsManager::getWalletByAddress(const bs::Address &addr) const
{
   const auto &address = addr.unprefixed();
   {
      for (const auto wallet : wallets_) {
         if (wallet.second && (wallet.second->containsAddress(address)
            || wallet.second->containsHiddenAddress(address))) {
            return wallet.second;
         }
      }
   }
   if ((settlementWallet_ != nullptr) && settlementWallet_->containsAddress(address)) {
      return settlementWallet_;
   }
   return nullptr;
}

void WalletsManager::eraseWallet(const WalletPtr &wallet)
{
   if (!wallet) {
      return;
   }
   const auto itId = std::find(walletsId_.begin(), walletsId_.end(), wallet->walletId());
   if (itId != walletsId_.end()) {
      walletsId_.erase(itId);
   }
   wallets_.erase(wallet->walletId());
}

bool WalletsManager::deleteWalletFile(const WalletPtr &wallet)
{
   bool isHDLeaf = false;
   logger_->info("Removing wallet {} ({})...", wallet->name(), wallet->walletId());
   for (auto hdWallet : hdWallets_) {
      const auto leaves = hdWallet.second->getLeaves();
      if (std::find(leaves.begin(), leaves.end(), wallet) != leaves.end()) {
         for (auto group : hdWallet.second->getGroups()) {
            if (group->deleteLeaf(wallet)) {
               isHDLeaf = true;
               eraseWallet(wallet);
               break;
            }
         }
      }
      if (isHDLeaf) {
         break;
      }
   }

   if (!isHDLeaf) {
      if (!wallet->eraseFile()) {
         logger_->error("Failed to remove wallet file for {}", wallet->name());
         return false;
      }
      if (wallet == settlementWallet_) {
         settlementWallet_ = nullptr;
      }
      eraseWallet(wallet);
   }

   return true;
}

bool WalletsManager::deleteWalletFile(const HDWalletPtr &wallet)
{
   const auto it = hdWallets_.find(wallet->walletId());
   if (it == hdWallets_.end()) {
      logger_->warn("Unknown HD wallet {} ({})", wallet->name(), wallet->walletId());
      return false;
   }

   for (const auto &leaf : wallet->getLeaves()) {
      eraseWallet(leaf);
   }

   const auto itId = std::find(hdWalletsId_.begin(), hdWalletsId_.end(), wallet->walletId());
   if (itId != hdWalletsId_.end()) {
      hdWalletsId_.erase(itId);
   }
   hdWallets_.erase(wallet->walletId());
   walletNames_.erase(wallet->name());
   const bool result = wallet->eraseFile();
   logger_->info("Wallet {} ({}) removed: {}", wallet->name(), wallet->walletId(), result);

   return result;
}

WalletsManager::HDWalletPtr WalletsManager::createWallet(const std::string& name, const std::string& description
   , wallet::Seed seed, const std::string &walletsPath, bool primary
   , const std::vector<bs::wallet::PasswordData> &pwdData, bs::wallet::KeyRank keyRank)
{
   const HDWalletPtr newWallet = std::make_shared<hd::Wallet>(name, description
                                                           , seed, logger_);

   if (hdWallets_.find(newWallet->walletId()) != hdWallets_.end()) {
      throw std::runtime_error("HD wallet with id " + newWallet->walletId() + " already exists");
   }

   newWallet->createStructure();
   if (primary) {
      newWallet->createGroup(bs::hd::CoinType::BlockSettle_Auth);
   }
   if (!pwdData.empty()) {
      newWallet->changePassword(pwdData, keyRank, SecureBinaryData(), false, false, false);
   }
   addWallet(newWallet, walletsPath);
   return newWallet;
}

void WalletsManager::addWallet(const HDWalletPtr &wallet, const std::string &walletsPath)
{
   if (!wallet) {
      return;
   }
   if (!walletsPath.empty()) {
      wallet->saveToDir(walletsPath);
   }
   saveWallet(wallet);
}
