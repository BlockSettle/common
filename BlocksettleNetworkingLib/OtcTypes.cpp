#include "OtcTypes.h"

#include <cassert>
#include <cmath>
#include <spdlog/spdlog.h>

#include "BTCNumericTypes.h"

using namespace bs::network;

std::string bs::network::otc::toString(bs::network::otc::Side side)
{
   switch (side) {
      case Side::Buy:   return "BUY";
      case Side::Sell:  return "SELL";
      default:          return "Unknown";
   }
}

std::string bs::network::otc::toString(bs::network::otc::RangeType range)
{
   switch(range) {
      case RangeType::Range0_1:
         return "0-1";
      case RangeType::Range1_5:
         return "1-5";
      case RangeType::Range5_10:
         return "5-10";
      case RangeType::Range10_50:
         return "10-50";
      case RangeType::Range50_100:
         return "50-100";
      case RangeType::Range100_250:
         return "100-250";
      case RangeType::Range250plus:
         return "250+";
   }

   assert(false);
   return "invalid range";
}

bs::network::otc::Range bs::network::otc::getRange(bs::network::otc::RangeType range)
{
   switch(range) {
      case RangeType::Range0_1:
         return Range{0, 1};
      case RangeType::Range1_5:
         return Range{1, 5};
      case RangeType::Range5_10:
         return Range{5, 10};
      case RangeType::Range10_50:
         return Range{10,50};
      case RangeType::Range50_100:
         return Range{50, 100};
      case RangeType::Range100_250:
         return Range{100, 250};
      case RangeType::Range250plus:
         return Range{250, 10000};
   }

   assert(false);
   return Range{0, 0};
}

bool bs::network::otc::isValidSide(bs::network::otc::Side side)
{
   switch (side) {
      case Side::Buy:
      case Side::Sell:
         return true;
      default:
         return false;
   }
}

bs::network::otc::Side bs::network::otc::switchSide(bs::network::otc::Side side)
{
   switch (side) {
      case Side::Sell:   return Side::Buy;
      case Side::Buy:    return Side::Sell;
      default:           return Side::Unknown;
   }
}

std::string bs::network::otc::toString(bs::network::otc::State state)
{
   switch (state) {
      case State::OfferSent:        return "OfferSent";
      case State::OfferRecv:        return "OfferRecv";
      case State::WaitPayinInfo:    return "WaitPayinInfo";
      case State::SentPayinInfo:    return "SentPayinInfo";
      case State::Idle:             return "Idle";
      case State::Blacklisted:      return "Blacklisted";
   }

   assert(false);
   return "";
}

bool bs::network::otc::Offer::operator==(const Offer &other) const
{
   return ourSide == other.ourSide
      && amount == other.amount
      && price == other.price;
}

bool bs::network::otc::Offer::operator!=(const Offer &other) const
{
   return !(*this == other);
}

double bs::network::otc::satToBtc(int64_t value)
{
   return double(value) / BTCNumericTypes::BalanceDivider;
}

double bs::network::otc::satToBtc(uint64_t value)
{
   return double(value) / BTCNumericTypes::BalanceDivider;
}

int64_t bs::network::otc::btcToSat(double value)
{
   return std::llround(value * BTCNumericTypes::BalanceDivider);
}

double bs::network::otc::fromCents(int64_t value)
{
   return double(value) / 100.;
}

int64_t bs::network::otc::toCents(double value)
{
   return std::llround(value * 100);
}

bs::network::otc::RangeType bs::network::otc::firstRangeValue(bs::network::otc::Env env)
{
   switch (env) {
      case Env::Prod: return RangeType::Range5_10;
      case Env::Test: return RangeType::Range0_1;
   }
   assert(false);
   return {};
}

bs::network::otc::RangeType bs::network::otc::lastRangeValue(bs::network::otc::Env env)
{
   return RangeType::Range250plus;
}

std::string bs::network::otc::PeerId::toString() const
{
   return fmt::format("{}/{}", contactId, otc::toString(type));
}

bool bs::network::otc::PeerId::operator==(const bs::network::otc::PeerId &other) const
{
   return type == other.type && contactId == other.contactId;
}

bool bs::network::otc::PeerId::operator!=(const bs::network::otc::PeerId &other) const
{
   return !this->operator==(other);
}

std::size_t bs::network::otc::PeerIdHash::operator()(const bs::network::otc::PeerId &key) const
{
   size_t res = 17;
   res = res * 31 + std::hash<std::string>()(key.contactId);
   res = res * 31 + std::hash<int>()(int(key.type));
   return res;
}

std::string bs::network::otc::toString(bs::network::otc::PeerType peerType)
{
   switch (peerType) {
      case PeerType::Private:    return "Private";
      case PeerType::PublicSent: return "PublicSent";
      case PeerType::PublicRecv: return "PublicRecv";
   }
   assert(false);
   return {};
}
