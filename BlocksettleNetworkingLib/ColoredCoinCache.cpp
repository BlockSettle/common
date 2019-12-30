/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#include "ColoredCoinCache.h"

#include "ColoredCoinLogic.h"
#include "tracker_cache.pb.h"

namespace {

   std::shared_ptr<BinaryData> getOrInsert(const BinaryData &hash, std::map<BinaryData, std::shared_ptr<BinaryData>> &txHashes)
   {
      auto &elem = txHashes[hash];
      if (!elem) {
         elem = std::make_shared<BinaryData>(hash);
      }
      return elem;
   }

} // namespace

BinaryData serializeSnapshot(ColoredCoinCache cache)
{
   if (!cache.snapshot) {
      return {};
   }

   Blocksettle::Storage::CcTracker::ColoredCoinSnapshot msg;

   for (const auto &utxo : cache.snapshot->utxoSet_) {
      for (const auto &item : utxo.second) {
         const auto &point = *item.second;
         auto d = msg.add_outpoints();
         d->set_value(point.value());
         d->set_index(point.index());
         d->set_tx_hash(point.getTxHash()->toBinStr());
         d->set_scr_addr(point.getScrAddr()->toBinStr());
      }
   }

   for (const auto &revoked : cache.snapshot->revokedAddresses_) {
      auto d = msg.add_revoked_addresses();
      d->set_scr_addr(revoked.first.toBinStr());
      d->set_height(revoked.second);
   }

   msg.set_start_height(cache.startHeight);
   msg.set_processed_height(cache.processedHeight);

   return msg.SerializeAsString();
}

ColoredCoinCache deserializeSnapshot(const BinaryData &data)
{
   Blocksettle::Storage::CcTracker::ColoredCoinSnapshot msg;
   bool result = msg.ParseFromArray(data.getPtr(), static_cast<int>(data.getSize()));
   if (!result) {
      return {};
   }

   ColoredCoinCache cache;
   cache.snapshot = std::make_shared<ColoredCoinSnapshot>();
   std::map<BinaryData, std::shared_ptr<BinaryData>> txHashes;
   std::map<BinaryData, std::shared_ptr<BinaryData>> scrAddresses;

   for (const auto &d : msg.outpoints()) {
      auto point = std::make_shared<CcOutpoint>(d.value(), d.index());
      const BinaryData txHash{d.tx_hash()};
      const BinaryData scrAddr{d.scr_addr()};

      point->setTxHash(getOrInsert(txHash, txHashes));
      point->setScrAddr(getOrInsert(scrAddr, scrAddresses));

      {
         auto &utxo = cache.snapshot->utxoSet_[txHash];
         auto result = utxo.emplace(point->index(), point);
         if (!result.second) {
            return {};
         }
      }

      {
         auto &scrAddrSet = cache.snapshot->scrAddrCcSet_[scrAddr];
         auto result = scrAddrSet.emplace(point);
         if (!result.second) {
            return {};
         }
      }
   }

   for (const auto &d : msg.revoked_addresses()) {
      cache.snapshot->revokedAddresses_[d.scr_addr()] = d.height();
   }

   cache.startHeight = msg.start_height();
   cache.processedHeight = msg.processed_height();

   return cache;
}
