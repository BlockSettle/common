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

namespace {

   AddressNetworkType networkType(NetworkType netType)
   {
      return netType == NetworkType::MainNet ? AddressNetworkType::MainNetType : AddressNetworkType::TestNetType;
   }

   AddressNetworkType networkType(const std::shared_ptr<ApplicationSettings> &settings)
   {
      return networkType(settings->get<NetworkType>(ApplicationSettings::netType));
   }

} // namespace

CCFileManager::CCFileManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<ConnectionManager>& connectionManager
   , const ZmqBipNewKeyCb &cb)
   : CCPubConnection(logger, connectionManager, cb)
   , appSettings_(appSettings)
{
   const auto &cbSecLoaded = [this](const bs::network::CCSecurityDef &ccSecDef) {
      emit CCSecurityDef(ccSecDef);
      emit CCSecurityId(ccSecDef.securityId);
      emit CCSecurityInfo(QString::fromStdString(ccSecDef.product), QString::fromStdString(ccSecDef.description)
         , (unsigned long)ccSecDef.nbSatoshis, QString::fromStdString(ccSecDef.genesisAddr.display()));
   };
   const auto &cbLoadComplete = [this] (unsigned int rev) {
      logger_->debug("[CCFileManager] loading complete, last revision: {}", rev);
      currentRev_ = rev;
      emit Loaded();
   };
   resolver_ = std::make_shared<CCPubResolver>(logger_
      , BinaryData::CreateFromHex(appSettings_->get<std::string>(ApplicationSettings::bsPublicKey))
      , cbSecLoaded, cbLoadComplete);

   connect(appSettings_.get(), &ApplicationSettings::settingChanged, this, &CCFileManager::onPubSettingsChanged
      , Qt::QueuedConnection);

   ccFilePath_ = appSettings->ccFilePath();
}

void CCFileManager::onPubSettingsChanged(int setting, QVariant)
{
   switch (setting) {
      case ApplicationSettings::customPubBridgeHost:
      case ApplicationSettings::customPubBridgePort:
      case ApplicationSettings::envConfiguration:
         RemoveAndDisableFileSave();
         break;
      default:
         break;
   }
}

void CCFileManager::RemoveAndDisableFileSave()
{
   saveToFileDisabled_ = true;
   if (QFile::exists(ccFilePath_)) {
      logger_->debug("[CCFileManager::RemoveAndDisableFileSave] remove {} and disable save"
         , ccFilePath_.toStdString());
      QFile::remove(ccFilePath_);
   } else {
      logger_->debug("[CCFileManager::RemoveAndDisableFileSave] disabling saving on cc gen file");
   }
}

bool CCFileManager::hasLocalFile() const
{
   return QFile(ccFilePath_).exists();
}

void CCFileManager::setBsClient(BsClient *bsClient)
{
   bsClient_ = bsClient;
}

void CCFileManager::LoadSavedCCDefinitions()
{
   if (!resolver_->loadFromFile(ccFilePath_.toStdString(), appSettings_->get<NetworkType>(ApplicationSettings::netType))) {
      emit LoadingFailed();
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

   if (genAddrResp.networktype() != networkType(appSettings_)) {
      logger_->error("[CCFileManager::ProcessCCGenAddressesResponse] network type mismatch in reply: {}"
         , (int)genAddrResp.networktype());
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

   if (saveToFileDisabled_) {
      logger_->debug("[{}] save to file disabled", __func__);
      return;
   }

   resolver_->saveToFile(ccFilePath_.toStdString(), response, sig);
}

bool CCFileManager::SubmitAddressToPuB(const bs::Address &address, uint32_t seed, const std::string &srcToken)
{
   if (!celerClient_) {
      logger_->error("[CCFileManager::SubmitAddressToPuB] not connected");
      return false;
   }

   if (!bsClient_) {
      SPDLOG_LOGGER_ERROR(logger_, "not connected to BsProxy");
      return false;
   }

   if (!address.isValid()) {
      SPDLOG_LOGGER_ERROR(logger_, "can't submit invalid CC address: '{}'", address.display());
      return false;
   }

   SubmitAddrForInitialDistributionRequest request;
   request.set_username(celerClient_->email());
   request.set_networktype(networkType(appSettings_));
   request.set_prefixedaddress(address.display());
   request.set_bsseed(seed);

   std::string requestData = request.SerializeAsString();
   BinaryData requestDataHash = BtcUtils::getSha256(requestData);

   BsClient::SignAddressReq req;
   req.type = BsClient::SignAddressReq::CcAddr;
   req.address = address;
   req.invisibleData = requestDataHash;
   req.srcCcToken = srcToken;

   req.signedCb = [this, address, requestData](const AutheIDClient::SignResult &result) {
      // No need to check result.data (AutheIDClient will check that invisible data is the same)

      RequestPacket packet;
      packet.set_requesttype(SubmitCCAddrInitialDistribType);
      packet.set_requestdata(requestData);

      // Copy AuthEid signature
      auto autheidSign = packet.mutable_autheidsign();
      autheidSign->set_serialization(AuthEidSign::Serialization(result.serialization));
      autheidSign->set_signature_data(result.data.toBinStr());
      autheidSign->set_sign(result.sign.toBinStr());
      autheidSign->set_certificate_client(result.certificateClient.toBinStr());
      autheidSign->set_certificate_issuer(result.certificateIssuer.toBinStr());
      autheidSign->set_ocsp_response(result.ocspResponse.toBinStr());

      logger_->debug("[CCFileManager::SubmitAddressToPuB] submitting addr {}", address.display());
      if (SubmitRequestToPB("submit_cc_addr", packet.SerializeAsString())) {
         emit CCInitialSubmitted(QString::fromStdString(address.display()));
      }
      else {
         emit CCSubmitFailed(QString::fromStdString(address.display()), tr("Failed to send to PB"));
      }
   };

   req.failedCb = [this, address](AutheIDClient::ErrorType error) {
      std::string errorStr = fmt::format("failed to sign data: {}", AutheIDClient::errorString(error).toStdString());
      SPDLOG_LOGGER_ERROR(logger_, "{}", errorStr);
      emit CCSubmitFailed(QString::fromStdString(address.display()), QString::fromStdString(errorStr));
   };

   bsClient_->signAddress(req);
   return true;
}

void CCFileManager::ProcessSubmitAddrResponse(const std::string& responseString)
{
   SubmitAddrForInitialDistributionResponse response;
   if (!response.ParseFromString(responseString)) {
      logger_->error("[CCFileManager::ProcessSubmitAddrResponse] failed to parse response");
      return;
   }

   auto addr = bs::Address::fromAddressString(response.prefixedaddress());

   if (!response.success()) {
      if (response.has_errormessage()) {
         logger_->error("[CCFileManager::ProcessSubmitAddrResponse] recv address {} rejected: {}"
            , addr.display(), response.errormessage());
      }
      else {
         logger_->error("[CCFileManager::ProcessSubmitAddrResponse] recv address {} rejected", addr.display());
      }
      return;
   }

   if (celerClient_->IsConnected()) {
      if (!celerClient_->SetCCAddressSubmitted(addr.display())) {
         logger_->warn("[CCFileManager::ProcessSubmitAddrResponse] failed to save address {} request event to Celer's user storage"
            , addr.display());
      }
   }

   logger_->debug("[CCFileManager::ProcessSubmitAddrResponse] {} succeeded", addr.display());

   emit CCAddressSubmitted(QString::fromStdString(addr.display()));
}

std::string CCFileManager::GetPuBHost() const
{
   return appSettings_->pubBridgeHost();
}

std::string CCFileManager::GetPuBPort() const
{
   return appSettings_->pubBridgePort();
}

std::string CCFileManager::GetPuBKey() const
{
   return appSettings_->get<std::string>(ApplicationSettings::pubBridgePubKey);
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
   const auto walletIdx = bs::hd::Path::keyToElem(ccDef.product);
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
   idx &= ~bs::hd::hardFlag;
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

std::string CCPubResolver::descriptionFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.description;
   }
   return {};
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
         ccSecurity.securityid(), ccSecurity.product(), ccSecurity.description(),
         bs::Address::fromAddressString(ccSecurity.genesisaddr()), ccSecurity.satoshisnb()
      };
      add(ccSecDef);
   }
   logger_->debug("[CCFileManager::ProcessCCGenAddressesResponse] got {} CC gen address[es]", securities_.size());
   cbLoadComplete_(resp->revision());
}

bool CCPubResolver::loadFromFile(const std::string &path, NetworkType netType)
{
   QFile f(QString::fromStdString(path));
   if (!f.exists()) {
      logger_->debug("[CCFileManager::LoadFromFile] no cc file to load at {}", path);
      return true;
   }
   if (!f.open(QIODevice::ReadOnly)) {
      logger_->error("[CCFileManager::LoadFromFile] failed to open file {} for reading", path);
      return false;
   }

   const auto buf = f.readAll();
   if (buf.isEmpty()) {
      logger_->error("[CCFileManager::LoadFromFile] failed to read from {}", path);
      return false;
   }

   Blocksettle::Storage::CCDefinitions msg;
   bool result = msg.ParseFromArray(buf.data(), buf.size());
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse storage file");
      return false;
   }

   result = verifySignature(msg.response(), msg.signature());
   if (!result) {
      logger_->error("[CCFileManager::LoadFromFile] signature verification failed for {}", path);
      return false;
   }

   GetCCGenesisAddressesResponse resp;
   if (!resp.ParseFromString(msg.response())) {
      logger_->error("[CCFileManager::LoadFromFile] failed to parse {}", path);
      return false;
   }

   if (resp.networktype() != networkType(netType)) {
      logger_->error("[CCFileManager::LoadFromFile] wrong network type in {}", path);
      return false;
   }

   fillFrom(&resp);
   return true;
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

bool CCPubResolver::verifySignature(const std::string& data, const std::string& signature) const
{
   return CryptoECDSA().VerifyData(data, signature, pubKey_);
}
