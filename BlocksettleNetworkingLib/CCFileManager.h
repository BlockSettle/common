#ifndef __CC_FILE_MANAGER_H__
#define __CC_FILE_MANAGER_H__

#include "CCPubConnection.h"

#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QVariant>

#include "Wallets/SyncWallet.h"

namespace Blocksettle {
   namespace Communication {
      class GetCCGenesisAddressesResponse;
   }
}

class ApplicationSettings;
class AuthSignManager;
class BaseCelerClient;

class CCPubResolver : public bs::sync::CCDataResolver
{
public:
   using CCSecLoadedCb = std::function<void(const bs::network::CCSecurityDef &)>;
   using CCLoadCompleteCb = std::function<void(unsigned int)>;
   CCPubResolver(const std::shared_ptr<spdlog::logger> &logger
      , const SecureBinaryData &bsPubKey, const CCSecLoadedCb &cbSec
      , const CCLoadCompleteCb &cbLoad)
      : logger_(logger), pubKey_(bsPubKey), cbSecLoaded_(cbSec)
      , cbLoadComplete_(cbLoad) {}

   std::string nameByWalletIndex(bs::hd::Path::Elem) const override;
   uint64_t lotSizeFor(const std::string &cc) const override;
   bs::Address genesisAddrFor(const std::string &cc) const override;
   std::string descriptionFor(const std::string &cc) const override;
   std::vector<std::string> securities() const override;

   void fillFrom(Blocksettle::Communication::GetCCGenesisAddressesResponse *resp);
   bool loadFromFile(const std::string &path);
   bool saveToFile(const std::string &path, unsigned int rev
      , const std::string &signature);

private:
   void add(const bs::network::CCSecurityDef &);
   void clear();
   bool verifySignature(const std::string& data, const std::string& signature) const;

private:
   std::shared_ptr<spdlog::logger>  logger_;
   const SecureBinaryData           pubKey_;
   std::map<std::string, bs::network::CCSecurityDef>  securities_;
   std::map<bs::hd::Path::Elem, std::string>          walletIdxMap_;
   const CCSecLoadedCb     cbSecLoaded_;
   const CCLoadCompleteCb  cbLoadComplete_;
};

class CCFileManager : public CCPubConnection
{
Q_OBJECT
public:
   CCFileManager(const std::shared_ptr<spdlog::logger> &logger, const std::shared_ptr<ApplicationSettings> &appSettings
      , const std::shared_ptr<AuthSignManager> &
      , const std::shared_ptr<ConnectionManager> &, const ZmqBIP15XDataConnection::cbNewKey &cb = nullptr);
   ~CCFileManager() noexcept override = default;

   CCFileManager(const CCFileManager&) = delete;
   CCFileManager& operator = (const CCFileManager&) = delete;
   CCFileManager(CCFileManager&&) = delete;
   CCFileManager& operator = (CCFileManager&&) = delete;

   std::shared_ptr<bs::sync::CCDataResolver> getResolver() const { return resolver_; }
   bool synchronized() const { return syncFinished_; }

   void LoadSavedCCDefinitions();
   void ConnectToCelerClient(const std::shared_ptr<BaseCelerClient> &);

   bool SubmitAddressToPuB(const bs::Address &, uint32_t seed);
   bool wasAddressSubmitted(const bs::Address &);

   bool hasLocalFile() const;

signals:
   void CCSecurityDef(bs::network::CCSecurityDef);
   void CCSecurityId(const std::string& securityId);
   void CCSecurityInfo(QString ccProd, QString ccDesc, unsigned long nbSatoshis, QString genesisAddr);

   void CCAddressSubmitted(const QString);
   void CCInitialSubmitted(const QString);
   void CCSubmitFailed(const QString address, const QString &err);
   void Loaded();
   void LoadingFailed();

private slots:
   void onPubSettingsChanged(int setting, QVariant value);

private:
   void RemoveAndDisableFileSave();

protected:
   void ProcessGenAddressesResponse(const std::string& response, bool sigVerified, const std::string &sig) override;
   void ProcessSubmitAddrResponse(const std::string& response) override;

   bool IsTestNet() const override;

   std::string GetPuBHost() const override;
   std::string GetPuBPort() const override;
   std::string GetPuBKey() const override;

private:
   std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<BaseCelerClient>           celerClient_;
   std::shared_ptr<AuthSignManager>       authSignManager_;

   // when user changes PuB connection settings - save to file should be disabled.
   // dev build feature only. final release should have single PuB.
   bool saveToFileDisabled_ = false;

   bool syncStarted_ = false;
   bool syncFinished_ = false;

   std::shared_ptr<CCPubResolver>   resolver_;

private:
   bool RequestFromPuB();
};

#endif // __CC_FILE_MANAGER_H__
