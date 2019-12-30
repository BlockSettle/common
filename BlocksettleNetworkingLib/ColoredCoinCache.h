/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#ifndef COLORED_COIN_CACHE_H
#define COLORED_COIN_CACHE_H

#include <memory>

#include "BinaryData.h"

struct ColoredCoinSnapshot;

struct ColoredCoinCache
{
   std::shared_ptr<ColoredCoinSnapshot> snapshot;
   unsigned startHeight{};
   unsigned processedHeight{};
};

BinaryData serializeSnapshot(ColoredCoinCache cache);

// Will return empty result in case of errors
ColoredCoinCache deserializeSnapshot(const BinaryData &data);

#endif
