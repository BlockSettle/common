#ifndef __AUTH_SIGN_MANAGER_H__
#define __AUTH_SIGN_MANAGER_H__

#include <functional>
#include <memory>
#include <QObject>
#include "BinaryData.h"

namespace spdlog {
   class logger;
}
class ApplicationSettings;
class CelerClient;
class AutheIDClient;
class SecureBinaryData;
class ConnectionManager;

class AuthSignManager : public QObject
{
Q_OBJECT

public:
   AuthSignManager(const std::shared_ptr<spdlog::logger>& logger
      , const std::shared_ptr<ApplicationSettings>& appSettings
      , const std::shared_ptr<CelerClient> &, const std::shared_ptr<ConnectionManager> &);
   ~AuthSignManager() noexcept;

   AuthSignManager(const AuthSignManager&) = delete;
   AuthSignManager& operator = (const AuthSignManager&) = delete;
   AuthSignManager(AuthSignManager&&) = delete;
   AuthSignManager& operator = (AuthSignManager&&) = delete;

   using SignedCb = std::function<void (const std::string &data, const BinaryData &invisibleData, const std::string &signature)>;
   using SignFailedCb = std::function<void(const QString &)>;
   bool Sign(const BinaryData &dataToSign, const QString &title, const QString &desc
      , const SignedCb &, const SignFailedCb &cbF = nullptr, int expiration = 30);

private slots:
   void onSignSuccess(const std::string &data, const BinaryData &invisibleData, const std::string &signature);
   void onFailed(const QString &);

private:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<CelerClient>           celerClient_;
   std::unique_ptr<AutheIDClient>         autheIDClient_;
   SignedCb                               onSignedCB_;
   SignFailedCb                           onSignFailedCB_;
};

#endif // __AUTH_SIGN_MANAGER_H__
