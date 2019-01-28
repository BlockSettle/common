#ifndef __WALLET_KEYS_CREATE_NEW_WIDGET_H__
#define __WALLET_KEYS_CREATE_NEW_WIDGET_H__

#include <QWidget>
#include "WalletEncryption.h"
#include "AutheIDClient.h"
#include "QWalletInfo.h"

namespace Ui {
    class WalletKeysCreateNewWidget;
}
class WalletKeyNewWidget;
class ApplicationSettings;

class WalletKeysCreateNewWidget : public QWidget
{
   Q_OBJECT
public:
   enum Flag {
      NoFlag = 0x00,
      HideAuthConnectButton = 0x01,
      HideWidgetContol = 0x02,
      HideGroupboxCaption = 0x04,
      SetPasswordLabelAsNew = 0x08,
      HidePubKeyFingerprint = 0x10
   };
   Q_DECLARE_FLAGS(Flags, Flag)

   WalletKeysCreateNewWidget(QWidget* parent = nullptr);
   ~WalletKeysCreateNewWidget() override;

   void setFlags(Flags flags);
   void init(AutheIDClient::RequestType requestType
             , const bs::hd::WalletInfo &walletInfo
             , const std::shared_ptr<ApplicationSettings>& appSettings
             , const std::shared_ptr<spdlog::logger> &logger);

   void cancel();

   bool isValid() const;
   bs::wallet::QPasswordData passwordData(int keyIndex) const { return pwdData_.at(keyIndex); }
   std::vector<bs::wallet::QPasswordData> keys() const { return pwdData_; }
   bs::wallet::KeyRank keyRank() const { return keyRank_; }

signals:
   void keyChanged();
   void keyCountChanged();
   void failed();
   void keyTypeChanged(bool password);

private slots:
   void onAddClicked();
   void onDelClicked();
   void onPasswordDataChanged(int index, bs::wallet::QPasswordData passwordData);

//   void onKeyChanged(int index, SecureBinaryData);
//   void onKeyTypeChanged(int index, bool password);
//   void onEncKeyChanged(int index, SecureBinaryData);
   void updateKeyRank(int);

private:
   void addKey();

private:
   std::unique_ptr<Ui::WalletKeysCreateNewWidget> ui_;
   //std::string walletId_;
   std::vector<std::unique_ptr<WalletKeyNewWidget>> widgets_;
   std::vector<bs::wallet::QPasswordData> pwdData_;
   bs::wallet::KeyRank keyRank_ = { 0, 0 };
   Flags flags_{NoFlag};
   std::shared_ptr<ApplicationSettings> appSettings_;
   QString username_;
   AutheIDClient::RequestType requestType_{};
   bs::hd::WalletInfo walletInfo_;
   std::shared_ptr<spdlog::logger> logger_;

};

Q_DECLARE_OPERATORS_FOR_FLAGS(WalletKeysCreateNewWidget::Flags)

#endif // __WALLET_KEYS_CREATE_NEW_WIDGET_H__
