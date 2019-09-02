#ifndef AUTOSQPROVIDER_H
#define AUTOSQPROVIDER_H

#include <QObject>

class ApplicationSettings;
class SignContainer;
class UserScriptRunner;
class AssetManager;
class QuoteProvider;
class MarketDataProvider;

namespace bs {
   class DealerUtxoResAdapter;
   namespace sync {
      namespace hd {
         class Leaf;
      }
      class Wallet;
      class WalletsManager;
   }
}

namespace spdlog {
   class logger;
}

// Auto quoting and signing provider
class AutoSQProvider : public QObject
{
   Q_OBJECT
public:
   explicit AutoSQProvider(const std::shared_ptr<spdlog::logger> logger
       , const std::shared_ptr<AssetManager>& assetManager
       , const std::shared_ptr<QuoteProvider>& quoteProvider
       , const std::shared_ptr<ApplicationSettings> &appSettings
       , const std::shared_ptr<bs::DealerUtxoResAdapter> &dealerUtxoAdapter
       , const std::shared_ptr<SignContainer> &container
       , const std::shared_ptr<MarketDataProvider> &mdProvider
       , QObject *parent = nullptr);

   // auto quote
   bool aqLoaded() const;
   void setAqLoaded(bool loaded);

   void initAQ(const QString &filename);
   void deinitAQ();

   void disableAutoSign();
   void tryEnableAutoSign();

   QString getDefaultScriptsDir();

   QStringList getAQScripts();
   QString getAQLastScript();

   QString getAQLastDir();
   void setAQLastDir(const QString &path);

   // auto sign
   bool autoSignState() const;

   //
   void setWalletManager(std::shared_ptr<bs::sync::WalletsManager> w);

signals:
   void aqScriptLoaded(const QString &filename);
   void aqScriptUnLoaded();
   void aqHistoryChanged();

   void autoSignAvailabilityChanged(bool available);
   void autoSignStateChanged(const std::string &walletId, bool active);

public slots:
   void onSignerStateUpdated();
   //void onAutoSignActivated();
   void onAutoSignStateChanged(const std::string &walletId, bool active);

   void onAqScriptLoaded(const QString &filename);
   void onAqScriptFailed(const QString &filename, const QString &error);
private:
   bool              autoSignState_{false};
   UserScriptRunner *aq_{};
   bool              aqLoaded_{false};
   bool              celerConnected_{false};
   bool              newLoaded_{false};

   std::shared_ptr<ApplicationSettings>       appSettings_;
   std::shared_ptr<spdlog::logger>            logger_;
   std::shared_ptr<SignContainer>             signingContainer_;
   std::shared_ptr<bs::sync::WalletsManager>  walletsManager_;
};

#endif // AUTOSQPROVIDER_H
