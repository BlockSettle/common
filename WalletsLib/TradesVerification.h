/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef TRADES_VERIFICATION_H
#define TRADES_VERIFICATION_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TxClasses.h"

namespace Codec_SignerState {
   class SignerState;
}
class  BinaryData;

namespace bs {

   class Address;

   enum class PayoutSignatureType : int
   {
      Undefined,
      ByBuyer,
      BySeller,
      Failed
   };

   const char *toString(const PayoutSignatureType t);

   class TradesVerification
   {
   public:
      struct Result
      {
         bool success{false};
         std::string errorMsg;

         // returns from verifyUnsignedPayin
         uint64_t totalFee{};
         uint64_t estimatedFee{};
         int totalOutputCount{};
         std::vector<UTXO> utxos;
         std::string changeAddr;
         BinaryData  payinHash;

         // returns from verifySignedPayout
         std::string payoutTxHashHex;

         static std::shared_ptr<Result> error(std::string errorMsg);
      };

      static bs::Address constructSettlementAddress(const BinaryData &settlementId
         , const BinaryData &buyAuthKey, const BinaryData &sellAuthKey);

      static PayoutSignatureType whichSignature(const Tx &tx
         , uint64_t value
         , const bs::Address &settlAddr
         , const BinaryData &buyAuthKey, const BinaryData &sellAuthKey
         , std::string *errorMsg = nullptr, const BinaryData& payinHash = {});

      static std::shared_ptr<Result> verifyUnsignedPayin(const Codec_SignerState::SignerState &unsignedPayin
         , float feePerByte, const std::string &settlementAddress, uint64_t tradeAmount);

      static std::shared_ptr<Result> verifySignedPayout(const BinaryData &signedPayout
         , const std::string &buyAuthKeyHex, const std::string &sellAuthKeyHex,  const BinaryData &payinHash
         , uint64_t tradeAmount, float feePerByte, const std::string &settlementId, const std::string &settlementAddress);

      static std::shared_ptr<Result> verifySignedPayin(const BinaryData &signedPayin
         , const BinaryData &payinHash, const std::vector<UTXO> &origUtxos);

      // preImages - key: address, value:preimage script
      // required for P2SH addresses only
      static bool XBTInputsAcceptable(
         const std::vector<UTXO>& utxoList, 
         const std::map<BinaryData, BinaryData>& preImages);

      static float getAllowedFeePerByteMin(float feePerByte);
      static float minRelayFee(void);
   };

}

#endif
