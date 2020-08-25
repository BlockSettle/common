/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CCFileManager.h"

#include "ApplicationSettings.h"
#include "CelerClient.h"
#include "ConnectionManager.h"
#include "EncryptionUtils.h"
#include "HDPath.h"

#include <spdlog/spdlog.h>

#include <cassert>

#include <QFile>

#include "bs_communication.pb.h"
#include "bs_storage.pb.h"

using namespace Blocksettle::Communication;

CCFileManager::CCFileManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ApplicationSettings> &appSettings)
   : CCPubConnection(logger)
   , appSettings_(appSettings)
{
   const auto &cbSecLoaded = [this](const bs::network::CCSecurityDef &ccSecDef) {
      emit CCSecurityDef(ccSecDef);
      emit CCSecurityId(ccSecDef.securityId);
      emit CCSecurityInfo(QString::fromStdString(ccSecDef.product)
         , (unsigned long)ccSecDef.nbSatoshis, QString::fromStdString(ccSecDef.genesisAddr.display()));
   };
   const auto &cbLoadComplete = [this] (unsigned int rev) {
      logger_->debug("[CCFileManager] loading complete, last revision: {}", rev);
      currentRev_ = rev;
      emit Loaded();
   };
   resolver_ = std::make_shared<CCPubResolver>(logger_
      , appSettings_->GetBlocksettleSignAddress()
      , cbSecLoaded, cbLoadComplete);

   ccFilePath_ = appSettings->ccFilePath();
}

bool CCFileManager::hasLocalFile() const
{
   return QFile(ccFilePath_).exists();
}

void CCFileManager::setBsClient(const std::weak_ptr<BsClient> &bsClient)
{
   bsClient_ = bsClient;
}

void CCFileManager::setCcAddressesSigned(const BinaryData &data)
{
   OnDataReceived(data.toBinStr());
}

void CCFileManager::LoadSavedCCDefinitions()
{
   auto loadError = resolver_->loadFromFile(ccFilePath_.toStdString(), appSettings_->get<NetworkType>(ApplicationSettings::netType));
   if (loadError != CcGenFileError::NoError) {
      emit LoadingFailed(loadError);
      QFile::remove(ccFilePath_);
   }
}

void CCFileManager::ConnectToCelerClient(const std::shared_ptr<BaseCelerClient> &celerClient)
{
   celerClient_ = celerClient;
}

bool CCFileManager::wasAddressSubmitted(const bs::Address &addr)
{
   return celerClient_->IsCCAddressSubmitted(addr.display());
}

void CCFileManager::cancelActiveSign()
{
   auto bsClient = bsClient_.lock();
   if (bsClient) {
      bsClient->cancelActiveSign();
   }
}

void CCFileManager::ProcessGenAddressesResponse(const std::string& response, const std::string &sig)
{
   bool sigVerified = resolver_->verifySignature(response, sig);
   if (!sigVerified) {
      SPDLOG_LOGGER_ERROR(logger_, "Signature verification failed! Rejecting CC genesis addresses reply.");
      return;
   }

   GetCCGenesisAddressesResponse genAddrResp;

   if (!genAddrResp.ParseFromString(response)) {
      logger_->error("[CCFileManager::ProcessCCGenAddressesResponse] data corrupted. Could not parse.");
      return;
   }

   if (genAddrResp.is_testnet() != (appSettings_->get<NetworkType>(ApplicationSettings::netType) == NetworkType::TestNet)) {
      logger_->error("[CCFileManager::ProcessCCGenAddressesResponse] network type mismatch in reply");
      return;
   }

   if (currentRev_ > 0 && genAddrResp.revision() == currentRev_) {
      logger_->debug("[CCFileManager::ProcessCCGenAddressesResponse] having the same revision already");
      return;
   }

   if (genAddrResp.revision() < currentRev_) {
      logger_->warn("[CCFileManager::ProcessCCGenAddressesResponse] PuB has older revision {} than we ({})"
         , genAddrResp.revision(), currentRev_);
   }

   resolver_->fillFrom(&genAddrResp);

   resolver_->saveToFile(ccFilePath_.toStdString(), response, sig);
}

bool CCFileManager::submitAddress(const bs::Address &address, uint32_t seed, const std::string &ccProduct)
{
   auto bsClient = bsClient_.lock();

   if (!celerClient_) {
      logger_->error("[CCFileManager::SubmitAddressToPuB] not connected");
      return false;
   }

   if (!bsClient) {
      SPDLOG_LOGGER_ERROR(logger_, "not connected to BsProxy");
      return false;
   }

   if (!address.isValid()) {
      SPDLOG_LOGGER_ERROR(logger_, "can't submit invalid CC address: '{}'", address.display());
      return false;
   }

   bsClient->submitCcAddress(address, seed, ccProduct, [this, address](const BsClient::BasicResponse &result) {
      auto bsClient = bsClient_.lock();

      if (!result.success) {
         SPDLOG_LOGGER_ERROR(logger_, "submit CC address failed: '{}'", result.errorMsg);
         emit CCSubmitFailed(QString::fromStdString(address.display()), QString::fromStdString(result.errorMsg));
         return;
      }

      emit CCInitialSubmitted(QString::fromStdString(address.display()));

      if (!bsClient) {
         SPDLOG_LOGGER_ERROR(logger_, "disconnected from server");
         return;
      }

      bsClient->signCcAddress(address, [this, address](const BsClient::SignResponse &result) {
         auto bsClient = bsClient_.lock();

         if (result.userCancelled) {
            SPDLOG_LOGGER_DEBUG(logger_, "signing CC address cancelled: '{}'", result.errorMsg);
            emit CCSubmitFailed(QString::fromStdString(address.display()), tr("Cancelled"));
            return;
         }

         if (!result.success) {
            SPDLOG_LOGGER_ERROR(logger_, "signing CC address failed: '{}'", result.errorMsg);
            emit CCSubmitFailed(QString::fromStdString(address.display()), QString::fromStdString(result.errorMsg));
            return;
         }

         if (!bsClient) {
            SPDLOG_LOGGER_ERROR(logger_, "disconnected from server");
            return;
         }

         bsClient->confirmCcAddress(address, [this, address](const BsClient::BasicResponse &result) {
            if (!result.success) {
               SPDLOG_LOGGER_ERROR(logger_, "confirming CC address failed: '{}'", result.errorMsg);
               emit CCSubmitFailed(QString::fromStdString(address.display()), QString::fromStdString(result.errorMsg));
               return;
            }

            if (!celerClient_->SetCCAddressSubmitted(address.display())) {
               SPDLOG_LOGGER_WARN(logger_, "failed to save address {} request event to Celer's user storage"
                  , address.display());
            }

            emit CCAddressSubmitted(QString::fromStdString(address.display()));
         });
      });
   });

   return true;
}

bool CCFileManager::IsTestNet() const
{
   return appSettings_->get<NetworkType>(ApplicationSettings::netType) != NetworkType::MainNet;
}


void CCPubResolver::clear()
{
   securities_.clear();
   walletIdxMap_.clear();
}

void CCPubResolver::add(const bs::network::CCSecurityDef &ccDef)
{
   securities_[ccDef.product] = ccDef;
   const auto walletIdx = bs::hd::Path::keyToElem(ccDef.product) | bs::hd::hardFlag;
   walletIdxMap_[walletIdx] = ccDef.product;
   cbSecLoaded_(ccDef);
}

std::vector<std::string> CCPubResolver::securities() const
{
   std::vector<std::string> result;
   for (const auto &ccDef : securities_) {
      result.push_back(ccDef.first);
   }
   return result;
}

std::string CCPubResolver::nameByWalletIndex(bs::hd::Path::Elem idx) const
{
   idx |= bs::hd::hardFlag;
   const auto &itWallet = walletIdxMap_.find(idx);
   if (itWallet != walletIdxMap_.end()) {
      return itWallet->second;
   }
   return {};
}

uint64_t CCPubResolver::lotSizeFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.nbSatoshis;
   }
   return 0;
}

bs::Address CCPubResolver::genesisAddrFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.genesisAddr;
   }
   return {};
}

void CCPubResolver::fillFrom(Blocksettle::Communication::GetCCGenesisAddressesResponse *resp)
{
   clear();
   for (int i = 0; i < resp->ccsecurities_size(); i++) {
      const auto ccSecurity = resp->ccsecurities(i);

      bs::network::CCSecurityDef ccSecDef = {
         ccSecurity.securityid(), ccSecurity.product(),
         bs::Address::fromAddressString(ccSecurity.genesisaddr()), ccSecurity.satoshisnb()
      };
      add(ccSecDef);
   }
   logger_->debug("[CCFileManager::ProcessCCGenAddressesResponse] got {} CC gen address[es]", securities_.size());
   cbLoadComplete_(resp->revision());
}

CcGenFileError CCPubResolver::loadFromFile(const std::string &path, NetworkType netType)
{
   QFile f(QString::fromStdString(path));
   if (!f.exists()) {
      logger_->debug("[CCFileManager::LoadFromFile] no cc file to load at {}", path);
      return CcGenFileError::ReadError;
   }
   if (!f.open(QIODevice::ReadOnly)) {
      logger_->error("[CCFileManager::LoadFromFile] failed to open file {} for reading", path);
      return CcGenFileError::ReadError;
   }

   const auto buf = f.readAll();
   if (buf.isEmpty()) {
      logger_->error("[CCFileManager::LoadFromFile] failed to read from {}", path);
      return CcGenFileError::ReadError;
   }

   Blocksettle::Storage::CCDefinitions msg;
   bool result = msg.ParseFromArray(buf.data(), buf.size());
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse storage file");
      return CcGenFileError::InvalidFormat;
   }

   result = verifySignature(msg.response(), msg.signature());
   if (!result) {
      logger_->error("[CCFileManager::LoadFromFile] signature verification failed for {}", path);
      return CcGenFileError::InvalidSign;
   }

   GetCCGenesisAddressesResponse resp;
   if (!resp.ParseFromString(msg.response())) {
      logger_->error("[CCFileManager::LoadFromFile] failed to parse {}", path);
      return CcGenFileError::InvalidFormat;
   }

   if (resp.is_testnet() != (netType == NetworkType::TestNet)) {
      logger_->error("[CCFileManager::LoadFromFile] wrong network type in {}", path);
      return CcGenFileError::InvalidFormat;
   }

   fillFrom(&resp);
   return CcGenFileError::NoError;
}

bool CCPubResolver::saveToFile(const std::string &path, const std::string &response, const std::string &sig)
{
   Blocksettle::Storage::CCDefinitions msg;
   msg.set_response(response);
   msg.set_signature(sig);
   auto data = msg.SerializeAsString();

   QFile f(QString::fromStdString(path));
   if (!f.open(QIODevice::WriteOnly)) {
      logger_->error("[CCFileManager::SaveToFile] failed to open file {} for writing", path);
      return false;
   }

   auto writeSize = f.write(data.data(), int(data.size()));
   if (data.size() != size_t(writeSize)) {
      logger_->error("[CCFileManager::SaveToFile] failed to write to {}", path);
      return false;
   }

   return true;
}

bool CCPubResolver::verifySignature(const std::string& data, const std::string& signatureStr) const
{
   const auto message = BinaryData::fromString(data);
   const auto signature = BinaryData::fromString(signatureStr);
   const auto signAddress = bs::Address::fromAddressString(signAddress_).prefixed();

   return ArmorySigner::Signer::verifyMessageSignature(message, signAddress, signature);
}
