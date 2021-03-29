/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __BS_COMMON_TYPES_H__
#define __BS_COMMON_TYPES_H__

#include <QDateTime>
#include <QObject>
#include <QString>
#include "Address.h"

#ifndef NDEBUG
#include <stdexcept>
#endif

namespace bs {
   namespace network {

      enum class UserType : int
      {
         // Invalid value
         Undefined,

         // Trading + XBT responses
         Dealing,

         // Market + XBT requests + OTC
         Trading,

         // Chat + private market trades
         Market,

         // Chat only access (account is not registered on Genoa)
         Chat,

         LastValue = Chat
      };

      static_assert(static_cast<int>(UserType::Undefined) == 0, "First value should be 0");

      bool isTradingEnabled(UserType);

      struct Side {
         enum Type {
            Undefined,
            Buy,
            Sell
         };

         static const char *toString(Type side);
         static const char *responseToString(Type side);

         static Type invert(Type side);
      };


      struct Asset {
         enum Type {
            Undefined,
            first,
            SpotFX = first,
            SpotXBT,
            PrivateMarket,
            DeliverableFutures,
            CashSettledFutures,
            last
         };

         static const char *toString(Type at);

         static bool isSpotType(const Type type);
         static bool isFuturesType(const Type type);
      };


      struct RFQ
      {
         std::string requestId;
         std::string security;
         std::string product;

         Asset::Type assetType{Asset::Type::Undefined};
         Side::Type  side;

         double quantity;
         // requestorAuthPublicKey - public key HEX encoded
         std::string requestorAuthPublicKey;
         std::string receiptAddress;
         std::string coinTxInput;

         bool isXbtBuy() const;
      };


      struct Quote
      {
         double price;
         double quantity;
         std::string requestId;
         std::string quoteId;
         std::string security;
         std::string product;

         // requestorAuthPublicKey - public key HEX encoded
         std::string requestorAuthPublicKey;
         // dealerAuthPublicKey - public key HEX encoded
         std::string dealerAuthPublicKey;
         // settlementId - HEX encoded
         std::string settlementId;
         std::string dealerTransaction;

         Side::Type  side;
         Asset::Type assetType;

         enum QuotingType {
            Automatic,
            Manual,
            Direct,
            Indicative,
            Tradeable
         };
         QuotingType quotingType;

         QDateTime   expirationTime;
         int         timeSkewMs;
         uint64_t    celerTimestamp;
      };


      struct Order
      {
         std::string clOrderId;
         QString exchOrderId;
         std::string quoteId;

         QDateTime dateTime;
         std::string security;
         std::string product;
         BinaryData  settlementId;
         std::string reqTransaction;
         std::string dealerTransaction;
         std::string pendingStatus;
         double quantity{};
         double leavesQty{};
         double price{};
         double avgPx{};

         Side::Type  side{};
         Asset::Type assetType{};

         enum Status {
            New,
            Pending,
            Failed,
            Filled
         };
         Status status{};
         std::string info;
      };


      struct FutureRequest
      {
         bs::XBTAmount amount;
         double price{};
         bs::network::Side::Type side{};
         bs::network::Asset::Type type{};
      };


      struct SecurityDef {
         Asset::Type assetType;
      };


      struct QuoteReqNotification
      {
         double quantity{};
         std::string quoteRequestId;
         std::string security;
         std::string product;
         std::string requestorAuthPublicKey;
         std::string sessionToken;
         std::string party;
         std::string settlementId;
         std::string requestorRecvAddress;

         Side::Type  side{};
         Asset::Type assetType{};

         enum Status {
            StatusUndefined,
            Withdrawn,
            PendingAck,
            Replied,
            Rejected,
            TimedOut
         };
         Status status{};

         uint64_t    expirationTime;
         int         timeSkewMs{};
         uint64_t    timestamp{};

         bool empty() const { return quoteRequestId.empty(); }
      };


      struct QuoteNotification
      {
         std::string authKey;
         std::string reqAuthKey;
         std::string settlementId;
         std::string sessionToken;
         std::string quoteRequestId;
         std::string security;
         std::string product;
         std::string transactionData;
         std::string receiptAddress;
         Asset::Type assetType;
         Side::Type  side;
         int         validityInS{};

         double      price{};
         double      quantity{};
         double      bidFwdPts{};   // looks like this field and all ones below are not used
         double      bidContraQty{};

         double      offerFwdPts{};
         double      offerContraQty{};

         QuoteNotification() : validityInS(120) {}
         QuoteNotification(const bs::network::QuoteReqNotification &qrn, const std::string &_authKey, double price
            , const std::string &txData);
      };

      struct MDInfo {
         double   bidPrice{};
         double   askPrice{};
         double   lastPrice{};

         void merge(const MDInfo &other);
      };

      struct MDField;
      using MDFields = std::vector<MDField>;

      struct MDField
      {
         enum Type {
            Unknown,
            PriceBid,
            PriceOffer,
            PriceMid,
            PriceOpen,
            PriceClose,
            PriceHigh,
            PriceLow,
            PriceSettlement,
            TurnOverQty,
            VWAP,
            PriceLast,
            PriceBestBid,
            PriceBestOffer,
            DailyVolume,
            Reject,
            MDTimestamp
         };
         Type     type;
         double   value;
         QString  levelQuantity;

         static MDField get(const MDFields &fields, Type type);
         static MDInfo  get(const MDFields &fields);
         // used for bid/offer only
         bool           isIndicativeForFutures() const;
      };


      struct CCSecurityDef
      {
         std::string    securityId;
         std::string    product;
         bs::Address    genesisAddr;
         uint64_t       nbSatoshis;
      };


      const std::string XbtCurrency = "XBT";

      // fx and xbt
      struct NewTrade
      {
         std::string product;
         double      price;
         double      amount;
         uint64_t    timestamp;
      };

      struct NewPMTrade
      {
         double      price;
         uint64_t    amount;
         std::string product;
         uint64_t    timestamp;
      };

      enum class Subsystem : int
      {
         Celer = 0,
         Otc = 1,

         First = Celer,
         Last = Otc,
      };

      // for celer and OTC trades
      struct UnsignedPayinData
      {
         std::string    unsignedPayin;
      };

   }  //namespace network
}  //namespace bs

//TODO: remove below declarations (no Qt signals/slots in common)
Q_DECLARE_METATYPE(bs::network::Asset::Type)
Q_DECLARE_METATYPE(bs::network::Quote)
Q_DECLARE_METATYPE(bs::network::Order)
Q_DECLARE_METATYPE(bs::network::SecurityDef)
Q_DECLARE_METATYPE(bs::network::QuoteReqNotification)
Q_DECLARE_METATYPE(bs::network::QuoteNotification)
Q_DECLARE_METATYPE(bs::network::MDField)
Q_DECLARE_METATYPE(bs::network::MDFields)
Q_DECLARE_METATYPE(bs::network::CCSecurityDef)
Q_DECLARE_METATYPE(bs::network::NewTrade)
Q_DECLARE_METATYPE(bs::network::NewPMTrade)
Q_DECLARE_METATYPE(bs::network::UnsignedPayinData)

#endif //__BS_COMMON_TYPES_H__
