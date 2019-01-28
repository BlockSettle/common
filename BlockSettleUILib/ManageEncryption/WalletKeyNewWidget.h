#ifndef __WALLET_KEY_NEW_WIDGET_H__
#define __WALLET_KEY_NEW_WIDGET_H__

#include <QTimer>
#include <QWidget>
#include "EncryptUtils.h"
#include "EncryptionUtils.h"
#include "AutheIDClient.h"
#include "QWalletInfo.h"

namespace Ui {
    class WalletKeyNewWidget;
}
class QPropertyAnimation;
class AutheIDClient;
class ApplicationSettings;

class WalletKeyNewWidget : public QWidget
{
   Q_OBJECT
public:
//   WalletKeyNewWidget(AutheIDClient::RequestType requestType, const std::string &walletId
//      , int index, bool password
//      , const std::pair<autheid::PrivateKey, autheid::PublicKey> &
//      , QWidget* parent = nullptr);

   WalletKeyNewWidget(AutheIDClient::RequestType requestType
                      , const bs::hd::WalletInfo &walletInfo
                      , int keyIndex
                      , const std::shared_ptr<ApplicationSettings>& appSettings
                      , const std::shared_ptr<spdlog::logger> &logger
                      , QWidget* parent = nullptr);

   enum class UseType {
      RequestAuth,
      ChangeAuth
   };
   Q_ENUMS(UseType)

   ~WalletKeyNewWidget() override;

   //void init(const std::shared_ptr<ApplicationSettings>& appSettings, const QString& username);
   void cancel();
   void start();

   //void setEncryptionKeys(const std::vector<SecureBinaryData> &encKeys, int index = 0);
   //void setFixedType(bool on = true);
   void setFocus();

//   void setHideAuthConnect(bool value);
//   void setHideAuthCombobox(bool value);
//   void setProgressBarFixed(bool value);
//   void setShowAuthId(bool value);
//   void setShowAuthIdLabel(bool value);
//   void setPasswordLabelAsNew();
//   void setPasswordLabelAsOld();
//   void setHideAuthEmailLabel(bool value);
//   void setHidePasswordWarning(bool value);
//   void setHideAuthControlsOnSignClicked(bool value);
//   void setHideProgressBar(bool value);

   void setUseType(UseType useType);

   // initially WalletKeyWidget designed to embed it to another widgets, not for using as popup dialog
   // this function enables possibility to show another dialog for eid signing
   // it just hides all active controls - sigining button and progress bar
   void hideInWidgetAuthControls();

//   bs::wallet::QPasswordData passwordData() const;
//   void setPasswordData(const bs::wallet::QPasswordData &passwordData);

signals:
   // emitted when password entered or eid auth recieved
   void passwordDataChanged(int keyIndex, const bs::wallet::QPasswordData &passwordData);

   // Signals that Auth was denied or timed out
   void failed();

private slots:
   void onTypeChanged();
   void onPasswordChanged();
   void onAuthIdChanged(const QString &);
   void onAuthSignClicked();
   void onAuthSucceeded(const std::string &deviceId, const SecureBinaryData &password);
   void onAuthFailed(const QString &text);
   void onAuthStatusUpdated(const QString &status);
   void onTimer();

private:
   void stop();
   QPropertyAnimation* startAuthAnimation(bool success);

private:
   std::shared_ptr<ApplicationSettings> appSettings_;

   std::unique_ptr<Ui::WalletKeyNewWidget> ui_;
   //std::string walletId_;
   int         keyIndex_;
   //bool        isPassword_;
   bool        authRunning_ = false;
   //bool        encryptionKeysSet_ = false;

   QTimer      timer_;
   float       timeLeft_;
   AutheIDClient *autheIDClient_{};

//   bool        hideAuthConnect_ = false;
//   bool        hideAuthCombobox_ = false;
//   bool        progressBarFixed_ = false;
//   bool        showAuthId_ = false;
//   bool        hideAuthEmailLabel_ = false;
//   bool        hideAuthControlsOnSignClicked_ = false;
//   bool        hideProgressBar_ = false;
//   bool        hidePasswordWarning_ = false;
   AutheIDClient::RequestType requestType_{};
   std::vector<std::string> knownDeviceIds_; // will contain only device id for key with index keyIndex

   bs::hd::WalletInfo walletInfo_;
   bs::wallet::QPasswordData passwordData_;
   std::shared_ptr<spdlog::logger> logger_;
};

#endif // __WALLET_KEY_NEW_WIDGET_H__
