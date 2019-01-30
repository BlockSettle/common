#include "WalletKeysCreateNewWidget.h"
#include "ui_WalletKeysCreateNewWidget.h"
#include <set>
#include <QSpinBox>
#include "ApplicationSettings.h"
#include "MobileUtils.h"


WalletKeysCreateNewWidget::WalletKeysCreateNewWidget(QWidget* parent)
   : QWidget(parent)
   , ui_(new Ui::WalletKeysCreateNewWidget())
{
   ui_->setupUi(this);
   ui_->pushButtonDelKey->setEnabled(false);
   ui_->labelRankN->clear();

   // currently we dont using m of n
   ui_->groupBox->hide();
   layout()->removeWidget(ui_->groupBox);

   connect(ui_->pushButtonAddKey, &QPushButton::clicked, this, &WalletKeysCreateNewWidget::onAddClicked);
   connect(ui_->pushButtonDelKey, &QPushButton::clicked, this, &WalletKeysCreateNewWidget::onDelClicked);
   connect(ui_->spinBoxRankM, SIGNAL(valueChanged(int)), this, SLOT(updateKeyRank(int)));
}

WalletKeysCreateNewWidget::~WalletKeysCreateNewWidget() = default;

void WalletKeysCreateNewWidget::setFlags(Flags flags)
{
   flags_ = flags;
}

void WalletKeysCreateNewWidget::init(AutheIDClient::RequestType requestType
                                     , const bs::hd::WalletInfo &walletInfo
                                     , WalletKeyNewWidget::UseType useType
                                     , const std::shared_ptr<ApplicationSettings>& appSettings
                                     , const std::shared_ptr<spdlog::logger> &logger)
{
   requestType_ = requestType;
   walletInfo_ = walletInfo;
   appSettings_ = appSettings;
   logger_ = logger;
   useType_ = useType;

   widgets_.clear();
   pwdData_.clear();

   if (flags_ & HideGroupboxCaption) {
      ui_->groupBox->setTitle(QString());
   }

   
   addKey();

   if (flags_ & HideWidgetContol) {
      ui_->widgetControl->hide();
   }
}

void WalletKeysCreateNewWidget::addKey()
{
   assert(!walletInfo_.rootId().isEmpty());
   const auto &authKeys = appSettings_->GetAuthKeys();
   auto widget = new WalletKeyNewWidget(requestType_, walletInfo_, widgets_.size(), appSettings_, logger_, this);
   widget->setUseType(useType_);

//   widget->init(appSettings_, username_);
//   if (flags_ & HideAuthConnectButton) {
//      widget->setHideAuthConnect(true);
//   }
//   if (flags_ & SetPasswordLabelAsNew) {
//      widget->setPasswordLabelAsNew();
//   }

   if (flags_ & HidePubKeyFingerprint || true) {
      ui_->labelPubKeyFP->hide();
   }
   else {
      const auto &pubKeyFP = autheid::toHexWithSeparators(autheid::getPublicKeyFingerprint(authKeys.second));
      ui_->labelPubKeyFP->setText(QString::fromStdString(pubKeyFP));
   }

   connect(widget, &WalletKeyNewWidget::passwordDataChanged, this, &WalletKeysCreateNewWidget::onPasswordDataChanged);
   connect(widget, &WalletKeyNewWidget::failed, this, &WalletKeysCreateNewWidget::failed);

   layout()->addWidget(widget);
   ui_->pushButtonDelKey->setEnabled(true);
   widgets_.emplace_back(widget);
   pwdData_.push_back(bs::wallet::QPasswordData());
   ui_->spinBoxRankM->setMaximum(pwdData_.size());
   ui_->spinBoxRankM->setMinimum(1);
   updateKeyRank(0);
   emit keyCountChanged();
}

void WalletKeysCreateNewWidget::onAddClicked()
{
   addKey();
}

void WalletKeysCreateNewWidget::onDelClicked()
{
   if (widgets_.empty()) {
      return;
   }
   widgets_.pop_back();
   pwdData_.resize(widgets_.size());
   if (pwdData_.empty()) {
      ui_->spinBoxRankM->setMinimum(0);
   }
   ui_->spinBoxRankM->setMaximum(pwdData_.size());
   updateKeyRank(0);
   emit keyCountChanged();
   if (widgets_.empty()) {
      ui_->pushButtonDelKey->setEnabled(false);
   }
}

void WalletKeysCreateNewWidget::onPasswordDataChanged(int index, bs::wallet::QPasswordData passwordData)
{
   pwdData_[index] = passwordData;
   emit keyChanged();
}

//void WalletKeysCreateNewWidget::onKeyChanged(int index, SecureBinaryData key)
//{
//   if ((index < 0) || (index >= pwdData_.size())) {
//      return;
//   }
//   pwdData_[index].password = key;
//   emit keyChanged();
//}

//void WalletKeysCreateNewWidget::onKeyTypeChanged(int index, bool password)
//{
//   if ((index < 0) || (index >= pwdData_.size())) {
//      return;
//   }
//   pwdData_[index].encType = password ? bs::wallet::EncryptionType::Password : bs::wallet::EncryptionType::Auth;
//   pwdData_[index].password.clear();
//   emit keyChanged();
//   emit keyTypeChanged(password);
//}

//void WalletKeysCreateNewWidget::onEncKeyChanged(int index, SecureBinaryData encKey)
//{
//   if ((index < 0) || (index >= pwdData_.size())) {
//      return;
//   }
//   pwdData_[index].encKey = encKey;
//   emit keyChanged();
//}

void WalletKeysCreateNewWidget::updateKeyRank(int)
{
   keyRank_.second = pwdData_.size();
   keyRank_.first = ui_->spinBoxRankM->value();
   if (pwdData_.empty()) {
      ui_->labelRankN->clear();
   }
   else {
      ui_->labelRankN->setText(QString::number(keyRank_.second));
   }
}

bool WalletKeysCreateNewWidget::isValid() const
{
   if (pwdData_.empty()) {
      return false;
   }

   std::set<SecureBinaryData> encKeys;
   for (const auto &pwd : pwdData_) {
      if (pwd.encType == bs::wallet::EncryptionType::Auth) {
         if (pwd.encKey.isNull()) {
            return false;
         }
         if (encKeys.find(pwd.encKey) != encKeys.end()) {
            return false;
         }
         encKeys.insert(pwd.encKey);
      } else if (pwd.password.getSize() < 6) {
         // Password must be at least 6 chars long.
         return false;
      }
   }
   return true;
}

void WalletKeysCreateNewWidget::cancel()
{
   for (const auto &keyWidget : widgets_) {
      keyWidget->cancel();
   }
}
