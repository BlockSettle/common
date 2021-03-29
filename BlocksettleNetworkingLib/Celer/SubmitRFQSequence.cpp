/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CommonUtils.h"
#include "SubmitRFQSequence.h"

#include "ProtobufUtils.h"
#include "UpstreamQuoteProto.pb.h"

#include <QDate>

#include <spdlog/spdlog.h>

using namespace bs::celer;
using namespace com::celertech::marketmerchant::api::quote;

SubmitRFQSequence::SubmitRFQSequence(const std::string& accountName
   , const bs::network::RFQ& rfq, const std::shared_ptr<spdlog::logger>& logger
   , bool debugPrintRFQ)
 : CommandSequence("CelerSubmitRFQSequence",
      {
         { false, nullptr, &SubmitRFQSequence::submitRFQ }
      })
   , accountName_(accountName)
   , rfq_(rfq)
   , logger_(logger)
   , debugPrintRFQ_(debugPrintRFQ)
{}


bool SubmitRFQSequence::FinishSequence()
{
   return true;
}

CelerMessage SubmitRFQSequence::submitRFQ()
{
   QuoteRequest request;

   auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());

   request.set_quoterequestid(rfq_.requestId);
   request.set_handlinginstruction(com::celertech::marketmerchant::api::enums::handlinginstruction::AUTOMATED_NO_BROKER);
   request.set_account(accountName_);
   request.set_authenticationaddress(rfq_.requestorAuthPublicKey);

   QuoteRequestGroup* group = request.add_quoterequestgroup();
   QuoteRequestLegGroup *leg = group->add_quoterequestleggroup();

   group->set_expiretimeinutcinmillis(timestamp.count() + 120 * 1000);

   group->set_assettype(bs::celer::toCeler(rfq_.assetType));
   group->set_producttype(bs::celer::toCelerProductType(rfq_.assetType));
   leg->set_settlementtype(bs::celer::toCelerSettlementType(rfq_.assetType));

   group->set_currency(rfq_.product);

   group->set_securitycode(rfq_.security);
   group->set_securityid(rfq_.security);

   leg->set_side(bs::celer::toCeler(rfq_.side));

   leg->set_qty(rfq_.quantity);

   leg->set_settlementdate(QDate::currentDate().toString(Qt::ISODate).toStdString());

   if (!rfq_.receiptAddress.empty()) {
      request.set_receiptaddress(rfq_.receiptAddress);
   }
   if (rfq_.assetType == bs::network::Asset::PrivateMarket) {
      group->set_cointransactioninput(rfq_.coinTxInput);
   }

   group->set_partyid("BLK_STANDARD");    // fixed on Celer side for this value now

   CelerMessage message;
   message.messageType = CelerAPI::QuoteUpstreamType;
   message.messageData = request.SerializeAsString();

   if (debugPrintRFQ_) {
      logger_->debug("RFQ: {}", ProtobufUtils::toJsonCompact(request));
   }

   return message;
}
