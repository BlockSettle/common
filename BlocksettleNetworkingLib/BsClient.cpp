/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BsClient.h"

#include <spdlog/spdlog.h>
#include <QTimer>

#include "FutureValue.h"
#include "ProtobufUtils.h"
#include "WsDataConnection.h"

#include "bs_proxy_terminal.pb.h"
#include "bs_proxy_terminal_pb.pb.h"

using namespace Blocksettle::Communication;
using namespace Blocksettle::Communication::ProxyTerminal;

namespace {

   const auto kServerError = QObject::tr("Server error").toStdString();
   const auto kTimeoutError = QObject::tr("Request timeout").toStdString();

   template<class T>
   static T errorResponse(const std::string &errorMsg)
   {
      T result;
      result.errorMsg = errorMsg;
      return result;
   }

}

BsClient::BsClient(const std::shared_ptr<spdlog::logger> &logger
   , QObject *parent)
   : QObject(parent)
   , logger_(logger)
{
}

BsClient::~BsClient()
{
   // Stop receiving events from DataConnectionListener before BsClient is partially destroyed
   connection_.reset();
}

void BsClient::setConnection(std::unique_ptr<DataConnection> connection)
{
   assert(!connection_);
   connection_ = std::move(connection);
}

void BsClient::startLogin(const std::string &email)
{
   Request request;
   auto d = request.mutable_start_login();
   d->set_email(email);

   sendRequest(&request, std::chrono::seconds(10), [this] {
      emit startLoginDone(false, kTimeoutError);
   });
}

void BsClient::authorize(const std::string &apiKey)
{
   Request request;
   auto d = request.mutable_authorize();
   d->set_api_key(apiKey);

   sendRequest(&request, std::chrono::seconds(10), [this] {
      emit authorizeDone(AuthorizeError::Timeout, {});
   });
}

void BsClient::sendPbMessage(std::string data)
{
   Request request;
   auto d = request.mutable_proxy_pb();
   d->set_data(std::move(data));
   sendMessage(&request);
}

void BsClient::sendCancelOnCCTrade(const std::string& clOrdId)
{
   SPDLOG_LOGGER_DEBUG(logger_, "send cancel on CC trade {}", clOrdId);

   ProxyTerminalPb::Request request;

   auto cancelMessage = request.mutable_cc_cancel();
   cancelMessage->set_client_order_id(clOrdId);

   sendPbMessage(request.SerializeAsString());
}

void BsClient::sendCancelOnXBTTrade(const std::string& settlementId)
{
   SPDLOG_LOGGER_DEBUG(logger_, "send cancel on XBT trade {}", settlementId);

   ProxyTerminalPb::Request request;

   auto cancelMessage = request.mutable_xbt_cancel();
   cancelMessage->set_settlement_id(settlementId);

   sendPbMessage(request.SerializeAsString());
}

void BsClient::sendUnsignedPayin(const std::string& settlementId
   , const bs::network::UnsignedPayinData& unsignedPayinData)
{
   SPDLOG_LOGGER_DEBUG(logger_, "send unsigned payin {}", settlementId);

   ProxyTerminalPb::Request request;

   auto data = request.mutable_unsigned_payin();
   data->set_settlement_id(settlementId);
   data->set_unsigned_payin(unsignedPayinData.unsignedPayin);

   sendPbMessage(request.SerializeAsString());
}

void BsClient::sendSignedPayin(const std::string& settlementId, const BinaryData& signedPayin)
{
   SPDLOG_LOGGER_DEBUG(logger_, "send signed payin {}", settlementId);

   ProxyTerminalPb::Request request;

   auto data = request.mutable_signed_payin();
   data->set_settlement_id(settlementId);
   data->set_signed_payin(signedPayin.toBinStr());

   sendPbMessage(request.SerializeAsString());
}

void BsClient::sendSignedPayout(const std::string& settlementId, const BinaryData& signedPayout)
{
   SPDLOG_LOGGER_DEBUG(logger_, "send signed payout {}", settlementId);

   ProxyTerminalPb::Request request;

   auto data = request.mutable_signed_payout();
   data->set_settlement_id(settlementId);
   data->set_signed_payout(signedPayout.toBinStr());

   sendPbMessage(request.SerializeAsString());
}

void BsClient::findEmailHash(const std::string &email)
{
   Request request;
   auto d = request.mutable_get_email_hash();
   d->set_email(email);

   auto timeoutCb = [this, email] {
      SPDLOG_LOGGER_ERROR(logger_, "getting email hash timed out for address: {}", email);
      emit emailHashReceived(email, "");
   };

   auto processCb = [this, email](const Blocksettle::Communication::ProxyTerminal::Response &response) {
      const auto &hash = response.get_email_hash().hash();
      SPDLOG_LOGGER_DEBUG(logger_, "got email hash address: {}, hash: {}", email, hash);
      emit emailHashReceived(email, hash);
   };

   sendRequest(&request, std::chrono::seconds(10), std::move(timeoutCb), std::move(processCb));
}

void BsClient::cancelLogin()
{
   Request request;
   request.mutable_cancel_login();
   sendMessage(&request);
}

void BsClient::getLoginResult()
{
   Request request;
   request.mutable_get_login_result();

   // Add some time to be able get timeout error from the server
   sendRequest(&request, autheidLoginTimeout() + std::chrono::seconds(3), [this] {
      BsClientLoginResult result;
      result.errorMsg = kTimeoutError;
      emit getLoginResultDone(result);
   });
}

void BsClient::logout()
{
   Request request;
   request.mutable_logout();
   sendMessage(&request);
}

void BsClient::celerSend(CelerAPI::CelerMessageType messageType, const std::string &data)
{
   Request request;
   auto d = request.mutable_celer();
   d->set_message_type(int(messageType));
   d->set_data(data);
   sendMessage(&request);
}

void BsClient::signAuthAddress(const bs::Address address, const SignCb &cb)
{
   cancelActiveSign();

   auto processCb = [this, cb, address](const Response &response) {
      if (!response.has_sign_auth_address()) {
         SPDLOG_LOGGER_ERROR(logger_, "unexpected response from BsProxy, expected sign_auth_address response");
         cb(errorResponse<SignResponse>(kServerError));
         return;
      }

      const auto &d = response.sign_auth_address();
      SignResponse result;
      result.success = d.basic().success();
      result.errorMsg = d.basic().error_msg();
      result.userCancelled = d.user_cancelled();
      cb(result);
   };

   auto timeoutCb = [cb] {
      cb(errorResponse<SignResponse>(kTimeoutError));
   };

   Request request;
   auto d = request.mutable_sign_auth_address();
   d->set_address(address.display());
   lastSignRequestId_ = sendRequest(&request, autheidAuthAddressTimeout() + std::chrono::seconds(5), std::move(timeoutCb), std::move(processCb));
}

void BsClient::confirmAuthAddress(const bs::Address address, const BsClient::AuthConfirmCb &cb)
{
   auto processCb = [this, cb, address](const Response &response) {
      if (!response.has_confirm_auth_submit()) {
         SPDLOG_LOGGER_ERROR(logger_, "unexpected response from BsProxy, expected confirm_auth_submit response");
         cb(bs::error::AuthAddressSubmitResult::ServerError);
         return;
      }

      const auto &d = response.confirm_auth_submit();

      cb(static_cast<bs::error::AuthAddressSubmitResult>(d.status_code()));
   };

   auto timeoutCb = [cb] {
      cb(bs::error::AuthAddressSubmitResult::RequestTimeout);
   };

   Request request;
   auto d = request.mutable_confirm_auth_address();
   d->set_address(address.display());
   sendRequest(&request, std::chrono::seconds(10), std::move(timeoutCb), std::move(processCb));
}

void BsClient::submitCcAddress(const bs::Address address, uint32_t seed, const std::string &ccProduct, const BasicCb &cb)
{
   auto processCb = [this, cb, address](const Response &response) {
      if (!response.has_submit_cc_address()) {
         SPDLOG_LOGGER_ERROR(logger_, "unexpected response from BsProxy, expected submit_cc_address response");
         cb(errorResponse<BasicResponse>(kServerError));
         return;
      }

      const auto &d = response.submit_cc_address();
      BasicResponse result;
      result.success = d.success();
      result.errorMsg = d.error_msg();
      cb(result);
   };

   auto timeoutCb = [cb] {
      cb(errorResponse<BasicResponse>(kTimeoutError));
   };

   Request request;
   auto d = request.mutable_submit_cc_address();
   d->mutable_address()->set_address(address.display());
   d->set_seed(seed);
   d->set_cc_product(ccProduct);
   sendRequest(&request, std::chrono::seconds(10), std::move(timeoutCb), std::move(processCb));
}

void BsClient::signCcAddress(const bs::Address address, const BsClient::SignCb &cb)
{
   cancelActiveSign();

   auto processCb = [this, cb, address](const Response &response) {
      if (!response.has_sign_cc_address()) {
         SPDLOG_LOGGER_ERROR(logger_, "unexpected response from BsProxy, expected sign_cc_address response");
         cb(errorResponse<SignResponse>(kServerError));
         return;
      }

      const auto &d = response.sign_cc_address();
      SignResponse result;
      result.success = d.basic().success();
      result.errorMsg = d.basic().error_msg();
      result.userCancelled = d.user_cancelled();
      cb(result);
   };

   auto timeoutCb = [cb] {
      cb(errorResponse<SignResponse>(kTimeoutError));
   };

   Request request;
   auto d = request.mutable_sign_cc_address();
   d->set_address(address.display());
   lastSignRequestId_ = sendRequest(&request, autheidCcAddressTimeout() + std::chrono::seconds(5), std::move(timeoutCb), std::move(processCb));
}

void BsClient::confirmCcAddress(const bs::Address address, const BsClient::BasicCb &cb)
{
   auto processCb = [this, cb, address](const Response &response) {
      if (!response.has_confirm_cc_address()) {
         SPDLOG_LOGGER_ERROR(logger_, "unexpected response from BsProxy, expected confirm_cc_address response");
         cb(errorResponse<BasicResponse>(kServerError));
         return;
      }

      const auto &d = response.confirm_cc_address();
      BasicResponse result;
      result.success = d.success();
      result.errorMsg = d.error_msg();
      cb(result);
   };

   auto timeoutCb = [cb] {
      cb(errorResponse<BasicResponse>(kTimeoutError));
   };

   Request request;
   auto d = request.mutable_confirm_cc_address();
   d->set_address(address.display());
   sendRequest(&request, std::chrono::seconds(10), std::move(timeoutCb), std::move(processCb));
}

void BsClient::cancelActiveSign()
{
   // Proxy will not send response for cancelled sign request.
   // And so cancelled sign will result in timeout callback calling.
   // This is a workaround for that problem.

   if (lastSignRequestId_ == 0) {
      return;
   }

   size_t count = activeRequests_.erase(lastSignRequestId_);
   lastSignRequestId_ = 0;

   if (count > 0) {
      Request request;
      request.mutable_cancel_sign();
      sendMessage(&request);
   }
}

// static
std::chrono::seconds BsClient::autheidLoginTimeout()
{
   return std::chrono::seconds(60);
}

// static
std::chrono::seconds BsClient::autheidAuthAddressTimeout()
{
   return std::chrono::seconds(30);
}

// static
std::chrono::seconds BsClient::autheidCcAddressTimeout()
{
   return std::chrono::seconds(90);
}

// static
std::string BsClient::requestTitleAuthAddr()
{
   return "Authentication Address";
}

// static
std::string BsClient::requestDescAuthAddr(const bs::Address &address)
{
   return fmt::format("Authentication Address: {}", address.display());
}

// static
std::string BsClient::requestTitleCcAddr()
{
   return "Equity Token issuance";
}

// static
std::string BsClient::requestDescCcAddr(const DescCc &descCC)
{
   return fmt::format("Product: {}", descCC.ccProduct);
}

void BsClient::OnDataReceived(const std::string &data)
{
   auto response = std::make_shared<Response>();
   bool result = response->ParseFromString(data);
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "can't parse response from BS proxy");
      return;
   }

   QMetaObject::invokeMethod(this, [this, response] {
      if (response->request_id() != 0) {
         auto it = activeRequests_.find(response->request_id());
         if (it == activeRequests_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "discard late response from BsProxy (requestId: {})", response->request_id());
            return;
         }

         if (it->second.processCb) {
            it->second.processCb(*response);
         }

         activeRequests_.erase(it);
      }

      switch (response->data_case()) {
         case Response::kStartLogin:
            processStartLogin(response->start_login());
            return;
         case Response::kAuthorize:
            processAuthorize(response->authorize());
            return;
         case Response::kGetLoginResult:
            processGetLoginResult(response->get_login_result());
            return;
         case Response::kCeler:
            processCeler(response->celer());
            return;
         case Response::kProxyPb:
            processProxyPb(response->proxy_pb());
            return;
         case Response::kGenAddrUpdated:
            processGenAddrUpdated(response->gen_addr_updated());
            return;
         case Response::kUserStatusUpdated:
            processUserStatusUpdated(response->user_status_updated());
            return;
         case Response::kUpdateFeeRate:
            processUpdateFeeRate(response->update_fee_rate());
            return;
         case Response::kUpdateBalance:
            processBalanceUpdate(response->update_balance());
            return;
         case Response::kTradingEnabled:
            processTradingEnabledStatus(response->trading_enabled());
            return;

         case Response::kGetEmailHash:
         case Response::kSignAuthAddress:
         case Response::kConfirmAuthSubmit:
         case Response::kSubmitCcAddress:
         case Response::kSignCcAddress:
         case Response::kConfirmCcAddress:
            // Will be handled from processCb
            return;

         case Response::DATA_NOT_SET:
            SPDLOG_LOGGER_ERROR(logger_, "invalid response from proxy");
            return;
      }

      SPDLOG_LOGGER_CRITICAL(logger_, "unknown response was detected!");
   });
}

void BsClient::OnConnected()
{
   emit connected();
}

void BsClient::OnDisconnected()
{
   emit disconnected();
}

void BsClient::OnError(DataConnectionListener::DataConnectionError errorCode)
{
   SPDLOG_LOGGER_ERROR(logger_, "connection to bs proxy failed ({})", int(errorCode));

   emit connectionFailed();
}

BsClient::RequestId BsClient::sendRequest(Request *request, std::chrono::milliseconds timeout
   , TimeoutCb timeoutCb, ProcessCb processCb)
{
   const int64_t requestId = newRequestId();
   ActiveRequest activeRequest;
   activeRequest.processCb = std::move(processCb);
   activeRequest.timeoutCb = std::move(timeoutCb);
   activeRequests_.emplace(requestId, std::move(activeRequest));

   QTimer::singleShot(timeout, this, [this, requestId] {
      auto it = activeRequests_.find(requestId);
      if (it == activeRequests_.end()) {
         return;
      }

      // Erase iterator before calling callback!
      // Callback could be be blocking and iterator might become invalid after callback return.
      auto callback = std::move(it->second.timeoutCb);
      activeRequests_.erase(it);

      // Callback could be blocking
      callback();
   });

   request->set_request_id(requestId);
   sendMessage(request);

   return requestId;
}

void BsClient::sendMessage(Request *request)
{
   connection_->send(request->SerializeAsString());
}

void BsClient::processStartLogin(const Response_StartLogin &response)
{
   bool success = response.error().error_code() == 0;
   emit startLoginDone(success, response.error().message());
}

void BsClient::processAuthorize(const Response_Authorize &response)
{
   if (response.error() || response.email().empty()) {
      AuthorizeError error{};
      switch (response.error()) {
         case bs::types::API_KEY_ERROR_UNKNOWN_KEY:
            error = AuthorizeError::UnknownApiKey;
            break;
         case bs::types::API_KEY_ERROR_UNKNOWN_IP_ADDR:
            error = AuthorizeError::UnknownIpAddr;
            break;
         default:
            error = AuthorizeError::ServerError;
            break;
      }
      emit authorizeDone(error, {});
      return;
   }

   emit authorizeDone(AuthorizeError::NoError, response.email());
}

void BsClient::processGetLoginResult(const Response_GetLoginResult &response)
{
   BsClientLoginResult result;
   result.status = static_cast<AutheIDClient::ErrorType>(response.error().error_code());
   result.errorMsg = response.error().message();
   result.userType = static_cast<bs::network::UserType>(response.user_type());
   result.celerLogin = response.celer_login();
   result.chatTokenData = BinaryData::fromString(response.chat_token_data());
   result.chatTokenSign = BinaryData::fromString(response.chat_token_sign());
   result.authAddressesSigned = BinaryData::fromString(response.auth_addresses_signed());
   result.ccAddressesSigned = BinaryData::fromString(response.cc_addresses_signed());
   result.enabled = response.enabled();
   result.feeRatePb = response.fee_rate();
   result.tradeSettings = bs::TradeSettings::fromPb(response.trade_settings());
   emit getLoginResultDone(result);
}

void BsClient::processCeler(const Response_Celer &response)
{
   auto messageType = CelerAPI::CelerMessageType(response.message_type());
   if (!CelerAPI::isValidMessageType(messageType)) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid celer msg type received: {}", int(messageType));
      return;
   }

   emit celerRecv(messageType, response.data());
}

void BsClient::processProxyPb(const Response_ProxyPb &response)
{
   Blocksettle::Communication::ProxyTerminalPb::Response message;
   bool result = message.ParseFromString(response.data());
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid PB message");
      return;
   }
   emit processPbMessage(message);
}

void BsClient::processGenAddrUpdated(const Response_GenAddrUpdated &response)
{
   SPDLOG_LOGGER_DEBUG(logger_, "new CC gen addresses updated");
   emit ccGenAddrUpdated(BinaryData::fromString(response.cc_addresses_signed()));
}

void BsClient::processUserStatusUpdated(const Response_UserStatusUpdated &response)
{
   SPDLOG_LOGGER_DEBUG(logger_, "user account state changed, new user type: {}, enabled: {}"
      , response.user_type(), response.enabled());
   emit accountStateChanged(static_cast<bs::network::UserType>(response.user_type()), response.enabled());
}

void BsClient::processUpdateFeeRate(const Response_UpdateFeeRate &response)
{
   emit feeRateReceived(response.fee_rate());
}

void BsClient::processBalanceUpdate(const Response_UpdateBalance &response)
{
   for (const auto &balance : response.balances()) {
      emit balanceUpdated(balance.currency(), balance.balance());
   }
   if (!balanceLoaded_) {
      balanceLoaded_ = true;
      emit balanceLoaded();
   }
}

BsClient::RequestId BsClient::newRequestId()
{
   lastRequestId_ += 1;
   return lastRequestId_;
}

void BsClient::processTradingEnabledStatus(bool tradingEnabled)
{
   emit tradingStatusChanged(tradingEnabled);
}
