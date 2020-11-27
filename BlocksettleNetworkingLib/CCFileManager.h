/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CC_FILE_MANAGER_H__
#define __CC_FILE_MANAGER_H__

#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QVariant>
#include <QPointer>

#include "BsClient.h"
#include "Wallets/SyncWallet.h"

class ApplicationSettings;
class BaseCelerClient;

class CCPubResolver : public bs::sync::CCDataResolver
{
public:

   using CCSecLoadedCb = std::function<void(const bs::network::CCSecurityDef &)>;
   using CCLoadCompleteCb = std::function<void()>;
   CCPubResolver(const std::shared_ptr<spdlog::logger> &logger
      , const std::string &signAddress, const CCSecLoadedCb &cbSec
      , const CCLoadCompleteCb &cbLoad)
      : logger_(logger), signAddress_(signAddress), cbSecLoaded_(cbSec)
      , cbLoadComplete_(cbLoad) {}

   std::string nameByWalletIndex(bs::hd::Path::Elem) const override;
   uint64_t lotSizeFor(const std::string &cc) const override;
   bs::Address genesisAddrFor(const std::string &cc) const override;
   std::vector<std::string> securities() const override;

   void fillFrom(const std::vector<bs::network::CCSecurityDef>& definitions);

private:
   void add(const bs::network::CCSecurityDef &);
   void clear();

private:
   std::shared_ptr<spdlog::logger>  logger_;
   const std::string                signAddress_;
   std::map<std::string, bs::network::CCSecurityDef>  securities_;
   std::map<bs::hd::Path::Elem, std::string>          walletIdxMap_;
   const CCSecLoadedCb     cbSecLoaded_;
   const CCLoadCompleteCb  cbLoadComplete_;
};

class CCFileManager : public QObject
{
Q_OBJECT
public:
   CCFileManager(const std::shared_ptr<spdlog::logger> &logger, const std::shared_ptr<ApplicationSettings> &appSettings);
   ~CCFileManager() noexcept override = default;

   CCFileManager(const CCFileManager&) = delete;
   CCFileManager& operator = (const CCFileManager&) = delete;
   CCFileManager(CCFileManager&&) = delete;
   CCFileManager& operator = (CCFileManager&&) = delete;

   std::shared_ptr<bs::sync::CCDataResolver> getResolver() const { return resolver_; }

   void ConnectToCelerClient(const std::shared_ptr<BaseCelerClient> &);

   bool submitAddress(const bs::Address &, uint32_t seed, const std::string &ccProduct);
   void cancelActiveSign();

   void setBsClient(const std::weak_ptr<BsClient> &);

   void SetLoadedDefinitions(const std::vector<bs::network::CCSecurityDef>& definitions);

signals:
   void CCSecurityDef(bs::network::CCSecurityDef);
   void CCSecurityId(const std::string& securityId);
   void CCSecurityInfo(QString ccProd, unsigned long nbSatoshis, QString genesisAddr);

   void CCAddressSubmitted(const QString);
   void CCInitialSubmitted(const QString);
   void CCSubmitFailed(const QString address, const QString &err);
   void Loaded();

private:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<BaseCelerClient>       celerClient_;

   std::shared_ptr<CCPubResolver>         resolver_;
   std::weak_ptr<BsClient>                bsClient_;
};

#endif // __CC_FILE_MANAGER_H__
