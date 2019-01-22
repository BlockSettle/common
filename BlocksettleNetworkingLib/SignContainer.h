#ifndef __SIGN_CONTAINER_H__
#define __SIGN_CONTAINER_H__

#include <memory>
#include <string>

#include <QObject>
#include <QStringList>

#include "HDNode.h"
#include "MetaData.h"

namespace spdlog {
   class logger;
}
namespace bs {
   class SettlementAddressEntry;
   namespace hd {
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
      Offline,
      // RemoteInproc - should be used for testing only, when you need to have signer and listener
      // running in same process and could not use TCP for any reason
      RemoteInproc
   };
   enum class TXSignMode {
      Full,
      Partial
   };
   struct Limits {
      uint64_t    autoSignSpendXBT = UINT64_MAX;
      uint64_t    manualSpendXBT = UINT64_MAX;
      int         autoSignTimeS = 0;
      int         manualPassKeepInMemS = 0;

      Limits() {}
      Limits(uint64_t asXbt, uint64_t manXbt, int asTime, int manPwTime)
         : autoSignSpendXBT(asXbt), manualSpendXBT(manXbt), autoSignTimeS(asTime)
         , manualPassKeepInMemS(manPwTime) {}
   };
   using RequestId = unsigned int;
   using PasswordType = SecureBinaryData;

   SignContainer(const std::shared_ptr<spdlog::logger> &, OpMode opMode);
   ~SignContainer() noexcept = default;

   SignContainer(const SignContainer&) = delete;
   SignContainer& operator = (const SignContainer&) = delete;
   SignContainer(SignContainer&&) = delete;
   SignContainer& operator = (SignContainer&&) = delete;

   virtual bool Start() = 0;
   virtual bool Stop() = 0;
   virtual bool Connect() = 0;
   virtual bool Disconnect() = 0;

   virtual RequestId SignTXRequest(const bs::wallet::TXSignRequest &, bool autoSign = false
      , TXSignMode mode = TXSignMode::Full, const PasswordType& password = {}
      , bool keepDuplicatedRecipients = false) = 0;
   virtual RequestId SignPartialTXRequest(const bs::wallet::TXSignRequest &
      , bool autoSign = false, const PasswordType& password = {}) = 0;
   virtual RequestId SignPayoutTXRequest(const bs::wallet::TXSignRequest &, const bs::Address &authAddr
      , const std::shared_ptr<bs::SettlementAddressEntry> &
      , bool autoSign = false, const PasswordType& password = {}) = 0;

   virtual RequestId SignMultiTXRequest(const bs::wallet::TXMultiSignRequest &) = 0;

   virtual void SendPassword(const std::string &walletId, const PasswordType &password,
      bool cancelledByUser) = 0;
   virtual RequestId CancelSignTx(const BinaryData &txId) = 0;

   virtual RequestId SetUserId(const BinaryData &) = 0;
   virtual RequestId SyncAddresses(const std::vector<std::pair<std::shared_ptr<bs::Wallet>, bs::Address>> &) = 0;
   virtual RequestId CreateHDLeaf(const std::shared_ptr<bs::hd::Wallet> &, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}) = 0;
   virtual RequestId CreateHDWallet(const std::string &name, const std::string &desc
      , bool primary, const bs::wallet::Seed &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}, bs::wallet::KeyRank keyRank = { 0, 0 }) = 0;
   virtual RequestId DeleteHD(const std::shared_ptr<bs::hd::Wallet> &) = 0;
   virtual RequestId DeleteHD(const std::shared_ptr<bs::Wallet> &) = 0;
   virtual RequestId GetDecryptedRootKey(const std::shared_ptr<bs::hd::Wallet> &, const SecureBinaryData &password = {}) = 0;
   virtual RequestId GetInfo(const std::shared_ptr<bs::hd::Wallet> &) = 0;
   virtual void SetLimits(const std::shared_ptr<bs::hd::Wallet> &, const SecureBinaryData &password, bool autoSign) = 0;
   virtual RequestId ChangePassword(const std::shared_ptr<bs::hd::Wallet> &, const std::vector<bs::wallet::PasswordData> &newPass
      , bs::wallet::KeyRank, const SecureBinaryData &oldPass
      , bool addNew, bool removeOld, bool dryRun) = 0;

   const OpMode &opMode() const { return mode_; }
   virtual bool hasUI() const { return false; }
   virtual bool isReady() const = 0;
   virtual bool isOffline() const = 0;
   virtual bool isWalletOffline(const std::string &walletId) const = 0;

signals:
   void connected();
   void disconnected();
   void authenticated();
   void connectionError(const QString &err);
   void ready();
   void Error(RequestId id, std::string error);
   void TXSigned(RequestId id, BinaryData signedTX, std::string error, bool cancelledByUser);

   void PasswordRequested(std::string walletId, std::string prompt, std::vector<bs::wallet::EncryptionType>
      , std::vector<SecureBinaryData> encKey, bs::wallet::KeyRank);

   void HDLeafCreated(RequestId id, BinaryData pubKey, BinaryData chainCode, std::string walletId);
   void HDWalletCreated(RequestId id, std::shared_ptr<bs::hd::Wallet>);
   void DecryptedRootKey(RequestId id, const SecureBinaryData &privKey, const SecureBinaryData &chainCode
      , std::string walletId);
   void HDWalletInfo(unsigned int id, std::vector<bs::wallet::EncryptionType>
      , std::vector<SecureBinaryData> &encKeys, bs::wallet::KeyRank);
   void MissingWallets(const std::vector<std::string> &);
   void AddressSyncFailed(const std::vector<std::pair<std::string, std::string>> &failedAddresses);
   void AddressSyncComplete();
   void UserIdSet();
   void PasswordChanged(const std::string &walletId, bool success);
   void AutoSignStateChanged(const std::string &walletId, bool active, const std::string &error);

protected:
   std::shared_ptr<spdlog::logger> logger_;
   const OpMode mode_;
};


std::shared_ptr<SignContainer> CreateSigner(const std::shared_ptr<spdlog::logger> &
   , const std::shared_ptr<ApplicationSettings> &, SignContainer::OpMode
   , const QString &host
   , const std::shared_ptr<ConnectionManager> & connectionManager);

bool SignerConnectionExists(const QString &host, const QString &port);


#endif // __SIGN_CONTAINER_H__
