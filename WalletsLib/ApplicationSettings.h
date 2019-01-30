#ifndef __APPLICATION_SETTINGS_H__
#define __APPLICATION_SETTINGS_H__

#include <atomic>
#include <QStringList>
#include <QString>
#include <QSettings>

#include <bdmenums.h>

#include "ArmorySettings.h"
#include "EncryptUtils.h"
#include "LogManager.h"


class ApplicationSettings : public QObject
{
   Q_OBJECT
public:
   ApplicationSettings(const QString &appName = QLatin1String("BlockSettle Terminal")
      , const QString& rootDir = {});
   ~ApplicationSettings() noexcept = default;

   ApplicationSettings(const ApplicationSettings&) = delete;
   ApplicationSettings& operator = (const ApplicationSettings&) = delete;

   ApplicationSettings(ApplicationSettings&&) = delete;
   ApplicationSettings& operator = (ApplicationSettings&&) = delete;

   bool LoadApplicationSettings(const QStringList& argList);
   QString  ErrorText() const { return errorText_; }

   void     SaveSettings();

   enum Setting {
      initialized,
      runArmoryLocally,
      netType,
      armoryDbIp,
      armoryDbPort,
      armoryPathName,
      pubBridgeHost,
      pubBridgePort,
      pubBridgePubKey,
      authServerHost,
      authServerPort,
      authServerPubKey,
      celerHost,
      celerPort,
      mdServerHost,
      mdServerPort,
      chatServerHost,
      chatServerPort,
      chatServerPubKey,
      chatPrivKey,
      chatPubKey,
      chatDbFile,
      celerUsername,
      signerHost,
      signerPort,
      signerRunMode,
      signerOfflineDir,
      autoSignSpendLimit,
      launchToTray,
      minimizeToTray,
      closeToTray,
      notifyOnTX,
      defaultAuthAddr,
      bsPublicKey,
      logDefault,
      logMessages,
      ccFileName,
      txCacheFileName,
      nbBackupFilesKeep,
      aqScripts,
      lastAqScript,
      dropQN,
      GUI_main_geometry,
      GUI_main_tab,
      Filter_MD_RFQ,
      Filter_MD_RFQ_Portfolio,
      Filter_MD_QN,
      Filter_MD_QN_cnt,
      ChangeLog_Base_Url,
      Binaries_Dl_Url,
      ResetPassword_Url,
      GetAccount_Url,
      GettingStartedGuide_Url,
      WalletFiltering,
      FxRfqLimit,
      XbtRfqLimit,
      PmRfqLimit,
      DisableBlueDotOnTabOfRfqBlotter,
      PriceUpdateInterval,
      ShowQuoted,
      AdvancedTxDialogByDefault,
      TransactionFilter,
      SubscribeToMDOnStart,
      MDLicenseAccepted,
      authPrivKey,
      jwtUsername,
      zmqLocalSignerPubKeyFilePath,
      zmqRemoteSignerPubKey,
      _last
   };

   struct SettingDef {
      QString  path;
      QVariant defVal;
      mutable bool     read;
      mutable QVariant value;

      SettingDef(const QString &_path, const QVariant &_defVal=QVariant())
         : path(_path), defVal(_defVal), read(false) {}
   };

   QVariant get(Setting s, bool getDefaultValue = false) const;
   bool isDefault(Setting s) const;
   template <typename T> T get(Setting s, bool getDefaultValue = false) const;
   void set(Setting s, const QVariant &val, bool toFile=true);
   void reset(Setting s, bool toFile=true);     // Reset setting to default value

   void SetDefaultSettings(bool toFile=false);                   // reset all settings to default

   static int GetDefaultArmoryLocalPort(NetworkType networkType);
   static int GetDefaultArmoryRemotePort(NetworkType networkType);
   QString GetArmoryRemotePort(bool getDefaultValue = false
      , NetworkType networkType = NetworkType::Invalid) const;

   QString GetSettingsPath() const;

   QString  GetHomeDir() const;
   QString  GetBackupDir() const;

   ArmorySettings GetArmorySettings() const;

   std::vector<bs::LogConfig> GetLogsConfig(bool getDefaultValue = false) const;

   unsigned int GetWalletScanIndex(const std::string &id) const;
   void SetWalletScanIndex(const std::string &id, unsigned int index);
   std::vector<std::pair<std::string, unsigned int>> UnfinishedWalletsRescan();

   std::pair<autheid::PrivateKey, autheid::PublicKey> GetAuthKeys();

signals:
   void settingChanged(int setting, QVariant value);

private:
   SocketType  GetArmorySocketType() const;
   QString  GetDBDir() const;
   QString  GetBitcoinBlocksDir() const;

   void SetHomeDir(const QString& path);
   void SetBitcoinsDir(const QString& path);
   void SetDBDir(const QString& path);

   QString GetDefaultHomeDir() const;
   QString GetDefaultBitcoinsDir() const;
   QString GetDefaultDBDir() const;

   QString AppendToWritableDir(const QString &filename) const;
   bs::LogConfig parseLogConfig(const QStringList &) const;
   bs::LogLevel parseLogLevel(QString) const;

private:
   QSettings   settings_;
   std::map<Setting, SettingDef> settingDefs_;
   mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

   QString  errorText_;
   QString  commonRoot_;
   QString  dataDir_;
   QString  bitcoinsDir_;
   QString  dbDir_;

   autheid::PrivateKey  authPrivKey_;
   autheid::PublicKey   authPubKey_;
};

#endif // __APPLICATION_SETTINGS_H__
