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

#include "DataConnectionListener.h"
#include "WalletSignerContainer.h"
#include "BIP15xHelpers.h"

#include "headless.pb.h"

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

class ConnectionManager;
class DataConnection;
class HeadlessListener;
class QProcess;
class WalletsManager;

class HeadlessContainer : public WalletSignerContainer
{
   Q_OBJECT
public:
   static NetworkType mapNetworkType(Blocksettle::Communication::headless::NetworkType netType);

   HeadlessContainer(const std::shared_ptr<spdlog::logger> &, OpMode);
   ~HeadlessContainer() noexcept override = default;

   bs::signer::RequestId signTXRequest(const bs::core::wallet::TXSignRequest &
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

   bs::signer::RequestId syncCCNames(const std::vector<std::string> &) override;

   bool createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData>& = {}, bs::sync::PasswordDialogData dialogData = {}, const CreateHDLeafCb &cb = nullptr) override;

   bool enableTradingInHDWallet(const std::string& rootWalletId
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
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &
      , bool persistent = true) override;
   void syncAddressBatch(const std::string &walletId,
      const std::set<BinaryData>& addrSet, std::function<void(bs::sync::SyncState)>) override;
   void extendAddressChain(const std::string &walletId, unsigned count, bool extInt,
      const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;

   void createSettlementWallet(const bs::Address &authAddr
      , const std::function<void(const SecureBinaryData &)> &) override;
   void setSettlementID(const std::string &walletId, const SecureBinaryData &id
      , const std::function<void(bool)> &) override;
   void getSettlementPayinAddress(const std::string &walletId
      , const bs::core::wallet::SettlementData &
      , const std::function<void(bool, bs::Address)> &) override;
   void getRootPubkey(const std::string &walletID
      , const std::function<void(bool, const SecureBinaryData &)> &) override;
   void getChatNode(const std::string &walletID
      , const std::function<void(const BIP32_Node &)> &) override;

   bool isReady() const override;
   bool isWalletOffline(const std::string &walletId) const override;

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
   void ProcessGetPayinAddr(unsigned int id, const std::string &data);
   void ProcessSettlGetRootPubkey(unsigned int id, const std::string &data);
   void ProcessAddrPreimageResponse(unsigned int id, const std::string &data);
   void ProcessUpdateStatus(const std::string &data);
   void ProcessChatNodeResponse(unsigned int id, const std::string &data);
   void ProcessSettlAuthResponse(unsigned int id, const std::string &data);
   void ProcessSettlCPResponse(unsigned int id, const std::string &data);
   void ProcessWindowStatus(unsigned int id, const std::string &data);

protected:
   std::shared_ptr<HeadlessListener>   listener_;
   std::unordered_set<std::string>     missingWallets_;
   std::unordered_set<std::string>     woWallets_;
   std::set<bs::signer::RequestId>     signRequests_;

   std::map<bs::signer::RequestId, std::function<void(std::vector<bs::sync::WalletInfo>)>>         cbWalletInfoMap_;
   std::map<bs::signer::RequestId, std::function<void(bs::sync::HDWalletData)>>  cbHDWalletMap_;
   std::map<bs::signer::RequestId, std::function<void(bs::sync::WalletData)>>    cbWalletMap_;
   std::map<bs::signer::RequestId, std::function<void(bs::sync::SyncState)>>     cbSyncAddrsMap_;
   std::map<bs::signer::RequestId, std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>> cbExtAddrsMap_;
   std::map<bs::signer::RequestId, std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>> cbNewAddrsMap_;
   std::map<bs::signer::RequestId, SignTxCb> cbSettlementSignTxMap_;
   std::map<bs::signer::RequestId, SignerStateCb>  cbSignerStateMap_;
   std::map<bs::signer::RequestId, std::function<void(const SecureBinaryData &)>>   cbSettlWalletMap_;
   std::map<bs::signer::RequestId, std::function<void(bool)>>                       cbSettlIdMap_;
   std::map<bs::signer::RequestId, std::function<void(bool, bs::Address)>>          cbPayinAddrMap_;
   std::map<bs::signer::RequestId, std::function<void(bool, const SecureBinaryData &)>>   cbSettlPubkeyMap_;
   std::map<bs::signer::RequestId, std::function<void(const BIP32_Node &)>>   cbChatNodeMap_;
   std::map<bs::signer::RequestId, std::function<void(const bs::Address &)>>  cbSettlAuthMap_;
   std::map<bs::signer::RequestId, std::function<void(const BinaryData &, const BinaryData &)>>  cbSettlCPMap_;

   std::map<bs::signer::RequestId, CreateHDLeafCb>          cbCCreateLeafMap_;
   std::map<bs::signer::RequestId, UpdateWalletStructureCB> cbUpdateWalletMap_;
};


class RemoteSigner : public HeadlessContainer
{
   Q_OBJECT
public:
   RemoteSigner(const std::shared_ptr<spdlog::logger> &, const QString &host
      , const QString &port, NetworkType netType
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , OpMode opMode = OpMode::Remote
      , const bool ephemeralDataConnKeys = true
      , const std::string& ownKeyFileDir = ""
      , const std::string& ownKeyFileName = ""
      , const bs::network::BIP15xNewKeyCb &inNewKeyCB = nullptr);
   ~RemoteSigner() noexcept override = default;

   void Start(void) override;
   bool Stop() override;
   void Connect(void) override;
   bool Disconnect() override;
   bool isOffline() const override;
   void updatePeerKeys(const bs::network::BIP15xPeers &);

protected slots:
   void onAuthenticated();
   void onConnected();
   void onDisconnected();
   void onConnError(ConnectionError error, const QString &details);
   void onPacketReceived(Blocksettle::Communication::headless::RequestPacket);

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
   bool isRestartScheduled_{false};
};

class LocalSigner : public RemoteSigner
{
   Q_OBJECT
public:
   LocalSigner(const std::shared_ptr<spdlog::logger> &, const QString &homeDir
      , NetworkType, const QString &port
      , const std::shared_ptr<ConnectionManager>& connectionManager
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
   std::shared_ptr<QProcess>  headlessProcess_;
};


class HeadlessListener : public QObject, public DataConnectionListener
{
   Q_OBJECT

   friend class RemoteSigner;

public:
   HeadlessListener(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<DataConnection> &conn, NetworkType netType)
      : logger_(logger), connection_(conn), netType_(netType) {}

   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

   bs::signer::RequestId Send(Blocksettle::Communication::headless::RequestPacket
      , bool updateId = true);

   bool isReady() const { return isReady_; }
   bool addCookieKeyToKeyStore(const std::string&, const std::string&);

signals:
   void authenticated();
   void authFailed();
   void connected();
   void disconnected();
   void error(HeadlessContainer::ConnectionError error, const QString &details);
   void PacketReceived(Blocksettle::Communication::headless::RequestPacket);

private:
   bs::signer::RequestId newRequestId();

   void processDisconnectNotification();
   void tryEmitError(SignContainer::ConnectionError errorCode, const QString &msg);

private:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<DataConnection>  connection_;
   const NetworkType                netType_;

   std::atomic<bs::signer::RequestId>            id_{0};

   // This will be updated from background thread
   std::atomic<bool>                isReady_{false};
   bool                             isConnected_{false};
   bool                             wasErrorReported_{false};
   std::atomic<bool>                isShuttingDown_{false};
};

#endif // __HEADLESS_CONTAINER_H__
