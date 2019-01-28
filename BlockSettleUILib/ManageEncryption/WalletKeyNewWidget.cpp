#include "WalletKeyNewWidget.h"

#include "ui_WalletKeyNewWidget.h"
#include <QComboBox>
#include <QGraphicsColorizeEffect>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadioButton>
#include <spdlog/spdlog.h>
#include "ApplicationSettings.h"
#include "AutheIDClient.h"
#include <QDebug>

using namespace bs::wallet;
using namespace bs::hd;

namespace
{

const int kAnimationDurationMs = 500;

const QColor kSuccessColor = Qt::green;
const QColor kFailColor = Qt::red;

}


//WalletKeyNewWidget::WalletKeyNewWidget(AutheIDClient::RequestType requestType, const std::string &walletId
//   , int index, bool password, const std::pair<autheid::PrivateKey, autheid::PublicKey> &authKeys
//   , QWidget* parent)
//   : QWidget(parent)
//   , ui_(new Ui::WalletKeyNewWidget())
//   , walletId_(walletId), index_(index)
//   , password_(password)
////   , prompt_(prompt)
//   , autheIDClient_(new AutheIDClient(spdlog::get(""), authKeys, this))
//   , requestType_(requestType)
//{
//   qDebug() << "WalletKeyNewWidget::WalletKeyNewWidget";

//   ui_->setupUi(this);
//   ui_->radioButtonPassword->setChecked(password);
//   ui_->radioButtonAuth->setChecked(!password);
//   ui_->pushButtonAuth->setEnabled(false);
//   ui_->progressBar->hide();
//   ui_->progressBar->setValue(0);

//   onTypeChanged();

//   connect(ui_->radioButtonPassword, &QRadioButton::clicked, this, &WalletKeyNewWidget::onTypeChanged);
//   connect(ui_->radioButtonAuth, &QRadioButton::clicked, this, &WalletKeyNewWidget::onTypeChanged);
//   connect(ui_->lineEditPassword, &QLineEdit::textChanged, this, &WalletKeyNewWidget::onPasswordChanged);
//   connect(ui_->lineEditPasswordConfirm, &QLineEdit::textChanged, this, &WalletKeyNewWidget::onPasswordChanged);
//   connect(ui_->comboBoxAuthId, &QComboBox::editTextChanged, this, &WalletKeyNewWidget::onAuthIdChanged);
//   connect(ui_->comboBoxAuthId, SIGNAL(currentIndexChanged(QString)), this, SLOT(onAuthIdChanged(QString)));
//   connect(ui_->pushButtonAuth, &QPushButton::clicked, this, &WalletKeyNewWidget::onAuthSignClicked);

//   connect(autheIDClient_, &AutheIDClient::succeeded, this, &WalletKeyNewWidget::onAuthSucceeded);
//   connect(autheIDClient_, &AutheIDClient::failed, this, &WalletKeyNewWidget::onAuthFailed);

//   timer_.setInterval(500);
//   connect(&timer_, &QTimer::timeout, this, &WalletKeyNewWidget::onTimer);
//}

WalletKeyNewWidget::WalletKeyNewWidget(AutheIDClient::RequestType requestType
                                       , const bs::hd::WalletInfo &walletInfo
                                       , int keyIndex
                                       , const std::shared_ptr<ApplicationSettings> &appSettings
                                       , const std::shared_ptr<spdlog::logger> &logger
                                       , QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::WalletKeyNewWidget())
   , requestType_(requestType)
   , walletInfo_(walletInfo)
   , keyIndex_(keyIndex)
   , appSettings_(appSettings)
   , logger_(logger)

{
   ui_->setupUi(this);

   // Default view
   //setUseType(UseType::RequestAuth);

   // todo: dont needed anymore?
   ui_->labelAuthId->setMaximumHeight(0);

   qDebug() << "WalletKeyNewWidget::WalletKeyNewWidget";

   passwordData_.q_setEncType(QEncryptionType::Unencrypted);
   for (int i = 0; i < walletInfo.encKeys().size(); ++i) {
      if (keyIndex == i) {
         const auto &encKey = walletInfo.encKeys().at(i);
         auto deviceInfo = AutheIDClient::getDeviceInfo(encKey.toStdString());
         if (!deviceInfo.deviceId.empty()) {
            knownDeviceIds_.push_back(deviceInfo.deviceId);
         }
         {
            ui_->comboBoxAuthId->addItem(QString::fromStdString(deviceInfo.userId));
         }
         if (deviceInfo.userId.empty()) {
            passwordData_.encType = EncryptionType::Password;
         }
         else {
            passwordData_.encType = EncryptionType::Auth;
         }
      }

   }
   if ((keyIndex >= 0) && (keyIndex < walletInfo.encKeys().size())) {
      ui_->comboBoxAuthId->setCurrentIndex(keyIndex);
   }


   ui_->radioButtonPassword->setChecked(passwordData_.encType == EncryptionType::Password);
   ui_->radioButtonAuth->setChecked(passwordData_.encType == EncryptionType::Auth);
   //ui_->pushButtonAuth->setEnabled(false);
   ui_->progressBar->hide();
   ui_->progressBar->setValue(0);

   onTypeChanged();

   connect(ui_->radioButtonPassword, &QRadioButton::clicked, this, &WalletKeyNewWidget::onTypeChanged);
   connect(ui_->radioButtonAuth, &QRadioButton::clicked, this, &WalletKeyNewWidget::onTypeChanged);
   connect(ui_->lineEditPassword, &QLineEdit::textChanged, this, &WalletKeyNewWidget::onPasswordChanged);
   connect(ui_->lineEditPasswordConfirm, &QLineEdit::textChanged, this, &WalletKeyNewWidget::onPasswordChanged);
   connect(ui_->comboBoxAuthId, &QComboBox::editTextChanged, this, &WalletKeyNewWidget::onAuthIdChanged);
   connect(ui_->comboBoxAuthId, SIGNAL(currentIndexChanged(QString)), this, SLOT(onAuthIdChanged(QString)));
   connect(ui_->pushButtonAuth, &QPushButton::clicked, this, &WalletKeyNewWidget::onAuthSignClicked);

   timer_.setInterval(500);
   connect(&timer_, &QTimer::timeout, this, &WalletKeyNewWidget::onTimer);
}


//void WalletKeyNewWidget::init(const std::shared_ptr<ApplicationSettings> &appSettings, const QString& username)
//{
//   qDebug() << "WalletKeyNewWidget::init";

//   const auto &serverPubKey = appSettings->get<std::string>(ApplicationSettings::authServerPubKey);
//   const auto &serverHost = appSettings->get<std::string>(ApplicationSettings::authServerHost);
//   const auto &serverPort = appSettings->get<std::string>(ApplicationSettings::authServerPort);

//   try {
//      autheIDClient_->connect(serverPubKey, serverHost, serverPort);
//   }
//   catch (const std::exception &) {
//      ui_->pushButtonAuth->setEnabled(false);
//   }

//   ui_->comboBoxAuthId->setEditText(username);
//}

WalletKeyNewWidget::~WalletKeyNewWidget() = default;

void WalletKeyNewWidget::onTypeChanged()
{
   if (ui_->radioButtonPassword->isChecked()) {
      passwordData_.encType = EncryptionType::Password;
      ui_->stackedWidget->setCurrentWidget(ui_->pagePassword);
      ui_->pageEid->setMaximumHeight(0);
      ui_->pagePassword->setMaximumHeight(SHRT_MAX);
   }
   else {
      passwordData_.encType = EncryptionType::Auth;
      ui_->stackedWidget->setCurrentWidget(ui_->pageEid);
      ui_->pageEid->setMaximumHeight(SHRT_MAX);
      ui_->pagePassword->setMaximumHeight(0);
   }
   emit passwordDataChanged(keyIndex_, passwordData_);

   //   ui_->labelPassword->setVisible(passwordData_.encType == EncryptionType::Password);
//   ui_->lineEditPassword->setVisible(passwordData_.encType == EncryptionType::Password);
//   ui_->labelPasswordConfirm->setVisible(passwordData_.encType == EncryptionType::Password);
//   ui_->lineEditPasswordConfirm->setVisible(passwordData_.encType == EncryptionType::Password);
//   ui_->labelPasswordInfo->setVisible(passwordData_.encType == EncryptionType::Password);
//   ui_->labelPasswordWarning->setVisible(passwordData_.encType == EncryptionType::Password && !hidePasswordWarning_);

//   ui_->labelAuthId->setVisible(passwordData_.encType == EncryptionType::Auth && showAuthId_);
//   ui_->widgetAuthLayout->setVisible(passwordData_.encType == EncryptionType::Auth);
   
//   ui_->pushButtonAuth->setVisible(!hideAuthConnect_);
//   ui_->comboBoxAuthId->setVisible(!hideAuthCombobox_);
//   ui_->widgetSpacing->setVisible(!progressBarFixed_);
//   ui_->labelAuthInfo->setVisible(!hideAuthEmailLabel_ && passwordData_.encType == EncryptionType::Auth && !hideAuthCombobox_);
}

void WalletKeyNewWidget::onPasswordChanged()
{
   ui_->labelPassword->setEnabled(false);
   if ((ui_->lineEditPassword->text() == ui_->lineEditPasswordConfirm->text()) || !ui_->lineEditPasswordConfirm->isVisible()) {
      if (!ui_->lineEditPassword->text().isEmpty()) {
         ui_->labelPassword->setEnabled(true);
      }
      passwordData_.q_setTextPassword(ui_->lineEditPassword->text());
      emit passwordDataChanged(keyIndex_, passwordData_);
   }
   else {
      emit passwordDataChanged(keyIndex_, QPasswordData());
   }

   QString msg;
   bool bGreen = false;
   if (ui_->lineEditPasswordConfirm->isVisible()) {
      auto pwd = ui_->lineEditPassword->text();
      auto pwdCf = ui_->lineEditPasswordConfirm->text();
      if (!pwd.isEmpty() && !pwdCf.isEmpty()) {
         if (pwd == pwdCf) {
            if (pwd.length() < 6) {
               msg = tr("Passwords match but are too short!");
            }
            else {
               msg = tr("Passwords match!");
               bGreen = true;
            }
         }
         else if (pwd.length() < pwdCf.length()) {
            msg = tr("Confirmation Password is too long!");
         }
         else {
            msg = tr("Passwords do not match!");
         }
      }
   }
   ui_->labelPasswordInfo->setStyleSheet(tr("QLabel { color : %1; }").arg(bGreen ? tr("#38C673") : tr("#EE2249")));
   ui_->labelPasswordInfo->setText(msg);
}

void WalletKeyNewWidget::onAuthIdChanged(const QString &text)
{
   ui_->labelAuthId->setText(text);
   passwordData_.q_setEncKey(text);
   emit passwordDataChanged(keyIndex_, passwordData_);
   ui_->pushButtonAuth->setEnabled(!text.isEmpty());
}

void WalletKeyNewWidget::onAuthSignClicked()
{
   if (authRunning_) {
      cancel();
      ui_->pushButtonAuth->setText(tr("Sign with Auth"));
      return;
   }
   else {
      autheIDClient_ = new AutheIDClient(logger_, appSettings_->GetAuthKeys(), this);
      const auto &serverPubKey = appSettings_->get<std::string>(ApplicationSettings::authServerPubKey);
      const auto &serverHost = appSettings_->get<std::string>(ApplicationSettings::authServerHost);
      const auto &serverPort = appSettings_->get<std::string>(ApplicationSettings::authServerPort);

      connect(autheIDClient_, &AutheIDClient::succeeded, this, &WalletKeyNewWidget::onAuthSucceeded);
      connect(autheIDClient_, &AutheIDClient::failed, this, &WalletKeyNewWidget::onAuthFailed);

      try {
         autheIDClient_->connect(serverPubKey, serverHost, serverPort);
      }
      catch (const std::exception &e) {
         // TODO display error
         ui_->pushButtonAuth->setEnabled(false);
      }

      //ui_->comboBoxAuthId->setEditText(username);
   }
   timeLeft_ = 120;
   ui_->progressBar->setMaximum(timeLeft_ * 2);
   //if (!hideProgressBar_) {
      ui_->progressBar->show();
   //}
   timer_.start();
   authRunning_ = true;

   autheIDClient_->start(requestType_, ui_->comboBoxAuthId->currentText().toStdString()
      , walletInfo_.rootId().toStdString(), knownDeviceIds_);
   ui_->pushButtonAuth->setText(tr("Cancel Auth request"));
   ui_->comboBoxAuthId->setEnabled(false);

   //if (hideAuthControlsOnSignClicked_) {
      ui_->widgetAuthLayout->hide();
   //}
}

void WalletKeyNewWidget::onAuthSucceeded(const std::string &encKey, const SecureBinaryData &password)
{
   stop();
   ui_->pushButtonAuth->setText(tr("Successfully signed"));
   ui_->pushButtonAuth->setEnabled(false);
   ui_->widgetAuthLayout->show();

   QPropertyAnimation *a = startAuthAnimation(true);
   connect(a, &QPropertyAnimation::finished, [this, encKey, password]() {
      passwordData_.encKey = encKey;
      passwordData_.password = password;
      emit passwordDataChanged(keyIndex_, passwordData_);

      //emit encKeyChanged(keyIndex_, encKey);
      //emit keyChanged(keyIndex_, password);
   });
}

void WalletKeyNewWidget::onAuthFailed(const QString &text)
{
   
   stop();
   ui_->pushButtonAuth->setEnabled(true);
   ui_->pushButtonAuth->setText(tr("Auth failed: %1 - retry").arg(text));
   ui_->widgetAuthLayout->show();
   
   QPropertyAnimation *a = startAuthAnimation(false);
   connect(a, &QPropertyAnimation::finished, [this]() {
      emit failed();
   });
}

void WalletKeyNewWidget::onAuthStatusUpdated(const QString &)
{}

void WalletKeyNewWidget::onTimer()
{
   timeLeft_ -= 0.5;
   if (timeLeft_ <= 0) {
      onAuthFailed(tr("Timeout"));
   }
   else {
      //ui_->progressBar->setFormat(tr("%1 seconds left").arg((int)timeLeft_));
      ui_->progressBar->setValue(timeLeft_ * 2);
      ui_->labelProgress->setText(tr("%1 seconds left").arg((int)timeLeft_));
      ui_->progressBar->repaint();
   }
}

void WalletKeyNewWidget::stop()
{
   ui_->lineEditPassword->clear();
   ui_->lineEditPasswordConfirm->clear();
   authRunning_ = false;
   if (autheIDClient_) {
      autheIDClient_->deleteLater();
      autheIDClient_ = nullptr;
   }
   timer_.stop();
   //if (!progressBarFixed_) {
      ui_->progressBar->hide();
   //}
   ui_->comboBoxAuthId->setEnabled(true);
   ui_->widgetAuthLayout->show();
}

void WalletKeyNewWidget::cancel()
{
   if (passwordData_.encType == EncryptionType::Auth) {
      if (autheIDClient_) {
         autheIDClient_->cancel();
      }
      stop();
   }
}

void WalletKeyNewWidget::start()
{
   if (passwordData_.encType == EncryptionType::Auth && !authRunning_ && !ui_->comboBoxAuthId->currentText().isEmpty()) {
      onAuthSignClicked();
      ui_->progressBar->setValue(0);
      //ui_->pushButtonAuth->setEnabled(true);
   }
}

//void WalletKeyNewWidget::setEncryptionKeys(const std::vector<SecureBinaryData> &encKeys, int index)
//{
//   encryptionKeysSet_ = true;
//   if (isPassword_) {
//      ui_->labelPasswordConfirm->hide();
//      ui_->lineEditPasswordConfirm->hide();
//      ui_->labelPasswordWarning->hide();
//   }
//   if (encKeys.empty()) {
//      return;
//   }
//   ui_->comboBoxAuthId->setEditable(false);

//   knownDeviceIds_.clear();
//   for (const auto &encKey : encKeys) {
//      auto deviceInfo = AutheIDClient::getDeviceInfo(encKey.toBinStr());
//      if (!deviceInfo.deviceId.empty()) {
//         knownDeviceIds_.push_back(deviceInfo.deviceId);
//      }
//      ui_->comboBoxAuthId->addItem(QString::fromStdString(deviceInfo.userId));
//   }
//   if ((index >= 0) && (index < encKeys.size())) {
//      ui_->comboBoxAuthId->setCurrentIndex(index);
//   }
//}

//void WalletKeyNewWidget::setFixedType(bool on)
//{
//   if (ui_->comboBoxAuthId->count() > 0) {
//      //ui_->comboBoxAuthId->setEnabled(!on);
//      ui_->comboBoxAuthId->setEditable(!on);
//   }
//   ui_->radioButtonPassword->setVisible(!on);
//   ui_->radioButtonAuth->setVisible(!on);
//}

void WalletKeyNewWidget::setFocus()
{
   if (passwordData_.encType == EncryptionType::Password) {
      ui_->lineEditPassword->setFocus();
   }
   else {
      ui_->comboBoxAuthId->setFocus();
   }
}

//void WalletKeyNewWidget::setHideAuthConnect(bool value)
//{
//   hideAuthConnect_ = value;
//   onTypeChanged();
//}

//void WalletKeyNewWidget::setHideAuthCombobox(bool value)
//{
//   hideAuthCombobox_ = value;
//   onTypeChanged();
//}

//void WalletKeyNewWidget::setProgressBarFixed(bool value)
//{
//   progressBarFixed_ = value;
//   onTypeChanged();
//}

//void WalletKeyNewWidget::setShowAuthId(bool value)
//{
//   showAuthId_ = value;
//   onTypeChanged();
//}

//void WalletKeyNewWidget::setPasswordLabelAsNew()
//{
//   ui_->labelPassword->setText(tr("New Password"));
//}

//void WalletKeyNewWidget::setPasswordLabelAsOld()
//{
//   ui_->labelPassword->setText(tr("Old Password"));
//}

//void WalletKeyNewWidget::setHideAuthEmailLabel(bool value)
//{
//   hideAuthEmailLabel_ = value;
//   onTypeChanged();
//}

//void WalletKeyNewWidget::setHidePasswordWarning(bool value)
//{
//   hidePasswordWarning_ = value;
//   onTypeChanged();
//}

//void WalletKeyNewWidget::setHideAuthControlsOnSignClicked(bool value)
//{
//   hideAuthControlsOnSignClicked_ = value;
//}

//void WalletKeyNewWidget::setHideProgressBar(bool value)
//{
//   hideProgressBar_ = value;
//}

void WalletKeyNewWidget::setUseType(WalletKeyNewWidget::UseType useType)
{
   ui_->labelPassword->setText(useType == UseType::ChangeAuth ? tr("New Password") : tr("Password"));
   ui_->comboBoxAuthId->setEditable(useType == UseType::ChangeAuth);

   ui_->widgetEncTypeSelect->setVisible(useType == UseType::ChangeAuth);
   ui_->labelPasswordConfirm->setVisible(useType == UseType::ChangeAuth);
   ui_->lineEditPasswordConfirm->setVisible(useType == UseType::ChangeAuth);
   ui_->labelPasswordInfo->setVisible(useType == UseType::ChangeAuth);
   ui_->labelPasswordWarning->setVisible(useType == UseType::ChangeAuth);


//   ui_->labelPassword->setVisible(useType == UseType::ChangeAuth);
//   ui_->lineEditPassword->setVisible(useType == UseType::ChangeAuth);

   if (useType == UseType::RequestAuth) {
      ui_->widgetEncTypeSelect->setMaximumHeight(0);

      //layout()->removeWidget(ui_->widgetEncTypeSelect);
      //layout()->removeWidget(ui_->stackedWidget);
//      ui_->pagePassword->layout()->removeWidget(ui_->labelPasswordConfirm);
//      ui_->pagePassword->layout()->removeWidget(ui_->lineEditPasswordConfirm);
//      ui_->pagePassword->layout()->removeWidget(ui_->labelPasswordInfo);
//      ui_->pagePassword->layout()->removeWidget(ui_->labelPasswordWarning);


//      ui_->widgetEncTypeSelect->deleteLater();
//      ui_->labelPasswordConfirm->deleteLater();
//      ui_->lineEditPasswordConfirm->deleteLater();
//      ui_->labelPasswordInfo->deleteLater();
//      ui_->labelPasswordWarning->deleteLater();
//      ui_->widgetEncTypeSelect->deleteLater();
   }
}

void WalletKeyNewWidget::hideInWidgetAuthControls()
{
   ui_->pushButtonAuth->setMaximumHeight(0);
   ui_->progressBar->setMaximumHeight(0);

   ui_->pushButtonAuth->hide();
   ui_->progressBar->hide();
}

QPropertyAnimation* WalletKeyNewWidget::startAuthAnimation(bool success)
{
   QGraphicsColorizeEffect *eff = new QGraphicsColorizeEffect(this);
   eff->setColor(success ? kSuccessColor : kFailColor);
   ui_->labelAuthId->setGraphicsEffect(eff);

   QPropertyAnimation *a = new QPropertyAnimation(eff, "strength");
   a->setDuration(kAnimationDurationMs);
   a->setStartValue(0.0);
   a->setEndValue(1.0);
   a->setEasingCurve(QEasingCurve::Linear);
   a->start(QPropertyAnimation::DeleteWhenStopped);

   return a;
}

//bs::wallet::QPasswordData WalletKeyNewWidget::passwordData() const
//{
//   return passwordData_;
//}

//void WalletKeyNewWidget::setPasswordData(const bs::wallet::QPasswordData &passwordData)
//{
//   passwordData_ = passwordData;
//}
