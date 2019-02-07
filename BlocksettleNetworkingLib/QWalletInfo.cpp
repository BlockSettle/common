#include <QFile>
#include "WalletEncryption.h"
#include "QWalletInfo.h"
#include "AutheIDClient.h"
#include "WalletBackupFile.h"

using namespace bs::hd;
using namespace bs::wallet;
using namespace Blocksettle::Communication;

WalletInfo::WalletInfo(const QString &rootId, const std::vector<EncryptionType> &encTypes
                       , const std::vector<SecureBinaryData> &encKeys, const KeyRank &keyRank)
{
   rootId_ = rootId;
   keyRank_ = keyRank;
   setEncKeys(encKeys);
   setEncTypes(encTypes);
}

WalletInfo::WalletInfo(const headless::GetHDWalletInfoResponse &response)
{
   rootId_ = QString::fromStdString(response.rootwalletid());
   for (int i = 0; i < response.enctypes_size(); ++i) {
      encTypes_.push_back(static_cast<bs::wallet::EncryptionType>(response.enctypes(i)));
   }
   for (int i = 0; i < response.enckeys_size(); ++i) {
      encKeys_.push_back(QString::fromStdString(response.enckeys(i)));
   }
   keyRank_ = { response.rankm(), response.rankn() };
}

WalletInfo::WalletInfo(const headless::PasswordRequest &request)
{
   setRootId(request.walletid());
   for (int i = 0; i < request.enctypes_size(); ++i) {
      encTypes_.push_back(static_cast<bs::wallet::EncryptionType>(request.enctypes(i)));
   }
   for (int i = 0; i < request.enckeys_size(); ++i) {
      encKeys_.push_back(QString::fromStdString(request.enckeys(i)));
   }
   keyRank_ = { request.rankm(), 0 };
}

WalletInfo::WalletInfo(std::shared_ptr<bs::hd::Wallet> hdWallet, QObject *parent)
{
   initFromRootWallet(hdWallet);
   initEncKeys(hdWallet);

   connect(hdWallet.get(), &bs::hd::Wallet::metaDataChanged, this, [this, hdWallet](){
      initFromRootWallet(hdWallet);
      initEncKeys(hdWallet);
   });
}

WalletInfo::WalletInfo(std::shared_ptr<bs::Wallet> wallet, std::shared_ptr<bs::hd::Wallet> rootHdWallet, QObject *parent)
{
   initFromWallet(wallet.get(), rootHdWallet->getWalletId());
   initEncKeys(rootHdWallet);

   connect(rootHdWallet.get(), &bs::hd::Wallet::metaDataChanged, this, [this, rootHdWallet](){
      initFromRootWallet(rootHdWallet);
      initEncKeys(rootHdWallet);
   });
}

WalletInfo::WalletInfo(const WalletInfo &other)
   : walletId_(other.walletId_), rootId_(other.rootId_)
   , name_(other.name_), desc_(other.desc_)
   , encKeys_(other.encKeys_), encTypes_(other.encTypes_), keyRank_(other.keyRank_)
{
}

WalletInfo &bs::hd::WalletInfo::WalletInfo::operator =(const WalletInfo &other)
{
   walletId_ = other.walletId_;
   rootId_ = other.rootId_;
   name_ = other.name_;
   desc_ = other.desc_;
   encKeys_ = other.encKeys_;
   encTypes_ = other.encTypes_;
   keyRank_ = other.keyRank_;

   return *this;
}

WalletInfo WalletInfo::fromDigitalBackup(const QString &filename)
{
   bs::hd::WalletInfo walletInfo;

   QFile file(filename);
   if (!file.exists()) return walletInfo;

   if (file.open(QIODevice::ReadOnly)) {
      QByteArray data = file.readAll();
      const auto wdb = WalletBackupFile::Deserialize(std::string(data.data(), data.size()));
      walletInfo.setName(QString::fromStdString(wdb.name));
      walletInfo.setDesc(QString::fromStdString(wdb.name));
   }
   return walletInfo;
}

void WalletInfo::initFromWallet(const bs::Wallet *wallet, const std::string &rootId)
{
   if (!wallet)
      return;

   walletId_ = QString::fromStdString(wallet->GetWalletId());
   rootId_ = QString::fromStdString(rootId);
   name_ = QString::fromStdString(wallet->GetWalletName());
   emit walletChanged();
}

void WalletInfo::initFromRootWallet(const std::shared_ptr<bs::hd::Wallet> &rootWallet)
{
   walletId_ = QString::fromStdString(rootWallet->getWalletId());
   name_ = QString::fromStdString(rootWallet->getName());
   rootId_ = QString::fromStdString(rootWallet->getWalletId());
   keyRank_ = rootWallet->encryptionRank();
   emit walletChanged();
}

void WalletInfo::initEncKeys(const std::shared_ptr<Wallet> &rootWallet)
{
   encKeys_.clear();
   setEncKeys(rootWallet->encryptionKeys());
   setEncTypes(rootWallet->encryptionTypes());
}

void WalletInfo::setDesc(const QString &desc)
{
   if (desc_ == desc)
      return;

   desc_ = desc;
   emit walletChanged();
}

void WalletInfo::setWalletId(const QString &walletId)
{
   if (walletId_ == walletId)
      return;

   walletId_ = walletId;
   emit walletChanged();
}

void WalletInfo::setRootId(const QString &rootId)
{
   if (rootId_ == rootId)
      return;

   rootId_ = rootId;
   emit walletChanged();
}

EncryptionType WalletInfo::encType()
{
   return encTypes_.isEmpty() ? bs::wallet::EncryptionType::Unencrypted : encTypes_.at(0);
}

QString WalletInfo::email() const
{
   if (encKeys_.isEmpty())
      return QString();

   return QString::fromStdString(AutheIDClient::getDeviceInfo(encKeys_.at(0).toStdString()).userId);
}

bs::wallet::KeyRank WalletInfo::keyRank() const
{
   return keyRank_;
}

void WalletInfo::setKeyRank(const bs::wallet::KeyRank &keyRank)
{
   keyRank_ = keyRank;
}

bool WalletInfo::isEidAuthOnly() const
{
   for (auto encType : encTypes()) {
      if (encType != EncryptionType::Auth) {
         return false;
      }
   }
   return true;
}

void WalletInfo::setEncKeys(const std::vector<SecureBinaryData> &encKeys)
{
   encKeys_.clear();
   for (const SecureBinaryData &encKey : encKeys) {
      encKeys_.push_back(QString::fromStdString(encKey.toBinStr()));
   }
   emit walletChanged();
}

void WalletInfo::setEncTypes(const std::vector<EncryptionType> &encTypes)
{
   encTypes_.clear();
   for (const EncryptionType &encType : encTypes) {
      encTypes_.push_back(encType);
   }
   emit walletChanged();
}

void WalletInfo::setPasswordData(const std::vector<PasswordData> &passwordData)
{
   encKeys_.clear();
   encTypes_.clear();

   bool isAuth = false;
   bool isPassword = false;
   for (const PasswordData &pw : passwordData) {
      encKeys_.push_back(QString::fromStdString(pw.encKey.toBinStr()));
      if (pw.encType == EncryptionType::Auth)
         isAuth = true;
      if (pw.encType == EncryptionType::Password)
         isPassword = true;
   }

   if (isAuth)
      encTypes_.append(EncryptionType::Auth);

   if (isPassword)
      encTypes_.append(EncryptionType::Password);

   emit walletChanged();
}

void WalletInfo::setEncKeys(const QList<QString> &encKeys)
{
   encKeys_ = encKeys;
   emit walletChanged();
}

void WalletInfo::setEncTypes(const QList<EncryptionType> &encTypes)
{
   encTypes_ = encTypes;
   emit walletChanged();
}

void WalletInfo::setName(const QString &name)
{
   if (name_ == name)
      return;

   name_ = name;
   emit walletChanged();
}
