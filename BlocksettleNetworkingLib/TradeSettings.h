/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef TRADE_SETTINGS_H
#define TRADE_SETTINGS_H

#include <cinttypes>

namespace bs {

   namespace types {
      class TradeSettings;
   }

   struct TradeSettings {
      uint64_t xbtTier1Limit{};
      uint32_t xbtPriceBand{};
      uint32_t authRequiredSettledTrades{};
      uint32_t authSubmitAddressLimit{};

      void toPb(types::TradeSettings &msg);
      static TradeSettings fromPb(const types::TradeSettings &msg);
   };

}

#endif
