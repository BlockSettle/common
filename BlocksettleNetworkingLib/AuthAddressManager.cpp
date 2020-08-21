/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthAddressManager.h"
#include <spdlog/spdlog.h>
#include <memory>
#include <QtConcurrent/QtConcurrentRun>
#include "AddressVerificator.h"
#include "ApplicationSettings.h"
#include "ArmoryConnection.h"
#include "BsClient.h"
#include "CelerClient.h"
#include "CheckRecipSigner.h"
#include "ClientClasses.h"
#include "FastLock.h"
#include "RequestReplyCommand.h"
#include "SignContainer.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"

using namespace Blocksettle::Communication;


AuthAddressManager::AuthAddressManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ArmoryConnection> &armory)
   : QObject(nullptr), logger_(logger), armory_(armory)
{}

void AuthAddressManager::init(const std::shared_ptr<ApplicationSettings>& appSettings
   , const std::shared_ptr<bs::sync::WalletsManager> &walletsManager
   , const std::shared_ptr<SignContainer> &container)
{
   settings_ = appSettings;
   walletsManager_ = walletsManager;
   signingContainer_ = container;

   connect(walletsManager_.get(), &bs::sync::WalletsManager::blockchainEvent, this, &AuthAddressManager::tryVerifyWalletAddresses);
   connect(walletsManager_.get(), &bs::sync::WalletsManager::authWalletChanged, this, &AuthAddressManager::onAuthWalletChanged);
   connect(walletsManager_.get(), &bs::sync::WalletsManager::walletChanged, this, &AuthAddressManager::onWalletChanged);
   connect(walletsManager_.get(), &bs::sync::WalletsManager::AuthLeafCreated, this, &AuthAddressManager::onWalletCreated);

   // signingContainer_ might be null if user rejects remote signer key
   if (signingContainer_) {
      connect(signingContainer_.get(), &SignContainer::TXSigned, this, &AuthAddressManager::onTXSigned);
   }

   SetAuthWallet();

   ArmoryCallbackTarget::init(armory_.get());
}

void AuthAddressManager::initLogin(const std::shared_ptr<BaseCelerClient> &celerClient
   , const std::shared_ptr<bs::TradeSettings> &tradeSettings)
{
   celerClient_ = celerClient;
   tradeSettings_ = tradeSettings;
}

const std::shared_ptr<bs::TradeSettings>& AuthAddressManager::tradeSettings() const
{
   return tradeSettings_;
}

void AuthAddressManager::SetAuthWallet()
{
   authWallet_ = walletsManager_->getAuthWallet();
}

bool AuthAddressManager::setup()
{
   if (!HaveAuthWallet()) {
      logger_->debug("[AuthAddressManager::setup] Auth wallet missing");
      addressVerificator_.reset();
      return false;
   }
   if (addressVerificator_) {
      return true;
   }

   if (readyError() != ReadyError::NoError) {
      return false;
   }

   addressVerificator_ = std::make_shared<AddressVerificator>(logger_, armory_
      , [this](const bs::Address &address, AddressVerificationState state)
   {
      if (!addressVerificator_) {
         logger_->error("[AuthAddressManager::setup] Failed to create AddressVerificator object");
         return;
      }

      logger_->info("Address verification on chain {} for {}", to_string(state), address.display());
      SetValidationState(address, state);
   });

   SetBSAddressList(bsAddressList_);
   return true;
}

void AuthAddressManager::onAuthWalletChanged()
{
   SetAuthWallet();
   addresses_.clear();
   tryVerifyWalletAddresses();
   emit AuthWalletChanged();
}

AuthAddressManager::~AuthAddressManager() noexcept
{
   addressVerificator_.reset();
   ArmoryCallbackTarget::cleanup();
}

size_t AuthAddressManager::GetAddressCount()
{
   FastLock locker(lockList_);
   return addresses_.size();
}

bs::Address AuthAddressManager::GetAddress(size_t index)
{
   FastLock locker(lockList_);
   if (index >= addresses_.size()) {
      return {};
   }
   return addresses_[index];
}

bool AuthAddressManager::WalletAddressesLoaded()
{
   FastLock locker(lockList_);
   return !addresses_.empty();
}

AuthAddressManager::ReadyError AuthAddressManager::readyError() const
{
   if (!HasAuthAddr()) {
      return ReadyError::MissingAuthAddr;
   }
   if (!HaveBSAddressList()) {
      return ReadyError::MissingAddressList;
   }
   if (!armory_) {
      return ReadyError::MissingArmoryPtr;
   }
   if (!armory_->isOnline()) {
      return ReadyError::ArmoryOffline;
   }

   return ReadyError::NoError;
}

bool AuthAddressManager::HaveAuthWallet() const
{
   return (authWallet_ != nullptr);
}

bool AuthAddressManager::HasAuthAddr() const
{
   return (HaveAuthWallet() && (authWallet_->getUsedAddressCount() > 0));
}

bool AuthAddressManager::CreateNewAuthAddress()
{
   const auto &cbAddr = [this](const bs::Address &) {
      emit walletsManager_->walletChanged(authWallet_->walletId());
   };
   authWallet_->getNewExtAddress(cbAddr);
   return true;
}

void AuthAddressManager::onTXSigned(unsigned int id, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason)
{
   const auto &itRevoke = signIdsRevoke_.find(id);
   if (itRevoke == signIdsRevoke_.end()) {
      return;
   }
   signIdsRevoke_.erase(id);

   if (result == bs::error::ErrorCode::NoError) {
      if (BroadcastTransaction(signedTX)) {
         emit AuthRevokeTxSent();
      }
      else {
         emit Error(tr("Failed to broadcast transaction"));
      }
   }
   else {
      logger_->error("[AuthAddressManager::onTXSigned] TX signing failed: {} {}"
         , bs::error::ErrorCodeToString(result).toStdString(), errorReason);
      emit Error(tr("Transaction sign error: %1").arg(bs::error::ErrorCodeToString(result)));
   }
}

bool AuthAddressManager::RevokeAddress(const bs::Address &address)
{
   const auto state = GetState(address);
   if ((state != AuthAddressState::Verifying) && (state != AuthAddressState::Verified)) {
      logger_->warn("[AuthAddressManager::RevokeAddress] attempting to revoke from incorrect state {}", (int)state);
      emit Error(tr("incorrect state"));
      return false;
   }
   if (!signingContainer_) {
      logger_->error("[AuthAddressManager::RevokeAddress] can't revoke without signing container");
      emit Error(tr("Missing signing container"));
      return false;
   }

   if (!addressVerificator_) {
      SPDLOG_LOGGER_ERROR(logger_, "addressVerificator_ is null");
      emit Error(tr("Missing address verificator"));
      return false;
   }

   const auto revokeData = addressVerificator_->getRevokeData(address);
   if (revokeData.first.empty() || !revokeData.second.isInitialized()) {
      logger_->error("[AuthAddressManager::RevokeAddress] failed to obtain revocation data");
      emit Error(tr("Missing revocation input"));
      return false;
   }

   const auto reqId = signingContainer_->signAuthRevocation(authWallet_->walletId(), address
      , revokeData.second, revokeData.first);
   if (!reqId) {
      logger_->error("[AuthAddressManager::RevokeAddress] failed to send revocation data");
      emit Error(tr("Failed to send revoke"));
      return false;
   }
   signIdsRevoke_.insert(reqId);
   return true;
}

void AuthAddressManager::OnDataReceived(const std::string& data)
{
   ResponsePacket response;

   if (!response.ParseFromString(data)) {
      logger_->error("[AuthAddressManager::OnDataReceived] failed to parse response from public bridge");
      return;
   }

   bool sigVerified = false;
   if (!response.has_datasignature()) {
      logger_->warn("[AuthAddressManager::OnDataReceived] Public bridge response of type {} has no signature!"
         , static_cast<int>(response.responsetype()));
   }
   else {
      const auto signAddress = bs::Address::fromAddressString(settings_->GetBlocksettleSignAddress()).prefixed();
      const auto message = BinaryData::fromString(response.responsedata());
      const auto signature = BinaryData::fromString(response.datasignature());

      sigVerified = ArmorySigner::Signer::verifyMessageSignature(message, signAddress, signature);
      if (!sigVerified) {
         logger_->error("[AuthAddressManager::OnDataReceived] Response signature verification failed - response {} dropped"
            , static_cast<int>(response.responsetype()));
         return;
      }
   }

   switch(response.responsetype()) {
   case RequestType::GetBSFundingAddressListType:
      ProcessBSAddressListResponse(response.responsedata(), sigVerified);
      break;
   default:
      break;
   }
}

void AuthAddressManager::ConfirmSubmitForVerification(const std::weak_ptr<BsClient> &bsClient, const bs::Address &address)
{
   logger_->debug("[AuthAddressManager::ConfirmSubmitForVerification] confirm submission of {}", address.display());

   auto bsClientPtr = bsClient.lock();
   bsClientPtr->signAuthAddress(address, [this, address, bsClient] (const BsClient::SignResponse &response) {
      if (response.userCancelled) {
         logger_->error("[AuthAddressManager::ConfirmSubmitForVerification sign cb] signing auth address cancelled: {}", response.errorMsg);
         emit AuthAddressSubmitCancelled(QString::fromStdString(address.display()));
         return;
      }

      if (!response.success) {
         logger_->error("[AuthAddressManager::ConfirmSubmitForVerification sign cb] signing auth address failed: {}", response.errorMsg);
         emit AuthAddressSubmitError(QString::fromStdString(address.display()), bs::error::AuthAddressSubmitResult::AuthRequestSignFailed);
         return;
      }

      logger_->debug("[AuthAddressManager::ConfirmSubmitForVerification sign cb] signing auth address succeed");

      auto bsClientPtr = bsClient.lock();
      if (!bsClientPtr) {
         logger_->error("[AuthAddressManager::ConfirmSubmitForVerification sign cb] disconnected from server");
         return;
      }

      bsClientPtr->confirmAuthAddress(address, [this, address] (bs::error::AuthAddressSubmitResult submitResult) {
         if (submitResult != bs::error::AuthAddressSubmitResult::Success) {
            logger_->error("[AuthAddressManager::ConfirmSubmitForVerification confirm cb] confirming auth address failed: {}", static_cast<int>(submitResult));
            emit AuthAddressSubmitError(QString::fromStdString(address.display()), submitResult);
            return;
         }

         logger_->debug("[AuthAddressManager::ConfirmSubmitForVerification confirm cb] confirming auth address succeed");

         markAsSubmitted(address);
      });
   });
}

void AuthAddressManager::SubmitToCeler(const bs::Address &address)
{
   if (celerClient_->IsConnected()) {
      const std::string addressString = address.display();
      std::unordered_set<std::string> submittedAddresses = celerClient_->GetSubmittedAuthAddressSet();
      if (submittedAddresses.find(addressString) == submittedAddresses.end()) {
         submittedAddresses.emplace(addressString);
         celerClient_->SetSubmittedAuthAddressSet(submittedAddresses);
      }
   }
   else {
      logger_->debug("[AuthAddressManager::SubmitToCeler] Celer is not connected");
   }
}

void AuthAddressManager::tryVerifyWalletAddresses()
{
   std::string errorMsg;
   ReadyError state = readyError();
   if (state != ReadyError::NoError) {
      SPDLOG_LOGGER_DEBUG(logger_, "can't start auth address verification: {}", readyErrorStr(state));
      return;
   }

   setup();

   VerifyWalletAddressesFunction();
}

void AuthAddressManager::VerifyWalletAddressesFunction()
{
   logger_->debug("[AuthAddressManager::VerifyWalletAddressesFunction] Starting to VerifyWalletAddresses");

   if (!HaveBSAddressList()) {
      logger_->debug("AuthAddressManager doesn't have BS addresses");
      return;
   }
   bool updated = false;

   if (!WalletAddressesLoaded()) {
      if (authWallet_ != nullptr) {
         for (const auto &addr : authWallet_->getUsedAddressList()) {
            AddAddress(addr);
         }
      }
      else {
         logger_->debug("AuthAddressManager auth wallet is null");
      }
      updated = true;
   }

   std::vector<bs::Address> listCopy;
   {
      FastLock locker(lockList_);
      listCopy = addresses_;
   }

   for (auto &addr : listCopy) {
      addressVerificator_->addAddress(addr);
   }
   addressVerificator_->startAddressVerification();

   if (updated) {
      emit VerifiedAddressListUpdated();
      emit AddressListUpdated();
   }
}

void AuthAddressManager::OnDisconnectedFromCeler()
{
   ClearAddressList();
}

void AuthAddressManager::ClearAddressList()
{
   bool adressListChanged = false;
   {
      FastLock locker(lockList_);
      if (!addresses_.empty()) {
         addresses_.clear();
         adressListChanged = true;
      }
   }

   if (adressListChanged) {
      emit AddressListUpdated();
      emit VerifiedAddressListUpdated();
   }
}

void AuthAddressManager::onWalletChanged(const std::string &walletId)
{
   bool listUpdated = false;
   if ((authWallet_ != nullptr) && (walletId == authWallet_->walletId())) {
      const auto &newAddresses = authWallet_->getUsedAddressList();
      const auto count = newAddresses.size();
      listUpdated = (count > addresses_.size());

      for (size_t i = addresses_.size(); i < count; i++) {
         const auto &addr = newAddresses[i];
         AddAddress(addr);
         if (addressVerificator_) {
            addressVerificator_->addAddress(addr);
         }
      }
   }

   if (listUpdated && addressVerificator_) {
      addressVerificator_->startAddressVerification();
      emit AddressListUpdated();
   }
}

void AuthAddressManager::AddAddress(const bs::Address &addr)
{
   SetExplicitState(addr, AuthAddressState::Unknown);

   FastLock locker(lockList_);
   addresses_.emplace_back(addr);
}

bool AuthAddressManager::HaveBSAddressList() const
{
   return !bsAddressList_.empty();
}

const std::unordered_set<std::string> &AuthAddressManager::GetBSAddresses() const
{
   return bsAddressList_;
}

void AuthAddressManager::setAuthAddressesSigned(const BinaryData &data)
{
   OnDataReceived(data.toBinStr());
}

std::string AuthAddressManager::readyErrorStr(AuthAddressManager::ReadyError error)
{
   switch (error) {
      case ReadyError::NoError:              return "NoError";
      case ReadyError::MissingAuthAddr:      return "MissingAuthAddr";
      case ReadyError::MissingAddressList:   return "MissingAddressList";
      case ReadyError::MissingArmoryPtr:     return "MissingArmoryPtr";
      case ReadyError::ArmoryOffline:        return "ArmoryOffline";
   }
   return "Unknown";
}

void AuthAddressManager::ProcessBSAddressListResponse(const std::string& response, bool sigVerified)
{
   GetBSFundingAddressListResponse recvList;

   if (!recvList.ParseFromString(response)) {
      logger_->error("[AuthAddressManager::ProcessBSAddressListResponse] data corrupted. Could not parse.");
      return;
   }
   if (!sigVerified) {
      logger_->error("[AuthAddressManager::ProcessBSAddressListResponse] rejecting unverified response");
      return;
   }

   std::unordered_set<std::string> tempList;
   int size = recvList.validation_address_size();
   for (int i = 0; i < size; i++) {
      tempList.emplace(recvList.validation_address(i));
   }

   logger_->debug("[AuthAddressManager::ProcessBSAddressListResponse] get {} BS addresses", tempList.size());

   ClearAddressList();
   SetBSAddressList(tempList);
   tryVerifyWalletAddresses();
}

AuthAddressManager::AuthAddressState AuthAddressManager::GetState(const bs::Address &addr) const
{
   FastLock lock(statesLock_);

   const auto itState = states_.find(addr);
   if (itState == states_.end()) {
      return AuthAddressState::Unknown;
   }

   return itState->second;
}

void AuthAddressManager::SetExplicitState(const bs::Address &addr, AuthAddressState state)
{
   FastLock lock(statesLock_);
   states_[addr] = state;
}

void AuthAddressManager::SetValidationState(const bs::Address &addr, AddressVerificationState state)
{
   const auto prevState = GetState(addr);

   AuthAddressState mappedState = AuthAddressState::Unknown;

   switch(state) {
   case AddressVerificationState::VerificationFailed:
      mappedState = AuthAddressState::Invalid;
      break;
   case AddressVerificationState::Virgin:
      mappedState = AuthAddressState::NotSubmitted;
      break;
   case AddressVerificationState::Tainted:
      mappedState = AuthAddressState::Tainted;
      break;
   case AddressVerificationState::Verifying:
      mappedState = AuthAddressState::Verifying;
      break;
   case AddressVerificationState::Verified:
      mappedState = AuthAddressState::Verified;
      break;
   case AddressVerificationState::Revoked:
      mappedState = AuthAddressState::Revoked;
      break;
   case AddressVerificationState::Invalidated_Explicit:
   case AddressVerificationState::Invalidated_Implicit:
      mappedState = AuthAddressState::RevokedByBS;
      break;
   }

   if (prevState == mappedState) {
      return;
   }

   if (mappedState == AuthAddressState::NotSubmitted) {
      if (prevState == AuthAddressState::Submitted) {
         return;
      }

      const auto &submittedAddresses = celerClient_->GetSubmittedAuthAddressSet();
      if (submittedAddresses.find(addr.display()) != submittedAddresses.end()) {
         mappedState = AuthAddressState::Submitted;
      }
   }

   SetExplicitState(addr, mappedState);

   if (mappedState == AuthAddressState::Verified
      && (  prevState == AuthAddressState::Verifying
         || prevState == AuthAddressState::Submitted)) {
      emit AddrVerifiedOrRevoked(QString::fromStdString(addr.display()), tr("Verified"));
      emit VerifiedAddressListUpdated();
   } else if (   (mappedState == AuthAddressState::Revoked || mappedState == AuthAddressState::RevokedByBS)
              && (prevState == AuthAddressState::Verified)) {
      emit AddrVerifiedOrRevoked(QString::fromStdString(addr.display()), tr("Revoked"));
   }

   emit AddrStateChanged();
   emit AddressListUpdated();
}

bool AuthAddressManager::BroadcastTransaction(const BinaryData& transactionData)
{
   return !armory_->broadcastZC(transactionData).empty();
}

void AuthAddressManager::setDefault(const bs::Address &addr)
{
   defaultAddr_ = addr;
   settings_->set(ApplicationSettings::defaultAuthAddr, QString::fromStdString(addr.display()));
   emit VerifiedAddressListUpdated();
}

bs::Address AuthAddressManager::getDefault() const
{
   if (defaultAddr_.empty()) {
      const auto &defaultAuthAddrStr = settings_->get<std::string>(ApplicationSettings::defaultAuthAddr);
      if (!defaultAuthAddrStr.empty()) {
         defaultAddr_ = bs::Address::fromAddressString(defaultAuthAddrStr);
      }
      auto verifAddresses = GetVerifiedAddressList();
      if (verifAddresses.empty()) {
         verifAddresses = GetSubmittedAddressList();
      }

      if (verifAddresses.empty()) {
         defaultAddr_.clear();
         return {};
      }
      const auto &it = std::find(verifAddresses.cbegin(), verifAddresses.cend(), defaultAddr_);
      if (defaultAddr_.empty() || (it == verifAddresses.end())) {
         defaultAddr_ = verifAddresses.at(0);
      }
   }
   return defaultAddr_;
}

size_t AuthAddressManager::getDefaultIndex() const
{
   if (defaultAddr_.empty()) {
      return 0;
   }
   size_t rv = 0;
   FastLock locker(lockList_);
   for (const auto& address : addresses_) {
      if (GetState(address) != AuthAddressState::Verified) {
         continue;
      }
      if (address.prefixed() == defaultAddr_.prefixed()) {
         return rv;
      }
      rv++;
   }
   return 0;
}

std::vector<bs::Address> AuthAddressManager::GetSubmittedAddressList(bool includeVerified /* = true */) const
{
   std::vector<bs::Address> list;
   {
      list.reserve(addresses_.size());

      FastLock locker(lockList_);
      for (const auto& address : addresses_) {
         const auto addressState = GetState(address);

         switch (addressState) {
         case AuthAddressState::Verified:
            if (!includeVerified) {
               break;
            }
         case AuthAddressState::Verifying:
         case AuthAddressState::Submitted:
         case AuthAddressState::Tainted:
            list.emplace_back(address);
            break;
         default:
            break;
         }
      }
   }
   return list;
}

std::vector<bs::Address> AuthAddressManager::GetVerifiedAddressList() const
{
   std::vector<bs::Address> list;
   {
      FastLock locker(lockList_);
      for (const auto& address : addresses_) {
         if (GetState(address) == AuthAddressState::Verified) {
            list.emplace_back(address);
         }
      }
   }
   return list;
}

bool AuthAddressManager::isAtLeastOneAwaitingVerification() const
{
   {
      FastLock locker(lockList_);
      for (const auto &address : addresses_) {
         auto addrState = GetState(address);
         if (addrState == AuthAddressState::Verifying
            || addrState == AuthAddressState::Verified) {
            return true;
         }
      }
   }
   return false;
}

bool AuthAddressManager::isAllLoadded() const
{
   {
      FastLock locker(lockList_);
      for (const auto &address : addresses_) {
         auto addrState = GetState(address);
         if (addrState == AuthAddressState::Unknown) {
            return false;
         }
      }
   }
   return true;
}

void AuthAddressManager::SetBSAddressList(const std::unordered_set<std::string>& bsAddressList)
{
   {
      FastLock locker(lockList_);
      bsAddressList_ = bsAddressList;

      if (!bsAddressList.empty()) {
         if (addressVerificator_) {
            addressVerificator_->SetBSAddressList(bsAddressList);
         }
      }
   }

   // Emit signal without holding lock
   emit gotBsAddressList();
}

void AuthAddressManager::onStateChanged(ArmoryState)
{
   QMetaObject::invokeMethod(this, [this]{
      tryVerifyWalletAddresses();
   });
}

void AuthAddressManager::markAsSubmitted(const bs::Address &address)
{
   SubmitToCeler(address);

   SetExplicitState(address, AuthAddressState::Submitted);

   emit AddressListUpdated();
   emit AuthAddressSubmitSuccess(QString::fromStdString(address.display()));
}

template <typename TVal> TVal AuthAddressManager::lookup(const bs::Address &key, const std::map<bs::Address, TVal> &container) const
{
   const auto it = container.find(key);
   if (it == container.end()) {
      return TVal();
   }
   return it->second;
}

void AuthAddressManager::onWalletCreated()
{
   auto authLeaf = walletsManager_->getAuthWallet();

   if (authLeaf != nullptr) {
      emit AuthWalletCreated(QString::fromStdString(authLeaf->walletId()));
   } else {
      logger_->error("[AuthAddressManager::onWalletCreated] we should be able to get auth wallet at this point");
   }
}

bool AuthAddressManager::hasSettlementLeaf(const bs::Address &addr) const
{
   return walletsManager_->hasSettlementLeaf(addr);
}

void AuthAddressManager::createSettlementLeaf(const bs::Address &addr
   , const std::function<void()> &cb)
{
   const auto &cbPubKey = [this, cb](const SecureBinaryData &pubKey) {
      if (pubKey.empty()) {
         return;
      }
      if (cb) {
         cb();
      }
   };
   walletsManager_->createSettlementLeaf(addr, cbPubKey);
}

bool AuthAddressManager::UserCanSubmitAuthAddress() const
{
   size_t submittedAddressCount = GetSubmittedAddressList(false).size();

   size_t maxSubmitCount = 0;

   if (userType_ == bs::network::UserType::Dealing) {
      maxSubmitCount = tradeSettings_->dealerAuthSubmitAddressLimit;
   } else if (userType_ == bs::network::UserType::Trading) {
      maxSubmitCount = tradeSettings_->authSubmitAddressLimit;
   }

   return maxSubmitCount > submittedAddressCount;
}

void AuthAddressManager::setUserType(bs::network::UserType userType)
{
   userType_ = userType;
}
