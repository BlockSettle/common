#ifndef __WALLET_KEYS_SUBMIT_NEW_WIDGET_H__
#define __WALLET_KEYS_SUBMIT_NEW_WIDGET_H__

#include <QWidget>
#include "WalletEncryption.h"
#include "AutheIDClient.h"
#include "QWalletInfo.h"
#include "WalletKeyNewWidget.h"

namespace Ui {
    class WalletKeysSubmitNewWidget;
}
class WalletKeyNewWidget;
class ApplicationSettings;

class WalletKeysSubmitNewWidget : public QWidget
{
   Q_OBJECT
public:
   enum Flag {
      NoFlag = 0x00,
      HideAuthConnectButton = 0x01,
      HideAuthCombobox = 0x02,
      HideGroupboxCaption = 0x04,
      AuthProgressBarFixed = 0x08,
      AuthIdVisible = 0x10,
      SetPasswordLabelAsOld = 0x20,
      HideAuthEmailLabel = 0x40,
      HideAuthControlsOnSignClicked = 0x80,
      HidePubKeyFingerprint = 0x100,
      HideProgressBar = 0x200,
      HidePasswordWarning = 0x400
   };
   Q_DECLARE_FLAGS(Flags, Flag)

   WalletKeysSubmitNewWidget(QWidget* parent = nullptr);
   ~WalletKeysSubmitNewWidget() override;

   void setFlags(Flags flags);

   void init(AutheIDClient::RequestType requestType
             , const bs::hd::WalletInfo &walletInfo
             , WalletKeyNewWidget::UseType useType
             , const std::shared_ptr<ApplicationSettings> &appSettings
             , const std::shared_ptr<spdlog::logger> &logger
             , const QString &prompt = QString());

   void cancel();

   bool isValid() const;
   SecureBinaryData key() const;
   bool isKeyFinal() const;

   void suspend() { suspended_ = true; }
   void resume();

   bs::wallet::QPasswordData passwordData(int keyIndex) const;

signals:
   void keyChanged();
   void keyCountChanged();
   void failed();
   void returnPressed();

public slots:
   void setFocus();
   void onPasswordDataChanged(int index, bs::wallet::QPasswordData passwordData);

private:
   void addKey(int encKeyIndex, const QString &prompt = QString());


private:
   std::unique_ptr<Ui::WalletKeysSubmitNewWidget> ui_;
   std::vector<WalletKeyNewWidget *> widgets_;
   std::vector<bs::wallet::QPasswordData> pwdData_;
   std::atomic_bool suspended_;
   Flags flags_{NoFlag};
   std::shared_ptr<ApplicationSettings> appSettings_;
   AutheIDClient::RequestType requestType_{};
   bool isKeyFinal_{false};
   bs::hd::WalletInfo walletInfo_;
   std::shared_ptr<spdlog::logger> logger_;
   WalletKeyNewWidget::UseType useType_;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(WalletKeysSubmitNewWidget::Flags)

#endif // __WALLET_KEYS_SUBMIT_NEW_WIDGET_H__
