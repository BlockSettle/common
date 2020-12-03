/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CommonTypes.h"
#include "TradesVerification.h"

Q_DECLARE_METATYPE(bs::PayoutSignatureType)

using namespace bs::network;

class CommonTypesMetaRegistration
{
public:
   CommonTypesMetaRegistration()
   {
      qRegisterMetaType<bs::network::Asset::Type>("AssetType");
      qRegisterMetaType<bs::network::Quote>("Quote");
      qRegisterMetaType<bs::network::Order>("Order");
      qRegisterMetaType<bs::network::SecurityDef>("SecurityDef");
      qRegisterMetaType<bs::network::QuoteReqNotification>("QuoteReqNotification");
      qRegisterMetaType<bs::network::QuoteNotification>("QuoteNotification");
      qRegisterMetaType<bs::network::MDField>("MDField");
      qRegisterMetaType<bs::network::MDFields>("MDFields");
      qRegisterMetaType<bs::network::CCSecurityDef>("CCSecurityDef");
      qRegisterMetaType<bs::network::NewTrade>("NewTrade");
      qRegisterMetaType<bs::network::NewPMTrade>("NewPMTrade");
      qRegisterMetaType<bs::network::UnsignedPayinData>();
      qRegisterMetaType<bs::PayoutSignatureType>();
   }

   ~CommonTypesMetaRegistration() noexcept = default;

   CommonTypesMetaRegistration(const CommonTypesMetaRegistration&) = delete;
   CommonTypesMetaRegistration& operator = (const CommonTypesMetaRegistration&) = delete;

   CommonTypesMetaRegistration(CommonTypesMetaRegistration&&) = delete;
   CommonTypesMetaRegistration& operator = (CommonTypesMetaRegistration&&) = delete;
};

static CommonTypesMetaRegistration commonTypesRegistrator{};

bool RFQ::isXbtBuy() const
{
   if (assetType != Asset::SpotXBT) {
      return false;
   }
   return (product != XbtCurrency) ? (side == Side::Sell) : (side == Side::Buy);
}


QuoteNotification::QuoteNotification(const QuoteReqNotification &qrn, const std::string &_authKey, double price
   , const std::string &txData)
   : authKey(_authKey), reqAuthKey(qrn.requestorAuthPublicKey), settlementId(qrn.settlementId), sessionToken(qrn.sessionToken)
   , quoteRequestId(qrn.quoteRequestId), security(qrn.security), product(qrn.product)
   , transactionData(txData), assetType(qrn.assetType), validityInS(120)
{
   const auto &baseProduct = security.substr(0, security.find('/'));
   side = bs::network::Side::invert(qrn.side);

   if ((side == bs::network::Side::Buy) || ((side == bs::network::Side::Sell) && (product != baseProduct))) {
      bidPx = offerPx = price;
      bidSz = offerSz = qrn.quantity;
   }
   else {
      offerPx = bidPx = price;
      offerSz = bidSz = qrn.quantity;
   }
}

MDField MDField::get(const MDFields &fields, MDField::Type type) {
   for (const auto &field : fields) {
      if (field.type == type) {
         return field;
      }
   }
   return { MDField::Unknown, 0, QString() };
}

bs::network::MDInfo bs::network::MDField::get(const MDFields &fields)
{
   MDInfo mdInfo;
   mdInfo.bidPrice = bs::network::MDField::get(fields, bs::network::MDField::PriceBid).value;
   mdInfo.askPrice = bs::network::MDField::get(fields, bs::network::MDField::PriceOffer).value;
   mdInfo.lastPrice = bs::network::MDField::get(fields, bs::network::MDField::PriceLast).value;
   return mdInfo;
}

Side::Type Side::fromCeler(com::celertech::marketmerchant::api::enums::side::Side side) {
   switch (side) {
      case com::celertech::marketmerchant::api::enums::side::BUY:    return Buy;
      case com::celertech::marketmerchant::api::enums::side::SELL:   return Sell;
   }
   return Undefined;
}

com::celertech::marketmerchant::api::enums::side::Side Side::toCeler(Side::Type side) {
   switch (side) {
      case Buy:   return com::celertech::marketmerchant::api::enums::side::BUY;
      case Sell:
      default:    return com::celertech::marketmerchant::api::enums::side::SELL;
   }
}

const char *Side::toString(Side::Type side) {
   switch (side) {
      case Buy:   return QT_TR_NOOP("BUY");
      case Sell:  return QT_TR_NOOP("SELL");
      default:    return "unknown";
   }
}

const char *Side::responseToString(Side::Type side) {
   switch (side) {
      case Buy:   return QT_TR_NOOP("Offer");
      case Sell:  return QT_TR_NOOP("Bid");
      default:    return "";
   }
}

Side::Type Side::invert(Side::Type side) {
   switch (side) {
      case Buy:   return Sell;
      case Sell:  return Buy;
      default:    return side;
   }
}

bs::network::Asset::Type bs::network::Asset::fromCelerProductType(com::celertech::marketmerchant::api::enums::producttype::ProductType pt) {
   switch (pt) {
      case com::celertech::marketmerchant::api::enums::producttype::SPOT:           return SpotFX;
      case com::celertech::marketmerchant::api::enums::producttype::BITCOIN:        return SpotXBT;
      case com::celertech::marketmerchant::api::enums::producttype::PRIVATE_SHARE:  return PrivateMarket;
      default: return Undefined;
   }
}

com::celertech::marketmerchant::api::enums::assettype::AssetType bs::network::Asset::toCeler(bs::network::Asset::Type at) {
   switch (at) {
      case SpotFX:         return com::celertech::marketmerchant::api::enums::assettype::FX;
      case Futures:        return com::celertech::marketmerchant::api::enums::assettype::FX;
      case SpotXBT:        return com::celertech::marketmerchant::api::enums::assettype::CRYPTO;
      case PrivateMarket:  return com::celertech::marketmerchant::api::enums::assettype::CRYPTO;
      default:             return com::celertech::marketmerchant::api::enums::assettype::STRUCTURED_PRODUCT;
   }
}

com::celertech::marketmerchant::api::enums::producttype::ProductType bs::network::Asset::toCelerProductType(bs::network::Asset::Type at) {
   switch (at) {
      case SpotFX:         return com::celertech::marketmerchant::api::enums::producttype::SPOT;
      case Futures:        return com::celertech::marketmerchant::api::enums::producttype::SPOT;
      case SpotXBT:        return com::celertech::marketmerchant::api::enums::producttype::BITCOIN;
      case PrivateMarket:  return com::celertech::marketmerchant::api::enums::producttype::PRIVATE_SHARE;
      default:             return com::celertech::marketmerchant::api::enums::producttype::SPOT;
   }
}

const char *bs::network::Asset::toCelerSettlementType(bs::network::Asset::Type at) {
   switch (at) {
      case SpotFX:         return "SPOT";
      case Futures:        return "SPOT";
      case SpotXBT:        return "XBT";
      case PrivateMarket:  return "CC";
      default:       return "";
   }
}

const char *bs::network::Asset::toString(bs::network::Asset::Type at) {
   switch (at) {
      case SpotFX:   return QT_TR_NOOP("Spot FX");
      case Futures:  return QT_TR_NOOP("1day Deliverable");
      case SpotXBT:  return QT_TR_NOOP("Spot XBT");
      case PrivateMarket:  return QT_TR_NOOP("Private Market");
      default:       return "";
   }
}

bool bs::network::isTradingEnabled(UserType userType)
{
   switch (userType) {
      case UserType::Market:
      case UserType::Trading:
      case UserType::Dealing:
         return true;
      default:
         return false;
   }
}

void bs::network::MDInfo::merge(const MDInfo &other)
{
   if (other.bidPrice > 0) {
      bidPrice = other.bidPrice;
   }
   if (other.askPrice > 0) {
      askPrice = other.askPrice;
   }
   if (other.lastPrice > 0) {
      lastPrice = other.lastPrice;
   }
}
