/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TradeSettings.h"

#include "bs_types.pb.h"

using namespace bs;

void TradeSettings::toPb(types::TradeSettings &msg)
{
   msg.set_xbt_tier1_limit(xbtTier1Limit);
   msg.set_xbt_price_band(xbtPriceBand);
   msg.set_auth_required_settled_trades(authRequiredSettledTrades);
   msg.set_auth_submit_address_limit(authSubmitAddressLimit);
}

TradeSettings TradeSettings::fromPb(const types::TradeSettings &msg)
{
   TradeSettings result;
   result.xbtTier1Limit = msg.xbt_tier1_limit();
   result.xbtPriceBand = msg.xbt_price_band();
   result.authRequiredSettledTrades = msg.auth_required_settled_trades();
   result.authSubmitAddressLimit = msg.auth_submit_address_limit();
   return result;
}
