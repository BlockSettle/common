/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthAddress.h"

AuthAddress::AuthAddress(const bs::Address &chainedAddr, AddressVerificationState state)
   : chainedAddress_(chainedAddr)
   , state_(state)
{}

std::string to_string(AddressVerificationState state)
{
   switch(state) {
   case AddressVerificationState::VerificationFailed:
      return "VerificationFailed";
   case AddressVerificationState::Virgin:
      return "Virgin";
   case AddressVerificationState::Tainted:
      return "Tainted";
   case AddressVerificationState::Verifying:
      return "Verifying";
   case AddressVerificationState::Verified:
      return "Verified";
   case AddressVerificationState::Revoked:
      return "Revoked";
   case AddressVerificationState::Invalidated_Explicit:
      return "Invalidated_Explicit";
   case AddressVerificationState::Invalidated_Implicit:
      return "Invalidated_Implicit";
   default:
      return "Unknown state enum value";
   }
}
