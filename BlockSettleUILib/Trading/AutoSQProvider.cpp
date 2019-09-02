#include "AutoSQProvider.h"

#include "ApplicationSettings.h"
#include "SignContainer.h"
#include "WalletManager.h"
#include "Wallets/SyncWalletsManager.h"
#include "Wallets/SyncHDWallet.h"
#include "UserScriptRunner.h"

#include <BaseCelerClient.h>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>

AutoSQProvider::AutoSQProvider(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<AssetManager>& assetManager
   , const std::shared_ptr<QuoteProvider>& quoteProvider
   , const std::shared_ptr<ApplicationSettings> &appSettings
   , const std::shared_ptr<bs::DealerUtxoResAdapter> &dealerUtxoAdapter
   , const std::shared_ptr<SignContainer> &container
   , const std::shared_ptr<MarketDataProvider> &mdProvider
   , const std::shared_ptr<BaseCelerClient> &celerClient
   , QObject *parent)
   : QObject(parent)
   , appSettings_(appSettings)
   , logger_(logger)
   , signingContainer_(container)
   , celerClient_(celerClient)
{
   aq_ = new UserScriptRunner(quoteProvider, dealerUtxoAdapter, container,
      mdProvider, assetManager, logger, this);

   if (walletsManager_) {
      aq_->setWalletsManager(walletsManager_);
   }

   if (signingContainer_) {
      connect(signingContainer_.get(), &SignContainer::ready, this, &AutoSQProvider::onSignerStateUpdated, Qt::QueuedConnection);
      connect(signingContainer_.get(), &SignContainer::disconnected, this, &AutoSQProvider::onSignerStateUpdated, Qt::QueuedConnection);
      connect(signingContainer_.get(), &SignContainer::AutoSignStateChanged, this, &AutoSQProvider::onAutoSignStateChanged);
   }

   connect(aq_, &UserScriptRunner::aqScriptLoaded, this, &AutoSQProvider::onAqScriptLoaded);
   connect(aq_, &UserScriptRunner::failedToLoad, this, &AutoSQProvider::onAqScriptFailed);

   onSignerStateUpdated();

   auto botFileInfo = QFileInfo(getDefaultScriptsDir() + QStringLiteral("/RFQBot.qml"));
   if (botFileInfo.exists() && botFileInfo.isFile()) {
      auto list = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
      if (list.indexOf(botFileInfo.absoluteFilePath()) == -1) {
         list << botFileInfo.absoluteFilePath();
      }
      appSettings_->set(ApplicationSettings::aqScripts, list);
      const auto lastScript = appSettings_->get<QString>(ApplicationSettings::lastAqScript);
      if (lastScript.isEmpty()) {
         appSettings_->set(ApplicationSettings::lastAqScript, botFileInfo.absoluteFilePath());
      }

   }

   connect(celerClient_.get(), &BaseCelerClient::OnConnectedToServer, this, &AutoSQProvider::onConnectedToCeler);
   connect(celerClient_.get(), &BaseCelerClient::OnConnectionClosed, this, &AutoSQProvider::onDisconnectedFromCeler);
}

void AutoSQProvider::onSignerStateUpdated()
{
   disableAutoSign();
   autoQuoter()->disableAQ();

   emit autoSQAvailabilityChanged();
}

void AutoSQProvider::disableAutoSign()
{
   if (!walletsManager_) {
      return;
   }

   const auto wallet = walletsManager_->getPrimaryWallet();
   if (!wallet) {
      logger_->error("Failed to obtain auto-sign primary wallet");
      return;
   }

   QVariantMap data;
   data[QLatin1String("rootId")] = QString::fromStdString(wallet->walletId());
   data[QLatin1String("enable")] = false;
   signingContainer_->customDialogRequest(bs::signer::ui::DialogType::ActivateAutoSign, data);
}

void AutoSQProvider::tryEnableAutoSign()
{
   if (!walletsManager_ || !signingContainer_) {
      return;
   }

   const auto wallet = walletsManager_->getPrimaryWallet();
   if (!wallet) {
      logger_->error("Failed to obtain auto-sign primary wallet");
      return;
   }

   QVariantMap data;
   data[QLatin1String("rootId")] = QString::fromStdString(wallet->walletId());
   data[QLatin1String("enable")] = true;
   signingContainer_->customDialogRequest(bs::signer::ui::DialogType::ActivateAutoSign, data);
}

bool AutoSQProvider::autoSQAvailable()
{
   return signingContainer_ && !signingContainer_->isOffline()
      && walletsManager_ && walletsManager_->isReadyForTrading()
      && celerClient_->IsConnected();
}

bool AutoSQProvider::aqLoaded() const
{
   return aqLoaded_;
}

void AutoSQProvider::onAutoSignStateChanged(const std::string &walletId, bool active)
{
   autoSignState_ = active;
   emit autoSignStateChanged(walletId, active);
}

void AutoSQProvider::setAqLoaded(bool loaded)
{
   aqLoaded_ = loaded;
   if (!loaded) {
      aq_->disableAQ();
   }
}

void AutoSQProvider::initAQ(const QString &filename)
{
   if (filename.isEmpty()) {
      return;
   }
   aqLoaded_ = false;
   aq_->enableAQ(filename);
}

void AutoSQProvider::deinitAQ()
{
   aq_->disableAQ();
   aqLoaded_ = false;
   emit aqScriptUnLoaded();
}

void AutoSQProvider::onAqScriptLoaded(const QString &filename)
{
   logger_->info("AQ script loaded ({})", filename.toStdString());
   aqLoaded_ = true;

   auto scripts = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
   if (scripts.indexOf(filename) < 0) {
      scripts << filename;
      appSettings_->set(ApplicationSettings::aqScripts, scripts);
   }
   appSettings_->set(ApplicationSettings::lastAqScript, filename);
   emit aqScriptLoaded(filename);
   emit aqHistoryChanged();
}

void AutoSQProvider::onAqScriptFailed(const QString &filename, const QString &error)
{
   logger_->error("AQ script loading failed (): {}", filename.toStdString(), error.toStdString());
   setAqLoaded(false);

   auto scripts = appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
   scripts.removeOne(filename);
   appSettings_->set(ApplicationSettings::aqScripts, scripts);
   appSettings_->reset(ApplicationSettings::lastAqScript);
   emit aqHistoryChanged();
}

void AutoSQProvider::onConnectedToCeler()
{
   emit autoSQAvailabilityChanged();
}

void AutoSQProvider::onDisconnectedFromCeler()
{
   autoQuoter()->disableAQ();
   disableAutoSign();

   emit autoSQAvailabilityChanged();
}

UserScriptRunner *AutoSQProvider::autoQuoter() const
{
    return aq_;
}

bool AutoSQProvider::autoSignState() const
{
    return autoSignState_;
}

void AutoSQProvider::setWalletsManager(std::shared_ptr<bs::sync::WalletsManager> &walletsMgr)
{
   walletsManager_ = walletsMgr;
   aq_->setWalletsManager(walletsMgr);

   connect(walletsMgr.get(), &bs::sync::WalletsManager::walletDeleted, this, &AutoSQProvider::autoSQAvailabilityChanged);
   connect(walletsMgr.get(), &bs::sync::WalletsManager::walletAdded, this, &AutoSQProvider::autoSQAvailabilityChanged);
   connect(walletsMgr.get(), &bs::sync::WalletsManager::walletsReady, this, &AutoSQProvider::autoSQAvailabilityChanged);
   connect(walletsMgr.get(), &bs::sync::WalletsManager::walletsSynchronized, this, &AutoSQProvider::autoSQAvailabilityChanged);
   connect(walletsMgr.get(), &bs::sync::WalletsManager::newWalletAdded, this, &AutoSQProvider::autoSQAvailabilityChanged);
   connect(walletsMgr.get(), &bs::sync::WalletsManager::walletImportFinished, this, &AutoSQProvider::autoSQAvailabilityChanged);

   emit autoSQAvailabilityChanged();
}

QString AutoSQProvider::getDefaultScriptsDir()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
   return QCoreApplication::applicationDirPath() + QStringLiteral("/scripts");
#else
   return QStringLiteral("/usr/share/blocksettle/scripts");
#endif
}

QStringList AutoSQProvider::getAQScripts()
{
   return appSettings_->get<QStringList>(ApplicationSettings::aqScripts);
}

QString AutoSQProvider::getAQLastScript()
{
   return appSettings_->get<QString>(ApplicationSettings::lastAqScript);
}

QString AutoSQProvider::getAQLastDir()
{
   return appSettings_->get<QString>(ApplicationSettings::LastAqDir);
}

void AutoSQProvider::setAQLastDir(const QString &path)
{
   appSettings_->set(ApplicationSettings::LastAqDir, QFileInfo(path).dir().absolutePath());
}
