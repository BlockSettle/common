#ifndef __HEADLESS_CONTAINER_H__
#define __HEADLESS_CONTAINER_H__

#include <string>
#include <memory>
#include <QObject>
#include <QStringList>
#include "MetaData.h"
#include "SignContainer.h"
#include "ApplicationSettings.h"

#include "headless.pb.h"

#include <mutex>
#include <set>

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
class HeadlessListener;
class QProcess;
class WalletsManager;
class ZmqSecuredDataConnection;


class HeadlessContainer : public SignContainer
{
   Q_OBJECT
public:
   HeadlessContainer(const std::shared_ptr<spdlog::logger> &, OpMode);
   ~HeadlessContainer() noexcept = default;

   HeadlessContainer(const HeadlessContainer&) = delete;
   HeadlessContainer& operator = (const HeadlessContainer&) = delete;
   HeadlessContainer(HeadlessContainer&&) = delete;
   HeadlessContainer& operator = (HeadlessContainer&&) = delete;

   RequestId SignTXRequest(const bs::wallet::TXSignRequest &, bool autoSign = false
      , TXSignMode mode = TXSignMode::Full, const PasswordType& password = {}
      , bool keepDuplicatedRecipients = false) override;
   RequestId SignPartialTXRequest(const bs::wallet::TXSignRequest &
      , bool autoSign = false, const PasswordType& password = {}) override;
   RequestId SignPayoutTXRequest(const bs::wallet::TXSignRequest &, const bs::Address &authAddr
      , const std::shared_ptr<bs::SettlementAddressEntry> &
      , bool autoSign = false, const PasswordType& password = {}) override;

   RequestId SignMultiTXRequest(const bs::wallet::TXMultiSignRequest &) override;

   void SendPassword(const std::string &walletId, const PasswordType &password,
      bool cancelledByUser) override;

   RequestId CancelSignTx(const BinaryData &txId) override;

   RequestId SetUserId(const BinaryData &) override;
   RequestId SyncAddresses(const std::vector<std::pair<std::shared_ptr<bs::Wallet>, bs::Address>> &) override;
   RequestId CreateHDLeaf(const std::shared_ptr<bs::hd::Wallet> &, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}) override;
   RequestId CreateHDWallet(const std::string &name, const std::string &desc
      , bool primary, const bs::wallet::Seed &seed
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}, bs::wallet::KeyRank keyRank = { 0, 0 }) override;
   RequestId DeleteHDRoot(const std::string &rootWalletId) override;
   RequestId DeleteHDLeaf(const std::string &leafWalletId) override;
   RequestId GetDecryptedRootKey(const std::shared_ptr<bs::hd::Wallet> &, const SecureBinaryData &password = {}) override;
   RequestId GetInfo(const std::string &rootWalletId) override;
   void SetLimits(const std::shared_ptr<bs::hd::Wallet> &, const SecureBinaryData &password, bool autoSign) override;
   RequestId ChangePassword(const std::shared_ptr<bs::hd::Wallet> &, const std::vector<bs::wallet::PasswordData> &newPass
      , bs::wallet::KeyRank, const SecureBinaryData &oldPass
      , bool addNew, bool removeOld, bool dryRun) override;

   bool isReady() const override;
   bool isWalletOffline(const std::string &walletId) const override;

protected:
   RequestId Send(Blocksettle::Communication::headless::RequestPacket, bool incSeqNo = true);
   void ProcessSignTXResponse(unsigned int id, const std::string &data);
   void ProcessPasswordRequest(const std::string &data);
   void ProcessCreateHDWalletResponse(unsigned int id, const std::string &data);
   RequestId SendDeleteHDRequest(const std::string &rootWalletId, const std::string &leafId);
   void ProcessGetRootKeyResponse(unsigned int id, const std::string &data);
   void ProcessGetHDWalletInfoResponse(unsigned int id, const std::string &data);
   void ProcessSyncAddrResponse(const std::string &data);
   void ProcessChangePasswordResponse(unsigned int id, const std::string &data);
   void ProcessSetLimitsResponse(unsigned int id, const std::string &data);

   std::shared_ptr<HeadlessListener>   listener_;
   std::unordered_set<std::string>     missingWallets_;
   std::set<RequestId>                 signRequests_;
};

bool KillHeadlessProcess();


class RemoteSigner : public HeadlessContainer
{
   Q_OBJECT
public:
   RemoteSigner(const std::shared_ptr<spdlog::logger> &, const QString &host
      , const QString &port, NetworkType netType
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , const std::shared_ptr<ApplicationSettings>& appSettings
      , const SecureBinaryData& pubKey
      , OpMode opMode = OpMode::Remote);
   ~RemoteSigner() noexcept = default;

   bool Start() override;
   bool Stop() override;
   bool Connect() override;
   bool Disconnect() override;
   bool isOffline() const override;
   bool hasUI() const override;

protected slots:
   void onAuthenticated();
   void onConnected();
   void onDisconnected();
   void onConnError(const QString &err);
   void onPacketReceived(Blocksettle::Communication::headless::RequestPacket);

private:
   void ConnectHelper();
   void Authenticate();

protected:
   const QString          host_;
   const QString          port_;
   const NetworkType      netType_;
   std::shared_ptr<ZmqSecuredDataConnection> connection_;
   SecureBinaryData       zmqSignerPubKey_;
   bool  authPending_ = false;
   std::shared_ptr<ApplicationSettings> appSettings_;

private:
   std::shared_ptr<ConnectionManager> connectionManager_;
   mutable std::mutex   mutex_;
};

class LocalSigner : public RemoteSigner
{
   Q_OBJECT
public:
   LocalSigner(const std::shared_ptr<spdlog::logger> &, const QString &homeDir
      , NetworkType, const QString &port
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , const std::shared_ptr<ApplicationSettings>& appSettings
      , const SecureBinaryData& pubKey
      , double asSpendLimit = 0);
   ~LocalSigner() noexcept = default;

   bool Start() override;
   bool Stop() override;

private:
   QStringList                args_;
   std::shared_ptr<QProcess>  headlessProcess_;
};


class HeadlessAddressSyncer : public QObject
{
   Q_OBJECT
public:
   HeadlessAddressSyncer(const std::shared_ptr<SignContainer> &, const std::shared_ptr<WalletsManager> &);

   void SyncWallet(const std::shared_ptr<bs::Wallet> &);

private slots:
   void onWalletsUpdated();
   void onSignerReady();

private:
   std::shared_ptr<SignContainer>   signingContainer_;
   std::shared_ptr<WalletsManager>  walletsMgr_;
   bool     wasOffline_ = false;
};

#endif // __HEADLESS_CONTAINER_H__
