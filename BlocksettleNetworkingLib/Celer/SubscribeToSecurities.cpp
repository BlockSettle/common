#include "SubscribeToSecurities.h"
#include "Celer/CommonUtils.h"
#include "NettyCommunication.pb.h"
#include "UpstreamMarketDataProto.pb.h"
#include "MarketDataRequestTypeProto.pb.h"
#include "MarketDataUpdateTypeProto.pb.h"
#include "DownstreamSecurityDefinitionProto.pb.h"

#include <QDate>

#include <spdlog/spdlog.h>

using namespace bs::celer;
using namespace com::celertech::baseserver::communication::protobuf;
using namespace com::celertech::marketmerchant::api::marketdata;
using namespace com::celertech::marketmerchant::api::securitydefinition;
using namespace com::celertech::marketmerchant::api::enums::marketdatarequesttype;
using namespace com::celertech::marketmerchant::api::enums::marketdataupdatetype;

SubscribeToSecurities::SubscribeToSecurities(const std::shared_ptr<spdlog::logger>& logger
   , const onSecuritiesSnapshotReceived &func)
 : CommandSequence("CelerSubscribeToSecurities",
      {
         { false, nullptr, &SubscribeToSecurities::subscribeFX },
         { true, &SubscribeToSecurities::process, nullptr }
      })
   , logger_(logger)
   , onSnapshotReceived_(func)
{}

bool SubscribeToSecurities::FinishSequence()
{
   if (onSnapshotReceived_ && !dictionary_.empty()) {
      onSnapshotReceived_(dictionary_);
   }
   return true;
}

CelerMessage SubscribeToSecurities::subscribeFX()
{
   MarketDataRequest request;
   request.set_marketdatarequestid(GetSequenceId());
   request.set_marketdatarequesttype(SNAPSHOT_PLUS_UPDATES);
   request.set_marketdataupdatetype(FULL_SNAPSHOT);

   CelerMessage message;
   message.messageType = CelerAPI::FindAllSecurityDefinitionsType;
   message.messageData = request.SerializeAsString();

   return message;
}

bool SubscribeToSecurities::process(const CelerMessage& message)
{
   MultiResponseMessage response;
   if (!response.ParseFromString(message.messageData)) {
      logger_->error("[CelerSubscribeToSecurities::process] failed to parse MultiResponseMessage");
      return false;
   }

   for (int i = 0; i < response.payload_size(); i++) {
      const ResponsePayload& payload = response.payload(i);
      auto payloadType = CelerAPI::GetMessageType(payload.classname());
      if (payloadType != CelerAPI::SecurityDefinitionDownstreamEventType) {
         logger_->error("[CelerSubscribeToSecurities::processFindSocketsResponse] invalid payload type {}", payload.classname());
         return false;
      }

      SecurityDefinitionDownstreamEvent securityDef;
      if (securityDef.ParseFromString(payload.contents())) {
         bs::network::SecurityDef security;
         security.assetType = bs::celer::fromCelerProductType(securityDef.producttype());
         dictionary_[securityDef.securityid()] = security;
         logger_->debug("[SecurityDef] {}: at={}({})", securityDef.securityid()
            , security.assetType, securityDef.producttype());
      }
      else {
         logger_->error("[CelerSubscribeToSecurities::process] failed to parse security definition");
         return false;
      }
   }

   return true;
}
