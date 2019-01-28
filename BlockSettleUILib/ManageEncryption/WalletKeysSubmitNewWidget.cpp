#include "WalletKeysSubmitNewWidget.h"
#include "ui_WalletKeysSubmitNewWidget.h"
#include <set>
#include <QFrame>
#include <QtConcurrent/QtConcurrentRun>
#include "ApplicationSettings.h"
#include "MobileUtils.h"
#include "WalletKeyNewWidget.h"

using namespace bs::wallet;
using namespace bs::hd;


WalletKeysSubmitNewWidget::WalletKeysSubmitNewWidget(QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::WalletKeysSubmitNewWidget())
   , suspended_(false)
{
   ui_->setupUi(this);

   // currently we dont using m of n
   ui_->groupBox->hide();
   layout()->removeWidget(ui_->groupBox);
}

WalletKeysSubmitNewWidget::~WalletKeysSubmitNewWidget() = default;

void WalletKeysSubmitNewWidget::setFlags(Flags flags)
{
   flags_ = flags;
}

void WalletKeysSubmitNewWidget::init(AutheIDClient::RequestType requestType
                                     , const bs::hd::WalletInfo &walletInfo
                                     , const std::shared_ptr<ApplicationSettings> &appSettings
                                     , const std::shared_ptr<spdlog::logger> &logger
                                     , const QString &prompt)
{
   requestType_ = requestType;
   walletInfo_ = walletInfo;
   logger_ = logger;
   appSettings_ = appSettings;

   qDeleteAll(widgets_.cbegin(), widgets_.cend());
   widgets_.clear();
   pwdData_.clear();

   if (flags_ & HideGroupboxCaption) {
      ui_->groupBox->setTitle(QString());
   }

   if (walletInfo.encTypes().empty()) {
      return;
   }

   bool hasAuth = false;
   for (const auto &encType : walletInfo.encTypes()) {
      if (encType == QEncryptionType::Auth) {
         hasAuth = true;
         break;
      }
   }
   if ((flags_ & HidePubKeyFingerprint) || !hasAuth || true) {
      ui_->labelPubKeyFP->hide();
   }
   else {
      ui_->labelPubKeyFP->show();
      QtConcurrent::run([this] {
         const auto &authKeys = appSettings_->GetAuthKeys();
         const auto &pubKeyFP = autheid::getPublicKeyFingerprint(authKeys.second);
         const auto &sPubKeyFP = QString::fromStdString(autheid::toHexWithSeparators(pubKeyFP));
         QMetaObject::invokeMethod(this, [this, sPubKeyFP] {
            ui_->labelPubKeyFP->setText(sPubKeyFP);
         });
      });
   }

   bool isAuthOnly = true;
   for (auto encType : walletInfo.encTypes()) {
      if (encType != QEncryptionType::Auth) {
         isAuthOnly = false;
      }
   }

   int encKeyIndex = 0;
   if (isAuthOnly) {
      addKey(0, true, prompt);
   }
   else if (walletInfo.encTypes().size() == walletInfo.keyRank().first) {
      for (const auto &encType : walletInfo.encTypes()) {
         const bool isPassword = (encType == QEncryptionType::Password);
         addKey(isPassword ? 0 : encKeyIndex++, true, prompt);
      }
   }
   else {
      if ((walletInfo.encTypes().size() > 1) && (walletInfo.keyRank().first == 1)) {
         addKey(0, false, prompt);
      }
      else {
         if ((walletInfo.encTypes().size() == 1) && (walletInfo.encTypes()[0] == QEncryptionType::Auth)
             && (walletInfo.encKeys().size() == walletInfo.keyRank().first)) {
            for (unsigned int i = 0; i < walletInfo.keyRank().first; ++i) {
               addKey(encKeyIndex++, true, prompt);
            }
         }
         else if ((walletInfo.encTypes().size() == 1) && (walletInfo.encTypes()[0] == QEncryptionType::Password)) {
            for (unsigned int i = 0; i < walletInfo.keyRank().first; ++i) {
               addKey(0, true, prompt);
            }
         }
         else {
            for (unsigned int i = 0; i < walletInfo.keyRank().first; ++i) {
               const bool isPassword = !(encKeyIndex < walletInfo.encKeys().size());
               addKey(isPassword ? 0 : encKeyIndex++, false, prompt);
            }
         }
      }
   }
}

//void WalletKeysSubmitNewWidget::addKey(bool password, const std::vector<SecureBinaryData> &encKeys
//   , int encKeyIndex, bool isFixed, const QString &prompt)
void WalletKeysSubmitNewWidget::addKey(int encKeyIndex, bool isFixed, const QString &prompt)
{
   assert(!walletInfo_.rootId().isEmpty());
   if (!widgets_.empty()) {
      const auto separator = new QFrame(this);
      separator->setFrameShape(QFrame::HLine);
      layout()->addWidget(separator);
   }

   //auto widget = new WalletKeyNewWidget(requestType_, walletId_, pwdData_.size(), password, authKeys, this);
   auto widget = new WalletKeyNewWidget(requestType_, walletInfo_, encKeyIndex, appSettings_, logger_, this);
   widget->setUseType(WalletKeyNewWidget::UseType::RequestAuth);
   widget->hideInWidgetAuthControls();

   widgets_.push_back(widget);
   pwdData_.push_back(QPasswordData());

   //widget->init(appSettings_, QString());

   connect(widget, &WalletKeyNewWidget::passwordDataChanged, this, &WalletKeysSubmitNewWidget::onPasswordDataChanged);
   connect(widget, &WalletKeyNewWidget::failed, this, &WalletKeysSubmitNewWidget::failed);

//   if (flags_ & HideAuthConnectButton) {
//      widget->setHideAuthConnect(true);
//   }
//   if (flags_ & HideAuthCombobox) {
//      widget->setHideAuthCombobox(true);
//   }
//   if (flags_ & AuthProgressBarFixed) {
//      widget->setProgressBarFixed(true);
//   }
//   if (flags_ & AuthIdVisible) {
//      widget->setShowAuthId(true);
//   }
//   if (flags_ & SetPasswordLabelAsOld) {
//      widget->setPasswordLabelAsOld();
//   }
//   if (flags_ & HideAuthEmailLabel) {
//      widget->setHideAuthEmailLabel(true);
//   }
//   if (flags_ & HideAuthControlsOnSignClicked) {
//      widget->setHideAuthControlsOnSignClicked(true);
//   }
//   if (flags_ & HideProgressBar) {
//      widget->setHideProgressBar(true);
//   }
//   if (flags_ & HidePasswordWarning) {
//      widget->setHidePasswordWarning(true);
//   }
   layout()->addWidget(widget);

   emit keyCountChanged();
   //widget->setEncryptionKeys(encKeys, encKeyIndex);
   //widget->setFixedType(isFixed);
   if (isFixed && !suspended_) {
      widget->start();
   }
}

void WalletKeysSubmitNewWidget::setFocus()
{
   if (widgets_.empty()) {
      return;
   }
   widgets_.front()->setFocus();
}

//void WalletKeysSubmitNewWidget::onKeyChanged(int index, SecureBinaryData key)
//{
//   if ((index < 0) || (index >= pwdData_.size())) {
//      return;
//   }
//   pwdData_[index].password = key;
//   isKeyFinal_ = (pwdData_[index].encType == bs::wallet::EncryptionType::Auth);
//   emit keyChanged();
//}

//void WalletKeysSubmitNewWidget::onKeyTypeChanged(int index, bool password)
//{
//   if ((index < 0) || (index >= pwdData_.size())) {
//      return;
//   }
//   pwdData_[index].encType = password ? bs::wallet::EncryptionType::Password : bs::wallet::EncryptionType::Auth;
//   pwdData_[index].password.clear();
//   emit keyChanged();
//}

//void WalletKeysSubmitNewWidget::onEncKeyChanged(int index, SecureBinaryData encKey)
//{
//   if ((index < 0) || (index >= pwdData_.size())) {
//      return;
//   }
//   pwdData_[index].encKey = encKey;
//   emit keyChanged();
//}

void WalletKeysSubmitNewWidget::onPasswordDataChanged(int index, QPasswordData passwordData)
{
   pwdData_[index] = passwordData;
   emit keyChanged();
}


bool WalletKeysSubmitNewWidget::isValid() const
{
   if (pwdData_.empty()) {
      return true;
   }
   std::set<SecureBinaryData> encKeys;
   for (const auto &pwd : pwdData_) {
      if (pwd.password.isNull()) {
         return false;
      }
      if (pwd.encType == bs::wallet::EncryptionType::Auth) {
         if (pwd.encKey.isNull()) {
            return false;
         }
         if (encKeys.find(pwd.encKey) != encKeys.end()) {
            return false;
         }
         encKeys.insert(pwd.encKey);
      }
   }
   return true;
}

void WalletKeysSubmitNewWidget::cancel()
{
   for (const auto &keyWidget : widgets_) {
      keyWidget->cancel();
   }
}

std::string WalletKeysSubmitNewWidget::encKey(int index) const
{
   if (index < 0 || index >= pwdData_.size()) {
      return {};
   }
   return pwdData_[index].encKey.toBinStr();
}

SecureBinaryData WalletKeysSubmitNewWidget::key() const
{
   SecureBinaryData result;
   for (const auto &pwd : pwdData_) {
      result = mergeKeys(result, pwd.password);
   }
   return result;
}

bool WalletKeysSubmitNewWidget::isKeyFinal() const
{
   return isKeyFinal_;
}

void WalletKeysSubmitNewWidget::resume()
{
   suspended_ = false;
   for (const auto &widget : widgets_) {
      widget->start();
   }
}

QPasswordData WalletKeysSubmitNewWidget::passwordData(int keyIndex) const
{
   return pwdData_.at(keyIndex);
}

