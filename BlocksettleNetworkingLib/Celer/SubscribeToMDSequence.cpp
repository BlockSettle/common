#include "SubscribeToMDSequence.h"
#include "Celer/CommonUtils.h"
#include "UpstreamMarketDataProto.pb.h"
#include "UpstreamMarketStatisticProto.pb.h"
#include "MarketDataRequestTypeProto.pb.h"
#include "MarketDataUpdateTypeProto.pb.h"

#include <QDate>

#include <spdlog/spdlog.h>

using namespace bs::celer;
using namespace com::celertech::marketmerchant::api::marketdata;
using namespace com::celertech::marketmerchant::api::marketstatistic;
using namespace com::celertech::marketmerchant::api::enums::marketdatarequesttype;
using namespace com::celertech::marketmerchant::api::enums::marketdataupdatetype;

SubscribeToMDSequence::SubscribeToMDSequence(const std::string& currencyPair
   , bs::network::Asset::Type at, const std::shared_ptr<spdlog::logger>& logger)
 : CommandSequence("CelerSubscribeToMDSequence",
      {
         { false, nullptr, &SubscribeToMDSequence::subscribeToMD}
      })
   , currencyPair_(currencyPair)
   , assetType_(at)
   , logger_(logger)
{}

bool SubscribeToMDSequence::FinishSequence()
{
   return true;
}

CelerMessage SubscribeToMDSequence::subscribeToMD()
{
   MarketDataRequest request;
   reqId_ = GetUniqueId();

   request.set_marketdatarequestid(reqId_);
   request.set_marketdatarequesttype(SNAPSHOT_PLUS_UPDATES);
   request.set_marketdataupdatetype(FULL_SNAPSHOT);
   request.set_marketdepth(0);
   request.set_securitycode(currencyPair_);
   request.set_securityid(currencyPair_);
   request.set_streamid("BLK_STANDARD");
   request.set_assettype(bs::celer::toCeler(assetType_));
   request.set_producttype(bs::celer::toCelerProductType(assetType_));
//   logger_->debug("MD req: {}", request.DebugString());

   if (assetType_ == bs::network::Asset::SpotFX) {
      request.set_settlementtype("SP");
   }

   CelerMessage message;
   message.messageType = CelerAPI::MarketDataRequestType;
   message.messageData = request.SerializeAsString();

   return message;
}
