#ifndef CHATCOMMONTYPES_H
#define CHATCOMMONTYPES_H

#include <inttypes.h>
#include <string>

#include <QString>

namespace bs {
   namespace network {

      struct ChatOTCSide {
         enum Type {
            Undefined,
            Buy,
            Sell
         };

         static const char *toString(Type side) {
            switch (side) {
               case Buy:   return QT_TR_NOOP("BUY");
               case Sell:  return QT_TR_NOOP("SELL");
               default:    return "unknown";
            }
         }

      };

      struct OTCPriceRange
      {
         uint64_t lower;
         uint64_t upper;
      };

      struct OTCQuantityRange
      {
         uint64_t lower;
         uint64_t upper;
      };

      struct OTCRangeID
      {
         enum class Type : int
         {
            Range1_5,
            Range5_10,
            Range10_50,
            Range50_100,
            Range100_250,
            Range250plus
         };

         static std::string toString(const Type& range)
         {
            switch(range) {
            case Type::Range1_5:
               return "1-5";
            case Type::Range5_10:
               return "5-10";
            case Type::Range10_50:
               return "10-50";
            case Type::Range50_100:
               return "50-100";
            case Type::Range100_250:
               return "100-250";
            case Type::Range250plus:
               return "250+";
            default:
#ifndef NDEBUG
               throw std::runtime_error("invalid range type");
#endif
               return "invalid range";

            }
         }

         static OTCQuantityRange toQuantityRange(const Type& range)
         {
            switch(range) {
            case Type::Range1_5:
               return OTCQuantityRange{1, 5};
            case Type::Range5_10:
               return OTCQuantityRange{5, 10};
            case Type::Range10_50:
               return OTCQuantityRange{10,50};
            case Type::Range50_100:
               return OTCQuantityRange{50, 100};
            case Type::Range100_250:
               return OTCQuantityRange{100, 250};
            case Type::Range250plus:
               return OTCQuantityRange{250, 1000};
            default:
#ifndef NDEBUG
               throw std::runtime_error("invalid range type");
#endif
               return OTCQuantityRange{1, 1};
            }
         }
      };

      struct OTCRequest
      {
         ChatOTCSide::Type    side;
         OTCRangeID::Type     amountRange;
      };

      struct OTCResponse
      {
         std::string     serverRequestId;
         std::string     requestorId;
         std::string     initialTargetId;

         OTCPriceRange     priceRange;
         OTCQuantityRange  quantityRange;
      };

      enum class OTCRequestRejectReason
      {
         CounterpartyOffline,
         CounterpartyNotFound,
         // InvalidTarget - ATM returned when trying to send to self
         InvalidTarget,
         RequestLimitToTargetExceeded,
         RequestsAmountExceeded,
         RequestAlreadyExpired
      };

      enum class OTCResponseRejectReason
      {
         // RequestNotAvailableForReply - request was pulled, closed or expired
         RequestNotAvailableForReply,
         // InvalidTarget - ATM returned when trying to reply to self
         InvalidTarget,
         // RequestAlreadyResponded - responder already have active response to that request
         RequestAlreadyResponded,
         // RequestResponseLimitExceeded - request have reached max number of
         // replies ( reject reeason for requests to common room)
         RequestResponseLimitExceeded
      };

      enum class OTCUpdateRejectReason
      {
         // TradingClosed - response or request were closed already
         TradingClosed,
         // TradeAlreadyAccepted - could not update trade that was accepted
         TradeAlreadyAccepted,
         // NoUpdateFromRequestorReceived - requestor shoudl send first update
         NoUpdateFromRequestorReceived,
         // PriceNotInrange price in update not in initial range
         PriceNotInrange,
         // AmountNotInRange amount not in initial range
         AmountNotInRange,
         // AccessDenied - update sender is neither requestor or responder
         AccessDenied
      };
   }
}

#endif // CHATCOMMONTYPES_H
