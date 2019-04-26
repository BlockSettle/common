#ifndef __SIGN_CONTAINER_H__
#define __SIGN_CONTAINER_H__

#include <memory>
#include <string>

#include <QObject>
#include <QStringList>
#include <QVariant>

#include "CoreWallet.h"
#include "QWalletInfo.h"

#include "SignerDefs.h"
#include "SignerUiDefs.h"
#include "ZMQ_BIP15X_DataConnection.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      namespace hd {
         class Leaf;
         class Wallet;
      }
      class SettlementWallet;
      class Wallet;
   }
}
class ApplicationSettings;
class ConnectionManager;

class SignContainer : public QObject
{
   Q_OBJECT
public:
   enum class OpMode {
      Local = 1,
      Remote,
      // RemoteInproc - should be used for testing only, when you need to have signer and listener
      // running in same process and could not use TCP for any reason
      RemoteInproc,
      LocalInproc
   };
   enum class TXSignMode {
      Full,
      Partial
   };
   using PasswordType = SecureBinaryData;

   SignContainer(const std::shared_ptr<spdlog::logger> &, OpMode opMode);
   ~SignContainer() noexcept = default;

   virtual bool Start() { return true; }
   virtual bool Stop() { return true; }
   virtual bool Connect() { return true; }
   virtual bool Disconnect() { return true; }

   virtual bs::signer::RequestId signTXRequest(const bs::core::wallet::TXSignRequest &
      , bool autoSign = false, TXSignMode mode = TXSignMode::Full, const PasswordType& password = {}
      , bool keepDuplicatedRecipients = false) = 0;
   virtual bs::signer::RequestId signPartialTXRequest(const bs::core::wallet::TXSignRequest &
      , bool autoSign = false, const PasswordType& password = {}) = 0;
   virtual bs::signer::RequestId signPayoutTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::Address &authAddr, const std::string &settlementId, bool autoSign = false
      , const PasswordType& password = {}) = 0;

   virtual bs::signer::RequestId signMultiTXRequest(const bs::core::wallet::TXMultiSignRequest &) = 0;

   virtual void SendPassword(const std::string &walletId, const PasswordType &password,
      bool cancelledByUser) = 0;
   virtual bs::signer::RequestId CancelSignTx(const BinaryData &txId) = 0;

   virtual bs::signer::RequestId SetUserId(const BinaryData &) = 0;
   virtual bs::signer::RequestId createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}) = 0;
   virtual bs::signer::RequestId createHDWallet(const std::string &name, const std::string &desc
      , bool primary, const bs::core::wallet::Seed &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}, bs::wallet::KeyRank keyRank = { 0, 0 }) = 0;
   virtual bs::signer::RequestId DeleteHDRoot(const std::string &rootWalletId) = 0;
   virtual bs::signer::RequestId DeleteHDLeaf(const std::string &leafWalletId) = 0;
   virtual bs::signer::RequestId getDecryptedRootKey(const std::string &walletId, const SecureBinaryData &password = {}) = 0;
   virtual bs::signer::RequestId GetInfo(const std::string &rootWalletId) = 0;
   virtual void setLimits(const std::string &walletId, const SecureBinaryData &password, bool autoSign) = 0;
   virtual void createSettlementWallet(const std::function<void(const std::shared_ptr<bs::sync::SettlementWallet> &)> &) {}
   virtual bs::signer::RequestId customDialogRequest(bs::signer::ui::DialogType signerDialog
      , const QVariantMap &data = QVariantMap()) = 0;

   virtual void syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &) = 0;
   virtual void syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &) = 0;
   virtual void syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &) = 0;
   virtual void syncAddressComment(const std::string &walletId, const bs::Address &, const std::string &) = 0;
   virtual void syncTxComment(const std::string &walletId, const BinaryData &, const std::string &) = 0;
   virtual void syncNewAddress(const std::string &walletId, const std::string &index, AddressEntryType
      , const std::function<void(const bs::Address &)> &) = 0;
   virtual void syncNewAddresses(const std::string &walletId, const std::vector<std::pair<std::string, AddressEntryType>> &
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &, bool persistent = true) = 0;

   const OpMode &opMode() const { return mode_; }
   virtual bool hasUI() const { return false; }
   virtual bool isReady() const { return true; }
   virtual bool isOffline() const { return true; }
   virtual bool isWalletOffline(const std::string &) const { return true; }

   virtual void setTargetDir(const QString& targetDir) {}
   virtual QString targetDir() const { return QString(); }

signals:
   void connected();
   void disconnected();
   void authenticated();
   void connectionError(const QString &err);
   void ready();
   void Error(bs::signer::RequestId id, std::string error);
   void TXSigned(bs::signer::RequestId id, BinaryData signedTX, std::string error, bool cancelledByUser);

   void PasswordRequested(bs::hd::WalletInfo walletInfo, std::string prompt);

   void HDLeafCreated(bs::signer::RequestId id, const std::shared_ptr<bs::sync::hd::Leaf> &);
   void HDWalletCreated(bs::signer::RequestId id, std::shared_ptr<bs::sync::hd::Wallet>);
   void DecryptedRootKey(bs::signer::RequestId id, const SecureBinaryData &privKey, const SecureBinaryData &chainCode
      , std::string walletId);
   void QWalletInfo(unsigned int id, const bs::hd::WalletInfo &);
   void UserIdSet();
   void PasswordChanged(const std::string &walletId, bool success);
   void AutoSignStateChanged(const std::string &walletId, bool active, const std::string &error);
   // Notified from remote/local signer when wallets list is updated
   void walletsListUpdated();

protected:
   std::shared_ptr<spdlog::logger> logger_;
   const OpMode mode_;
};


std::shared_ptr<SignContainer> CreateSigner(const std::shared_ptr<spdlog::logger> &
   , const std::shared_ptr<ApplicationSettings> &
   , SignContainer::OpMode
   , const QString &host
   , const std::shared_ptr<ConnectionManager> & connectionManager
   , const bool& ephemeralDataConnKeys
   , const ZmqBIP15XDataConnection::cbNewKey& inNewKeyCB = nullptr);

bool SignerConnectionExists(const QString &host, const QString &port);


#endif // __SIGN_CONTAINER_H__
