#include "WalletKeysSubmitNewWidget.h"
#include "ui_WalletKeysSubmitNewWidget.h"
#include <set>
#include <QFrame>
#include <QtConcurrent/QtConcurrentRun>
#include "ApplicationSettings.h"
#include "MobileUtils.h"

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
                                     , WalletKeyNewWidget::UseType useType
                                     , const std::shared_ptr<ApplicationSettings> &appSettings
                                     , const std::shared_ptr<spdlog::logger> &logger
                                     , const QString &prompt)
{
   requestType_ = requestType;
   walletInfo_ = walletInfo;
   logger_ = logger;
   appSettings_ = appSettings;
   useType_ = useType;

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


void WalletKeysSubmitNewWidget::addKey(int encKeyIndex, bool isFixed, const QString &prompt)
{
   assert(!walletInfo_.rootId().isEmpty());
   if (!widgets_.empty()) {
      const auto separator = new QFrame(this);
      separator->setFrameShape(QFrame::HLine);
      layout()->addWidget(separator);
   }

   auto widget = new WalletKeyNewWidget(requestType_, walletInfo_, encKeyIndex, appSettings_, logger_, this);
   widget->setUseType(useType_);

   widgets_.push_back(widget);
   pwdData_.push_back(QPasswordData());


   connect(widget, &WalletKeyNewWidget::passwordDataChanged, this, &WalletKeysSubmitNewWidget::onPasswordDataChanged);
   connect(widget, &WalletKeyNewWidget::failed, this, &WalletKeysSubmitNewWidget::failed);

   // propagate focus to next widget
   connect(widget, &WalletKeyNewWidget::returnPressed, this, [this](int keyIndex){
      if (widgets_.size() > keyIndex + 1)
         widgets_.at(keyIndex + 1)->setFocus();
      else
         emit returnPressed();
   });


   layout()->addWidget(widget);

   emit keyCountChanged();

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

