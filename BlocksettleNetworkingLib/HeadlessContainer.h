/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef HEADLESS_CONTAINER_H
#define HEADLESS_CONTAINER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include <QStringList>

#include "BIP15xHelpers.h"
#include "DataConnectionListener.h"
#include "ThreadSafeContainers.h"
#include "WalletSignerContainer.h"


namespace spdlog {
   class logger;
}
namespace bs {
   class SettlementAddressEntry;
   namespace hd {
      class Wallet;
   }
   namespace network {
      class TransportBIP15xClient;
   }
}
namespace Blocksettle {
   namespace Communication {
      namespace headless {
         class RequestPacket;
      }
   }
};

class ConnectionManager;
class DataConnection;
class HeadlessListener;
class QProcess;
class WalletsManager;

class HeadlessContainer : public WalletSignerContainer
{
public:
   static NetworkType mapNetworkType(Blocksettle::Communication::headless::NetworkType netType);

   HeadlessContainer(const std::shared_ptr<spdlog::logger> &, OpMode, SignerCallbackTarget *);
   ~HeadlessContainer() noexcept override = default;

   [[deprecated]] bs::signer::RequestId signTXRequest(const bs::core::wallet::TXSignRequest &
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false) override;
   void signTXRequest(const bs::core::wallet::TXSignRequest&
      , const std::function<void(const BinaryData &signedTX, bs::error::ErrorCode
         , const std::string& errorReason)>&
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false) override;

   bs::signer::RequestId signSettlementTXRequest(const bs::core::wallet::TXSignRequest &txSignReq
      , const bs::sync::PasswordDialogData &dialogData
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false
      , const SignTxCb &cb = nullptr) override;

   bs::signer::RequestId signSettlementPartialTXRequest(const bs::core::wallet::TXSignRequest &txSignReq
      , const bs::sync::PasswordDialogData &dialogData
      , const SignTxCb &cb = nullptr) override;

   bs::signer::RequestId signSettlementPayoutTXRequest(const bs::core::wallet::TXSignRequest &txSignReq
      , const bs::core::wallet::SettlementData &, const bs::sync::PasswordDialogData &dialogData
      , const SignTxCb &cb = nullptr) override;

   bs::signer::RequestId signAuthRevocation(const std::string &walletId, const bs::Address &authAddr
      , const UTXO &, const bs::Address &bsAddr, const SignTxCb &cb = nullptr) override;

   bs::signer::RequestId resolvePublicSpenders(const bs::core::wallet::TXSignRequest &
      , const SignerStateCb &cb) override;

   bs::signer::RequestId updateDialogData(const bs::sync::PasswordDialogData &dialogData, uint32_t dialogId = 0) override;

   bs::signer::RequestId CancelSignTx(const BinaryData &txId) override;

   bs::signer::RequestId setUserId(const BinaryData &, const std::string &walletId) override;
   bs::signer::RequestId syncCCNames(const std::vector<std::string> &) override;

   bool createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData>& = {}, bs::sync::PasswordDialogData dialogData = {}, const CreateHDLeafCb &cb = nullptr) override;

   bool enableTradingInHDWallet(const std::string& rootWalletId, const BinaryData &userId
      , bs::sync::PasswordDialogData dialogData = {}, const UpdateWalletStructureCB& cb = nullptr) override;

   bool promoteWalletToPrimary(const std::string& rootWalletId
      , bs::sync::PasswordDialogData dialogData = {}, const UpdateWalletStructureCB& cb = nullptr) override;

   bs::signer::RequestId DeleteHDRoot(const std::string &rootWalletId) override;
   bs::signer::RequestId DeleteHDLeaf(const std::string &leafWalletId) override;
   bs::signer::RequestId GetInfo(const std::string &rootWalletId) override;
   //void setLimits(const std::string &walletId, const SecureBinaryData &password, bool autoSign) override;
   bs::signer::RequestId customDialogRequest(bs::signer::ui::GeneralDialogType signerDialog, const QVariantMap &data = QVariantMap()) override;

   void syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &) override;
   void syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &) override;
   void syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &) override;
   void syncAddressComment(const std::string &walletId, const bs::Address &, const std::string &) override;
   void syncTxComment(const std::string &walletId, const BinaryData &, const std::string &) override;

   void setSettlAuthAddr(const std::string &walletId, const BinaryData &, const bs::Address &addr) override;
   void getSettlAuthAddr(const std::string &walletId, const BinaryData &
      , const std::function<void(const bs::Address &)> &) override;
   void setSettlCP(const std::string &walletId, const BinaryData &payinHash, const BinaryData &settlId
      , const BinaryData &cpPubKey) override;
   void getSettlCP(const std::string &walletId, const BinaryData &payinHash
      , const std::function<void(const BinaryData &, const BinaryData &)> &) override;

   void syncNewAddresses(const std::string &walletId, const std::vector<std::string> &
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;
   void syncAddressBatch(const std::string &walletId,
      const std::set<BinaryData>& addrSet, std::function<void(bs::sync::SyncState)>) override;
   void extendAddressChain(const std::string &walletId, unsigned count, bool extInt,
      const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;

   void createSettlementWallet(const bs::Address &authAddr
      , const std::function<void(const SecureBinaryData &)> &) override;
   void setSettlementID(const std::string &walletId, const SecureBinaryData &id
      , const std::function<void(bool, const SecureBinaryData&)> &) override;
   void getSettlementPayinAddress(const std::string &walletId
      , const bs::core::wallet::SettlementData &
      , const std::function<void(bool, bs::Address)> &) override;
   void getRootPubkey(const std::string &walletID
      , const std::function<void(bool, const SecureBinaryData &)> &) override;
   void getAddressPubkey(const std::string& walletID, const std::string& address
      , const std::function<void(const SecureBinaryData&)>&) override;
   void getChatNode(const std::string &walletID
      , const std::function<void(const BIP32_Node &)> &) override;

   bool isReady() const override;
   bool isWalletOffline(const std::string &walletId) const override;

   virtual void restartConnection() = 0;
   virtual void onConnected() = 0;
   virtual void onDisconnected() = 0;
   virtual void onConnError(ConnectionError error, const QString &details) = 0;
   virtual void onAuthenticated() = 0;
   virtual void onPacketReceived(const Blocksettle::Communication::headless::RequestPacket &) = 0;

protected:
   bs::signer::RequestId Send(const Blocksettle::Communication::headless::RequestPacket &, bool incSeqNo = true);
   void ProcessSignTXResponse(unsigned int id, const std::string &data);
   void ProcessSettlementSignTXResponse(unsigned int id, const std::string &data);
   void ProcessPubResolveResponse(unsigned int id, const std::string &data);
   void ProcessCreateHDLeafResponse(unsigned int id, const std::string &data);
   void ProcessEnableTradingInWalletResponse(unsigned int id, const std::string& data);
   void ProcessPromoteWalletResponse(unsigned int id, const std::string& data);
   void ProcessGetHDWalletInfoResponse(unsigned int id, const std::string &data);
   void ProcessAutoSignActEvent(unsigned int id, const std::string &data);
   void ProcessSyncWalletInfo(unsigned int id, const std::string &data);
   void ProcessSyncHDWallet(unsigned int id, const std::string &data);
   void ProcessSyncWallet(unsigned int id, const std::string &data);
   void ProcessSyncAddresses(unsigned int id, const std::string &data);
   void ProcessExtAddrChain(unsigned int id, const std::string &data);
   void ProcessSettlWalletCreate(unsigned int id, const std::string &data);
   void ProcessSetSettlementId(unsigned int id, const std::string &data);
   void ProcessSetUserId(const std::string &data);
   void ProcessGetPayinAddr(unsigned int id, const std::string &data);
   void ProcessSettlGetRootPubkey(unsigned int id, const std::string &data);
   void ProcessUpdateStatus(const std::string &data);
   void ProcessChatNodeResponse(unsigned int id, const std::string &data);
   void ProcessSettlAuthResponse(unsigned int id, const std::string &data);
   void ProcessSettlCPResponse(unsigned int id, const std::string &data);
   void ProcessWindowStatus(unsigned int id, const std::string &data);
   void ProcessAddrPubkeyResponse(unsigned int id, const std::string& data);

protected:
   std::shared_ptr<HeadlessListener>   listener_;
   std::unordered_set<std::string>     missingWallets_;
   std::unordered_set<std::string>     woWallets_;
   std::set<bs::signer::RequestId>     signRequests_;

   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(std::vector<bs::sync::WalletInfo>)>>         cbWalletInfoMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(bs::sync::HDWalletData)>>  cbHDWalletMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(bs::sync::WalletData)>>    cbWalletMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(bs::sync::SyncState)>>     cbSyncAddrsMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>> cbExtAddrsMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>> cbNewAddrsMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, SignTxCb> cbSettlementSignTxMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, SignerStateCb>  cbSignerStateMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(const SecureBinaryData &)>>   cbSettlWalletMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(bool, bs::Address)>>          cbPayinAddrMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(bool, const SecureBinaryData &)>>   cbSettlPubkeyMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(const BIP32_Node &)>>   cbChatNodeMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(const bs::Address &)>>  cbSettlAuthMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(const BinaryData &, const BinaryData &)>>  cbSettlCPMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, std::function<void(BinaryData signedTX, bs::error::ErrorCode result, const std::string& errorReason)>> signTxMap_;

   bs::ThreadSafeMap<bs::signer::RequestId, CreateHDLeafCb>          cbCCreateLeafMap_;
   bs::ThreadSafeMap<bs::signer::RequestId, UpdateWalletStructureCB> cbUpdateWalletMap_;
};


class QtHCT : public QObject, public SignerCallbackTarget
{
   Q_OBJECT
public:
   QtHCT(QObject* parent) : QObject(parent) {}

   void connected(const std::string& host) override { emit connected(); }
   void connError(SignContainer::ConnectionError err, const QString& desc) override { emit connectionError(err, desc); }
   void connTorn() override { emit disconnected(); }
   void onError(bs::signer::RequestId reqId, const std::string& errMsg) override;
   void onAuthComplete() override { emit authenticated(); }
   void onReady() override { emit ready(); }
   void txSigned(bs::signer::RequestId reqId, const BinaryData& signedTX
      , bs::error::ErrorCode errCode, const std::string& errMsg = {}) override;
   void walletInfo(bs::signer::RequestId reqId
      , const Blocksettle::Communication::headless::GetHDWalletInfoResponse& wi) override;
   void autoSignStateChanged(bs::error::ErrorCode errCode
      , const std::string& walletId) override;
   void authLeafAdded(const std::string& walletId);
   void newWalletPrompt() override { emit needNewWalletPrompt(); }
   void walletsReady() override { emit walletsReadyToSync(); }
   void walletsChanged() override { emit walletsListUpdated(); }
   void windowIsVisible(bool v) override { emit windowVisibilityChanged(v); }

signals:
   void connected();
   void connectionError(SignContainer::ConnectionError, const QString&);
   void disconnected();
   void authenticated();
   void ready();
   void needNewWalletPrompt();
   void walletsReadyToSync();
   void walletsListUpdated();
//   void SignerCallbackTarget();
   void windowVisibilityChanged(bool);

   void Error(bs::signer::RequestId id, const std::string& errMsg);
   void TXSigned(bs::signer::RequestId id, const BinaryData& signedTX
      , bs::error::ErrorCode errCode, const std::string& errMsg);
   void QWalletInfo(bs::signer::RequestId, const bs::hd::WalletInfo&);
   void AutoSignStateChanged(bs::error::ErrorCode errCode
      , const std::string& walletId);
   void AuthLeafAdded(const std::string&);
};


class RemoteSigner : public HeadlessContainer
{
public:
   RemoteSigner(const std::shared_ptr<spdlog::logger> &, const QString &host
      , const QString &port, NetworkType netType
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , SignerCallbackTarget *, OpMode opMode = OpMode::Remote
      , const bool ephemeralDataConnKeys = true
      , const std::string& ownKeyFileDir = ""
      , const std::string& ownKeyFileName = ""
      , const bs::network::BIP15xNewKeyCb &inNewKeyCB = nullptr);
   ~RemoteSigner() noexcept override;

   void Start(void) override;
   bool Stop() override;
   void Connect(void) override;
   bool Disconnect() override;
   bool isOffline() const override;
   void updatePeerKeys(const bs::network::BIP15xPeers &);
   void reconnect();

protected:
   void onConnected() override ;
   void onDisconnected() override;
   void onConnError(ConnectionError error, const QString &details) override;
   void onAuthenticated() override;
   void onPacketReceived(const Blocksettle::Communication::headless::RequestPacket &) override;
   void restartConnection() override;

private:
   void Authenticate();
   // Recreates new ZmqBIP15XDataConnection because it can't gracefully handle server restart
   void RecreateConnection();
   void ScheduleRestart();

protected:
   const QString                       host_;
   const QString                       port_;
   const NetworkType                   netType_;
   const bool                          ephemeralDataConnKeys_;
   const std::string                   ownKeyFileDir_;
   const std::string                   ownKeyFileName_;
   std::shared_ptr<DataConnection>     connection_;
   std::shared_ptr<bs::network::TransportBIP15xClient>   bip15xTransport_;
   const bs::network::BIP15xNewKeyCb   cbNewKey_{ nullptr };

private:
   std::shared_ptr<ConnectionManager> connectionManager_;
   mutable std::mutex   mutex_;
   bool headlessConnFinished_ = false;
   std::atomic_bool  isRestartScheduled_{false};
   std::thread       restartThread_;
};

class LocalSigner : public RemoteSigner
{
public:
   LocalSigner(const std::shared_ptr<spdlog::logger> &, const QString &homeDir
      , NetworkType, const QString &port
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , SignerCallbackTarget *
      , const bool startSignerProcess = true
      , const std::string& ownKeyFileDir = ""
      , const std::string& ownKeyFileName = ""
      , double asSpendLimit = 0
      , const bs::network::BIP15xNewKeyCb &inNewKeyCB = nullptr);
   ~LocalSigner() noexcept override;

   void Start(void) override;
   void Connect(void) override {}
   bool Stop() override;

protected:
   virtual QStringList args() const;

private:
   const QString  homeDir_;
   const bool     startProcess_;
   const double   asSpendLimit_;
   std::recursive_mutex mutex_;
   std::shared_ptr<QProcess>  headlessProcess_;
};


class HeadlessListener : public DataConnectionListener
{
public:
   HeadlessListener(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<DataConnection> &conn, NetworkType netType
      , HeadlessContainer *hc)
      : logger_(logger), connection_(conn), netType_(netType)
      , parent_(hc)
   {}

   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

   bs::signer::RequestId Send(Blocksettle::Communication::headless::RequestPacket
      , bool updateId = true);

   bool isReady() const { return isReady_; }
   bool addCookieKeyToKeyStore(const std::string&, const std::string&);

   void setConnected(bool flag = true);

private:
   bs::signer::RequestId newRequestId();

   void processDisconnectNotification();
   void tryEmitError(SignContainer::ConnectionError errorCode, const QString &msg);

private:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<DataConnection>  connection_;
   const NetworkType                netType_;
   HeadlessContainer    *parent_{ nullptr };

   std::atomic<bs::signer::RequestId>            id_{0};

   // This will be updated from background thread
   std::atomic<bool>                isReady_{false};
   bool                             isConnected_{false};
   bool                             wasErrorReported_{false};
   std::atomic<bool>                isShuttingDown_{false};
};

#endif // __HEADLESS_CONTAINER_H__
