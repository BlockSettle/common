#ifndef __MOBILE_CLIENT_H__
#define __MOBILE_CLIENT_H__

#include <botan/ecies.h>
#include <botan/auto_rng.h>
#include <QObject>
#include "DataConnectionListener.h"
#include "EncryptionUtils.h"
#include "MobileClient.h"
#include "ZmqSecuredDataConnection.h"
#include "EncryptUtils.h"
#include "rp_api.pb.h"

namespace spdlog {
   class logger;
}

class QTimer;
class ConnectionManager;
class RequestReplyCommand;

class MobileClient : public QObject, public DataConnectionListener
{
   Q_OBJECT

public:
   struct DeviceInfo
   {
      std::string userId;
      std::string deviceId;
      std::string deviceName;
   };

   enum RequestType
   {
      Unknown,
      ActivateWallet,
      DeactivateWallet,
      SignWallet,
      BackupWallet,
      ActivateWalletOldDevice,
      ActivateWalletNewDevice,
      DeactivateWalletDevice,
      VerifyWalletKey,
      ActivateOTP,
      // Private market and others with lower timeout
      SettlementTransaction,

      // Please also add new type text in getMobileClientRequestText
   };
   Q_ENUM(RequestType)

   static DeviceInfo getDeviceInfo(const std::string &encKey);

   MobileClient(const std::shared_ptr<spdlog::logger> &
      , const std::pair<autheid::PrivateKey, autheid::PublicKey> &
      , QObject *parent = nullptr);
   ~MobileClient() override;

   void connect(const std::string &serverPubKey
      , const std::string &serverHost, const std::string &serverPort);
   bool start(RequestType requestType, const std::string &email, const std::string &walletId
      , const std::vector<std::string> &knownDeviceIds);
   bool sign(const BinaryData &data, const std::string &email
      , const QString &title, const QString &description, int expiration = 30);
   bool authenticate(const std::string& email);
   void cancel();

   bool isConnected() const;

signals:
   void succeeded(const std::string& encKey, const SecureBinaryData &password);
   void signSuccess(const std::string &data, const BinaryData &invisibleData, const std::string &signature);
   void authSuccess(const std::string &jwt);
   void failed(const QString &text);

private slots:
   void timeout();

private:
   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

   bool sendToAuthServer(const std::string &payload, const AutheID::RP::PayloadType type);
   void processCreateReply(const uint8_t *payload, size_t payloadSize);
   void processResultReply(const uint8_t *payload, size_t payloadSize);

   void processSignatureReply(const AutheID::RP::SignatureReply &);

   QString getMobileClientRequestText(RequestType requestType);
   bool isMobileClientNewDeviceNeeded(RequestType requestType);
   int getMobileClientTimeout(RequestType requestType);

private:
   std::unique_ptr<ConnectionManager> connectionManager_;
   std::shared_ptr<ZmqSecuredDataConnection> connection_;
   std::shared_ptr<spdlog::logger> logger_;
   std::string requestId_;
   std::string email_;
   bool resultAuth_;

   const std::pair<autheid::PrivateKey, autheid::PublicKey> authKeys_;

   QTimer *timer_;

   std::vector<std::string> knownDeviceIds_;
};
Q_DECLARE_METATYPE(MobileClient::RequestType)

#endif // __MOBILE_CLIENT_H__
