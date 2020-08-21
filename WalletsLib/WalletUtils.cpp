/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WalletUtils.h"

#include <map>
#include <limits>

using namespace bs;

std::vector<UTXO> bs::selectUtxoForAmount(const std::vector<UTXO> &utxos, uint64_t amount)
{
   if (amount == std::numeric_limits<uint64_t>::max()) {
      return utxos;
   }
   else if (amount == 0) {
      return {};
   }

   auto inputs = utxos;
   std::sort(inputs.begin(), inputs.end(), [](const UTXO& left, const UTXO& right) -> bool {
      return left.getValue() > right.getValue();
   });

   auto remainingAmount = static_cast<int64_t>(amount);
   auto begin = inputs.begin();
   for (; begin != inputs.end() && remainingAmount > 0; ++begin) {
      auto value = static_cast<int64_t>(begin->getValue());
      if (remainingAmount > value) {
         remainingAmount -= value;
         continue;
      }

      auto next = begin + 1;
      while (next != inputs.end() && remainingAmount <= static_cast<int64_t>(next->getValue())) {
         ++next;
      }
      
      std::iter_swap(begin, --next);
      break;
   }

   if (begin == inputs.end()) {
      return inputs;
   }

   inputs.erase(++begin, inputs.end());
   return inputs;
}
