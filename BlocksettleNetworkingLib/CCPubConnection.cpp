#include "CCPubConnection.h"

#include "ConnectionManager.h"
#include "RequestReplyCommand.h"
#include "ZmqSecuredDataConnection.h"

#include "bs_communication.pb.h"

#include <spdlog/spdlog.h>

using namespace Blocksettle::Communication;

CCPubConnection::CCPubConnection(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ConnectionManager>& connectionManager)
 : logger_{logger}
 , connectionManager_{connectionManager}
{}

bool CCPubConnection::LoadCCDefinitionsFromPub()
{
   GetCCGenesisAddressesRequest genAddrReq;
   RequestPacket  request;

   genAddrReq.set_networktype(IsTestNet()
      ? AddressNetworkType::TestNetType
      : AddressNetworkType::MainNetType);

   if (currentRev_ > 0) {
      genAddrReq.set_hasrevision(currentRev_);
   }

   request.set_requesttype(GetCCGenesisAddressesType);
   request.set_requestdata(genAddrReq.SerializeAsString());

   return SubmitRequestToPB("get_cc_gen_list", request.SerializeAsString());
}

bool CCPubConnection::SubmitRequestToPB(const std::string& name, const std::string& data)
{
   const auto connection = connectionManager_->CreateZMQBIP15XDataConnection();
   cmdPuB_ = std::make_shared<RequestReplyCommand>(name, connection, logger_);

   cmdPuB_->SetReplyCallback([this](const std::string& data) {
      OnDataReceived(data);
      cmdPuB_->CleanupCallbacks();
      return true;
   });

   cmdPuB_->SetErrorCallback([this](const std::string& message) {
      logger_->error("[CCPubConnection::{}] error callback: {}", cmdPuB_->GetName(), message);
      cmdPuB_->CleanupCallbacks();
   });

   if (!cmdPuB_->ExecuteRequest(GetPuBHost(), GetPuBPort(), data, true)) {
      logger_->error("[CCPubConnection::SubmitRequestToPB] failed to send request {}", name);
      return false;
   }

   return true;
}

void CCPubConnection::OnDataReceived(const std::string& data)
{
   if (data.empty()) {
      return;
   }
   ResponsePacket response;

   if (!response.ParseFromString(data)) {
      logger_->error("[CCPubConnection::OnDataReceived] failed to parse response from public bridge");
      return;
   }

   bool sigVerified = false;

   if (!response.has_datasignature()) {
      logger_->warn("[CCPubConnection::OnDataReceived] Public bridge response of type {} has no signature!"
         , static_cast<int>(response.responsetype()));
   } else {
      sigVerified = VerifySignature(response.responsedata(), response.datasignature());
      if (!sigVerified) {
         logger_->error("[CCPubConnection::OnDataReceived] Response signature verification failed - response {} dropped"
            , static_cast<int>(response.responsetype()));
         return;
      }
   }

   switch (response.responsetype()) {
   case RequestType::GetCCGenesisAddressesType:
      ProcessGenAddressesResponse(response.responsedata(), sigVerified, response.datasignature());
      break;
   case RequestType::SubmitCCAddrInitialDistribType:
      ProcessSubmitAddrResponse(response.responsedata());
      break;
   case RequestType::ErrorMessageResponseType:
      ProcessErrorResponse(response.responsedata());
      break;
   default:
      logger_->error("[CCPubConnection::OnDataReceived] unrecognized response type from public bridge: {}", response.responsetype());
      break;
   }
}

void CCPubConnection::ProcessErrorResponse(const std::string& responseString) const
{
   ErrorMessageResponse response;
   if (!response.ParseFromString(responseString)) {
      logger_->error("[CCPubConnection::ProcessErrorResponse] failed to parse error message response");
      return;
   }

   logger_->error("[CCPubConnection::ProcessErrorResponse] error message from public bridge: {}", response.errormessage());
}
