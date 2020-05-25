/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __XBT_TRADE_DATA_H__
#define __XBT_TRADE_DATA_H__

#include "BinaryData.h"
#include "XBTAmount.h"

#include <string>
#include <memory>
#include <QDateTime>

class CustomerAccount;

namespace bs {
   namespace network {
      enum class Subsystem : int;
   }
}

struct XBTTradeData
{
   bs::network::Subsystem subsystem{};

   std::string settlementId;

   std::string clOrderId;

   bool        requestorSellXBT{};

   std::shared_ptr<CustomerAccount> requestor;
   std::string requestorPublicKey;

   std::shared_ptr<CustomerAccount> dealer;
   std::string dealerPublicKey;

   bs::XBTAmount  xbtAmount;
   double         ccyAmount{};

   double      price{};

   std::string product;
   std::string ccyName;

   // TX from clients
   std::string unsignedPayin;
   BinaryData  signedPayin;
   BinaryData  signedPayout;

   BinaryData  payinHash;
   uint64_t    totalPayinFee = 0;

   QDateTime   tradeCreatedDateTime;

   std::string settlementAddress;

   // result of TX validation
   // (0; 100]
   int                        minScore = 0;
   std::vector<std::string>   rejectedInputs;
   std::string                scorechainUrl;

   // loadedFromReservation - true if trade was loaded from genoa.
   //                         means that after first check on that settlement address
   //                         reservation should be released if history is empty.
   bool loadedFromReservation = false;
   bool cashReserved = false;

   std::string lastStatus;

   std::map<std::string, BinaryData> preimageData;

   bool savedInHistory = false;

   enum class TradeState : int
   {
      StateUndefined = 0,
      UnsignedPayinRequested,
      UnsignedPayinProcessing,
      SignedPayoutRequested,
      SignedPayoutProcessing,
      SignedPayinRequested,
      SignedPayinProcessing,
      SettlementBroadcasting,
      SettlementMonitoring,
      TradeSettlementTimedOut,
      TradeCancelled
   };

   TradeState tradeState = TradeState::StateUndefined;

   bool payInBroadcastedByUser = false;
   bool payOutBroadcastedByUser = false;
};

using XBTTradePtr = std::shared_ptr<XBTTradeData>;

#endif // __XBT_TRADE_DATA_H__
