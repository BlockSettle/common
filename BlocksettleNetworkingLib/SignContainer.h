/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
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

#include "PasswordDialogData.h"

namespace spdlog {
   class logger;
}
namespace Codec_SignerState {
   class SignerState;
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

class SignContainer : public QObject
{
   Q_OBJECT
public:
   enum class OpMode {
      Local = 1,
      Remote,
      LocalInproc
   };
   enum class TXSignMode {
      Full,
      Partial
   };
   using PasswordType = SecureBinaryData;

   enum ConnectionError //TODO: rename to ConnectionStatus
   {
      NoError, // TODO: rename to Connected
      Ready,   // AKA authenticated
      UnknownError,
      SocketFailed,
      HostNotFound,
      HandshakeFailed,
      SerializationFailed,
      HeartbeatWaitFailed,
      InvalidProtocol,
      NetworkTypeMismatch,
      ConnectionTimeout,
      SignerGoesOffline,
      CookieError
   };
   Q_ENUM(ConnectionError)

   SignContainer(const std::shared_ptr<spdlog::logger> &, OpMode opMode);
   ~SignContainer() noexcept = default;

   virtual void Start(void) = 0;
   virtual bool Stop() { return true; }
   virtual void Connect(void) = 0;
   virtual bool Disconnect() { return true; }

   using SignTxCb = std::function<void(bs::error::ErrorCode result, const BinaryData &)>;
   using SignerStateCb = std::function<void(bs::error::ErrorCode result
      , const Codec_SignerState::SignerState &)>;

   // If wallet is offline serialize request and write to file with path TXSignRequest::offlineFilePath
   [[deprecated]] virtual bs::signer::RequestId signTXRequest(const bs::core::wallet::TXSignRequest &
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false) = 0;

   virtual void signTXRequest(const bs::core::wallet::TXSignRequest&
      , const std::function<void(const BinaryData &signedTX, bs::error::ErrorCode
         , const std::string& errorReason)> &
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false) = 0;

   virtual bs::signer::RequestId signSettlementTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::sync::PasswordDialogData &dialogData
      , TXSignMode mode = TXSignMode::Full
      , bool keepDuplicatedRecipients = false
      , const std::function<void(bs::error::ErrorCode result, const BinaryData &signedTX)> &cb = nullptr) = 0;

   virtual bs::signer::RequestId signSettlementPartialTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::sync::PasswordDialogData &dialogData
      , const SignTxCb &cb = nullptr) = 0;

   virtual bs::signer::RequestId signSettlementPayoutTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::core::wallet::SettlementData &, const bs::sync::PasswordDialogData &dialogData
      , const SignTxCb &cb = nullptr) = 0;

   virtual bs::signer::RequestId signAuthRevocation(const std::string &walletId, const bs::Address &authAddr
      , const UTXO &, const bs::Address &bsAddr, const SignTxCb &cb = nullptr) = 0;

   virtual bs::signer::RequestId resolvePublicSpenders(const bs::core::wallet::TXSignRequest &
      , const SignerStateCb &cb) = 0;

   virtual bs::signer::RequestId updateDialogData(const bs::sync::PasswordDialogData &dialogData, uint32_t dialogId = 0) = 0;

   virtual bs::signer::RequestId CancelSignTx(const BinaryData &txId) = 0;

   virtual bs::signer::RequestId setUserId(const BinaryData &, const std::string &walletId) = 0;
   virtual bs::signer::RequestId syncCCNames(const std::vector<std::string> &) = 0;

   virtual bs::signer::RequestId GetInfo(const std::string &rootWalletId) = 0;

   virtual bs::signer::RequestId customDialogRequest(bs::signer::ui::GeneralDialogType signerDialog
      , const QVariantMap &data = QVariantMap()) = 0;

   virtual void syncNewAddress(const std::string &walletId, const std::string &index
      , const std::function<void(const bs::Address &)> &);
   virtual void syncNewAddresses(const std::string &walletId, const std::vector<std::string> &
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) = 0;

   const OpMode &opMode() const { return mode_; }
   virtual bool isReady() const { return true; }
   virtual bool isOffline() const { return true; }
   virtual bool isWalletOffline(const std::string &) const { return true; }

   bool isLocal() const { return mode_ == OpMode::Local || mode_ == OpMode::LocalInproc; }
   bool isWindowVisible() const { return isWindowVisible_; } // available only for local signer

signals:
   void connected();
   void disconnected();
   void authenticated();
   void connectionError(ConnectionError error, const QString &details);
   void ready();
   void Error(bs::signer::RequestId id, std::string error);
   void TXSigned(bs::signer::RequestId id, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason = {});
   // emitted only for local signer
   void windowVisibilityChanged(bool visible);

   void QWalletInfo(unsigned int id, const bs::hd::WalletInfo &);
   void PasswordChanged(const std::string &walletId, bool success);

   // NoError mean turned on, AutoSignDisabled mean turned off, other codes mean error
   void AutoSignStateChanged(bs::error::ErrorCode result, const std::string &walletId);

protected:
   std::shared_ptr<spdlog::logger> logger_;
   const OpMode mode_;
   bool isWindowVisible_{};
};


bool SignerConnectionExists(const QString &host, const QString &port);


#endif // __SIGN_CONTAINER_H__
