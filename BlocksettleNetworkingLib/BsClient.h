/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_CLIENT_H
#define BS_CLIENT_H

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>

#include <QObject>

#include <spdlog/logger.h>

#include "Address.h"

#include "autheid_utils.h"
#include "AutheIDClient.h"
#include "BSErrorCode.h"
#include "CelerMessageMapper.h"
#include "CommonTypes.h"
#include "DataConnectionListener.h"
#include "TradeSettings.h"
#include "ValidityFlag.h"

class DataConnection;
template<typename T> class FutureValue;

namespace Blocksettle {
   namespace Communication {
      namespace ProxyTerminal {
         class Request;
         class Response;
         class Response_Authorize;
         class Response_Celer;
         class Response_GenAddrUpdated;
         class Response_GetLoginResult;
         class Response_ProxyPb;
         class Response_StartLogin;
         class Response_UpdateBalance;
         class Response_UpdateFeeRate;
         class Response_UserStatusUpdated;
      }
   }
}

namespace Blocksettle {
   namespace Communication {
      namespace ProxyTerminalPb {
         class Request;
         class Response;
      }
   }
}

namespace bs {
   namespace network {
      enum class UserType : int;
   }
}

struct BsClientLoginResult
{
   AutheIDClient::ErrorType status{};
   std::string errorMsg;
   bs::network::UserType userType{};
   std::string celerLogin;
   BinaryData chatTokenData;
   BinaryData chatTokenSign;
   BinaryData authAddressesSigned;
   BinaryData ccAddressesSigned;
   bool enabled{};
   float feeRatePb{};
   bs::TradeSettings tradeSettings;
};

class BsClient : public QObject, public DataConnectionListener
{
   Q_OBJECT
public:
   struct BasicResponse
   {
      bool success{};
      std::string errorMsg;
   };
   using BasicCb = std::function<void(BasicResponse)>;

   using AuthConfirmCb = std::function<void(bs::error::AuthAddressSubmitResult)>;

   struct SignResponse : public BasicResponse
   {
      bool userCancelled{};
   };
   using SignCb = std::function<void(SignResponse)>;

   struct DescCc
   {
      std::string ccProduct;
   };

   using RequestId = int64_t;

   enum class AuthorizeError : int
   {
      NoError,
      UnknownApiKey,
      UnknownIpAddr,
      Timeout,
      ServerError,
   };

   BsClient(const std::shared_ptr<spdlog::logger>& logger
      , QObject *parent = nullptr);
   ~BsClient() override;

   void setConnection(std::unique_ptr<DataConnection>);

   void startLogin(const std::string &email);
   void authorize(const std::string &apiKey);

   void sendPbMessage(std::string data);

   // Cancel login. Please note that this will close channel.
   void cancelLogin();
   void getLoginResult();
   void logout();
   void celerSend(CelerAPI::CelerMessageType messageType, const std::string &data);

   void signAuthAddress(const bs::Address address, const SignCb &cb);
   void confirmAuthAddress(const bs::Address address, const AuthConfirmCb &cb);

   void submitCcAddress(const bs::Address address, uint32_t seed, const std::string &ccProduct, const BasicCb &cb);
   void signCcAddress(const bs::Address address, const SignCb &cb);
   void confirmCcAddress(const bs::Address address, const BasicCb &cb);

   void cancelActiveSign();

   static std::chrono::seconds autheidLoginTimeout();
   static std::chrono::seconds autheidAuthAddressTimeout();
   static std::chrono::seconds autheidCcAddressTimeout();

   // Returns how signed title and description text should look in the mobile device.
   // PB will check it to be sure that the user did sign what he saw.
   // NOTE: If text here will be updated make sure to update both PB and Proxy at the same time.
   static std::string requestTitleAuthAddr();
   static std::string requestDescAuthAddr(const bs::Address &address);
   // NOTE: CC address text details are not enforced on PB right now!
   static std::string requestTitleCcAddr();
   static std::string requestDescCcAddr(const DescCc &descCC);

public slots:
   void sendUnsignedPayin(const std::string& settlementId, const bs::network::UnsignedPayinData& unsignedPayinData);
   void sendSignedPayin(const std::string& settlementId, const BinaryData& signedPayin);
   void sendSignedPayout(const std::string& settlementId, const BinaryData& signedPayout);

   void sendCancelOnXBTTrade(const std::string& settlementId);
   void sendCancelOnCCTrade(const std::string& clOrdId);

   void findEmailHash(const std::string &email);

signals:
   void startLoginDone(bool success, const std::string &errorMsg);
   void authorizeDone(AuthorizeError error, const std::string &email);
   void getLoginResultDone(const BsClientLoginResult &result);

   void celerRecv(CelerAPI::CelerMessageType messageType, const std::string &data);
   // Register Blocksettle::Communication::ProxyTerminalPb::Response with qRegisterMetaType() if queued connection is needed
   void processPbMessage(const Blocksettle::Communication::ProxyTerminalPb::Response &message);

   void connected();
   void disconnected();
   void connectionFailed();

   void emailHashReceived(const std::string &email, const std::string &hash);
   void ccGenAddrUpdated(const BinaryData &ccGenAddrData);
   void accountStateChanged(bs::network::UserType userType, bool enabled);
   void feeRateReceived(float feeRate);
   void balanceLoaded();
   void balanceUpdated(const std::string &currency, double balance);

   void tradingStatusChanged(bool tradingEnabled);

private:
   using ProcessCb = std::function<void(const Blocksettle::Communication::ProxyTerminal::Response &response)>;
   using TimeoutCb = std::function<void()>;

   struct ActiveRequest
   {
      ProcessCb processCb;
      TimeoutCb timeoutCb;
   };

   // From DataConnectionListener
   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

   RequestId sendRequest(Blocksettle::Communication::ProxyTerminal::Request *request
      , std::chrono::milliseconds timeout, TimeoutCb timeoutCb, ProcessCb processCb = nullptr);
   void sendMessage(Blocksettle::Communication::ProxyTerminal::Request *request);

   void processStartLogin(const Blocksettle::Communication::ProxyTerminal::Response_StartLogin &response);
   void processAuthorize(const Blocksettle::Communication::ProxyTerminal::Response_Authorize &response);
   void processGetLoginResult(const Blocksettle::Communication::ProxyTerminal::Response_GetLoginResult &response);
   void processCeler(const Blocksettle::Communication::ProxyTerminal::Response_Celer &response);
   void processProxyPb(const Blocksettle::Communication::ProxyTerminal::Response_ProxyPb &response);
   void processGenAddrUpdated(const Blocksettle::Communication::ProxyTerminal::Response_GenAddrUpdated &response);
   void processUserStatusUpdated(const Blocksettle::Communication::ProxyTerminal::Response_UserStatusUpdated &response);
   void processUpdateFeeRate(const Blocksettle::Communication::ProxyTerminal::Response_UpdateFeeRate &response);
   void processBalanceUpdate(const Blocksettle::Communication::ProxyTerminal::Response_UpdateBalance &response);
   void processTradingEnabledStatus(bool tradingEnabled);

   RequestId newRequestId();

   std::shared_ptr<spdlog::logger> logger_;

   std::unique_ptr<DataConnection> connection_;

   std::map<RequestId, ActiveRequest> activeRequests_;
   RequestId lastRequestId_{};
   RequestId lastSignRequestId_{};

   bool balanceLoaded_{};
};

#endif
