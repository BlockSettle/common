#include "CelerPropertiesDefinitions.h"

#include <unordered_map>

std::string CelerUserProperties::GetCelerPropertyDescription(const std::string& propertyName)
{
   static std::unordered_map<std::string, std::string> descriptions = 
   {
      { UserIdPropertyName, "unique user ID"},
      { SubmittedBtcAuthAddressListPropertyName, "submitted auth address list"},
      { SubmittedCCAddressListPropertyName, "submitted CC address list"},
      { MarketSessionPropertyName, "market session (system property)"},
      { SocketAccessPropertyName, "socket access (system property)"},
      { BitcoinParticipantPropertyName, "general trading"},
      { BitcoinDealerPropertyName, "XBT dealing"}
   };

   auto it = descriptions.find(propertyName);
   if (it == descriptions.end()) {
      return "undefined property";
   }

   return it->second;
}
