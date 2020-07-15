/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TradesVerification.h"

#include <spdlog/spdlog.h>

#include "BinaryData.h"
#include "CheckRecipSigner.h"
#include "SettableField.h"

namespace {

   // Allow actual fee be 5% lower than expected
   const float kFeeRateIncreaseThreshold = 0.05f;

}

const char *bs::toString(const bs::PayoutSignatureType t)
{
   switch (t) {
      case bs::PayoutSignatureType::ByBuyer:        return "buyer";
      case bs::PayoutSignatureType::BySeller:       return "seller";
      default:                                      return "undefined";
   }
}

std::shared_ptr<bs::TradesVerification::Result> bs::TradesVerification::Result::error(std::string errorMsg)
{
   auto result = std::make_shared<Result>();
   result->errorMsg = std::move(errorMsg);
   return result;
}

bs::Address bs::TradesVerification::constructSettlementAddress(const BinaryData &settlementId
   , const BinaryData &buyAuthKey, const BinaryData &sellAuthKey)
{
   try {
      auto buySaltedKey = CryptoECDSA::PubKeyScalarMultiply(buyAuthKey, settlementId);
      auto sellSaltedKey = CryptoECDSA::PubKeyScalarMultiply(sellAuthKey, settlementId);

      const auto buyAsset = std::make_shared<AssetEntry_Single>(0, BinaryData()
         , buySaltedKey, nullptr);
      const auto sellAsset = std::make_shared<AssetEntry_Single>(0, BinaryData()
         , sellSaltedKey, nullptr);

      //create ms asset
      std::map<BinaryData, std::shared_ptr<AssetEntry>> assetMap;

      assetMap.insert(std::make_pair(BinaryData::CreateFromHex("00"), buyAsset));
      assetMap.insert(std::make_pair(BinaryData::CreateFromHex("01"), sellAsset));

      const auto assetMs = std::make_shared<AssetEntry_Multisig>(
         0, BinaryData(), assetMap, 1, 2);

      //create ms address
      const auto addrMs = std::make_shared<AddressEntry_Multisig>(assetMs, true);

      //nest it
      const auto addrP2wsh = std::make_shared<AddressEntry_P2WSH>(addrMs);

      return bs::Address::fromHash(addrP2wsh->getPrefixedHash());
   } catch(...) {
      return {};
   }
}

bs::PayoutSignatureType bs::TradesVerification::whichSignature(const Tx &tx, uint64_t value
   , const bs::Address &settlAddr, const BinaryData &buyAuthKey, const BinaryData &sellAuthKey
   , std::string *errorMsg, const BinaryData& providedPayinHash)
{
   if (!tx.isInitialized() || buyAuthKey.empty() || sellAuthKey.empty()) {
      return bs::PayoutSignatureType::Failed;
   }

   constexpr uint32_t txOutIndex = 0;

   int inputId = 0;
   if (providedPayinHash.getSize() == 32)
   {
      /*
      If a hash for the payin is provided, the code will look for the input with
      the relevant outpoint (payinHash:0). This allows for 2 levels of sig verification:

        a) On signed payout delivery, we expect a properly formed payout, and will not
           tolerate any deviation from the protocol. There we shouldn't pass the payin
           hash, as the payout should only have 1 input, which points to the payin first
           output.

        b) When checking sig state for the payin spender as seen on chain, we need to
           know who the signer is regardless of the payout tx strucuture. There we
           should pass the payin hash, as there is no such thing as a tx spending from
           our expected payin without a relevant signature.
      */

      //look for input id with our expected outpoint
      unsigned i=0;
      for (; i<tx.getNumTxIn(); i++)
      {
         auto txIn = tx.getTxInCopy(i);
         auto outpoint = txIn.getOutPoint();

         if (outpoint.getTxHash() == providedPayinHash && 
            outpoint.getTxOutIndex() == txOutIndex) {
            //TODO: log a warning if i != 0 (unexpected payout structure)
            inputId = i;
            break;
         }
      }

      if (i==tx.getNumTxIn()) {
         //could not find payin output in payout outpoint's, report undefined behavior
         return bs::PayoutSignatureType::Undefined;
      }
   }

   const TxIn in = tx.getTxInCopy(inputId);
   const OutPoint op = in.getOutPoint();
   const auto payinHash = op.getTxHash();

   if (op.getTxOutIndex() != txOutIndex) {
      if (errorMsg != nullptr) {
         *errorMsg = fmt::format("invalid outpoint txOutIndex for TX: {}", tx.getThisHash().toHexStr());
      }

      return bs::PayoutSignatureType::Failed;
   }

   UTXO utxo(value, UINT32_MAX, 0, txOutIndex, payinHash
      , BtcUtils::getP2WSHOutputScript(settlAddr.unprefixed()));

   //serialize signed tx
   auto txdata = tx.serialize();

   auto bctx = BCTX::parse(txdata);

   std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;

   utxoMap[utxo.getTxHash()][txOutIndex] = utxo;

   //setup verifier
   try {
      TransactionVerifier tsv(*bctx, utxoMap);

      auto tsvFlags = tsv.getFlags();
      tsvFlags |= SCRIPT_VERIFY_P2SH_SHA256 | SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_SEGWIT;
      tsv.setFlags(tsvFlags);

      /*
      Strict signature state checks expects all supporting UTXOs before checking. Loose
      checks pass returns sign status for all available & relevant UTXOs.

      - payin hash not provided: verifying payout during settlement handshake, using
        strict checks.
      - payin hash provided: checking broadcasted/mined payoout, need to know who 
        signed the payin output, not whether the tx is valid within our context; using
        loose checks.
      */
      bool strictVerification = providedPayinHash.empty() ? true : false;
      auto verifierState = tsv.evaluateState(strictVerification);

      auto inputState = verifierState.getSignedStateForInput(inputId);

      if (inputState.getSigCount() == 0) {
         if (errorMsg) {
            *errorMsg = fmt::format("no signatures received for TX: {}", tx.getThisHash().toHexStr());
         }
         return bs::PayoutSignatureType::Failed;
      }

      if (inputState.isSignedForPubKey(buyAuthKey)) {
         return bs::PayoutSignatureType::ByBuyer;
      }
      if (inputState.isSignedForPubKey(sellAuthKey)) {
         return bs::PayoutSignatureType::BySeller;
      }
      return bs::PayoutSignatureType::Undefined;
   } catch (const std::exception &e) {
      if (errorMsg) {
         *errorMsg = fmt::format("failed: {}", e.what());
      }
      return bs::PayoutSignatureType::Failed;
   } catch (...) {
      if (errorMsg) {
         *errorMsg = "unknown error";
      }
      return bs::PayoutSignatureType::Failed;
   }
}

std::shared_ptr<bs::TradesVerification::Result> bs::TradesVerification::verifyUnsignedPayin(
   const Codec_SignerState::SignerState &unsignedPayin
   , float feePerByte, const std::string &settlementAddress, uint64_t tradeAmount)
{
   if (!unsignedPayin.IsInitialized()) {
      return Result::error("no unsigned payin provided");
   }

   try {
      bs::CheckRecipSigner deserializedSigner(unsignedPayin);
      auto settlAddressBin = bs::Address::fromAddressString(settlementAddress);

      // check that there is only one output of correct amount to settlement address
      auto recipients = deserializedSigner.recipients();
      uint64_t settlementAmount = 0;
      uint64_t totalOutputAmount = 0;
      uint64_t settlementOutputsCount = 0;
      int totalOutputCount = 0;

      SettableField<bs::Address> optionalChangeAddr;

      for (unsigned i=0; i<recipients.size(); i++) {
         auto& recipient = recipients[i];
         uint64_t value = recipient->getValue();

         totalOutputAmount += value;
         const auto &addr = bs::CheckRecipSigner::getRecipientAddress(recipient);
         if (addr == settlAddressBin) {
            settlementAmount += value;
            settlementOutputsCount += 1;

            //fail the check if the settlement isn't the first output of the PayIn tx.
            if (i != 0) {
               return Result::error(fmt::format("unexpected settlement output "
                  "id: {}, expected 0", i));
            }

         } else {
            if (value <= bs::Address::getNativeSegwitDustAmount()) {
               return Result::error(fmt::format("output #{} is a dust ({})", i, value));
            }
            optionalChangeAddr.setValue(addr);
         }
         totalOutputCount += 1;
      }

      if (settlementOutputsCount != 1) {
         return Result::error(fmt::format("unexpected settlement outputs count: {}"
            ", expected 1", settlementOutputsCount));
      }
      if (settlementAmount != tradeAmount) {
         return Result::error(fmt::format("unexpected settlement amount: {}, "
            "expected {}", settlementAmount, tradeAmount));
      }

      // check that fee is fine
      auto spenders = deserializedSigner.spenders();

      uint64_t totalInput = 0;
      for (const auto& spender : spenders) {
         totalInput += spender->getValue();
      }

      if (totalInput < totalOutputAmount) {
         return Result::error(fmt::format("total inputs {} lower that outputs {}", totalInput, totalOutputAmount));
      }

      if (deserializedSigner.isRBF()) {
         return Result::error("Pay-In could not be RBF transaction");
      }

      const uint64_t totalFee = totalInput - totalOutputAmount;
      float feePerByteMin = getAllowedFeePerByteMin(feePerByte);
      const uint64_t estimatedFeeMin = deserializedSigner.estimateFee(feePerByteMin, totalFee);

      if (totalFee < estimatedFeeMin) {
         return Result::error(fmt::format("fee is too small: {}, expected: {} ({} s/b)"
            , totalFee, estimatedFeeMin, feePerByteMin));
      }

      auto result = std::make_shared<Result>();
      result->success = true;
      result->totalFee = totalFee;
      result->estimatedFee = estimatedFeeMin;
      result->totalOutputCount = totalOutputCount;
      if (optionalChangeAddr.isValid()) {
         result->changeAddr = optionalChangeAddr.getValue().display();
      }

      result->utxos.reserve(spenders.size());

      for (const auto& spender : spenders) {
         result->utxos.push_back(spender->getUtxo());
      }

      result->payinHash = deserializedSigner.getTxId();

      if (!XBTInputsAcceptable(result->utxos)) {
         return Result::error("Not supported input type used");
      }
      return result;

   } catch (const std::exception &e) {
      return Result::error(fmt::format("exception during payin processing: {}", e.what()));
   } catch (...) {
      return Result::error(fmt::format("undefined exception during payin processing"));
   }
}

std::shared_ptr<bs::TradesVerification::Result> bs::TradesVerification::verifySignedPayout(const BinaryData &signedPayout
   , const std::string &buyAuthKeyHex, const std::string &sellAuthKeyHex
   , const BinaryData &payinHash, uint64_t tradeAmount, float feePerByte, const std::string &settlementId
   , const std::string &settlementAddress)
{
   if (signedPayout.empty()) {
      return Result::error("signed payout is not provided");
   }

   if (payinHash.empty()) {
      return Result::error("there is no saved payin hash");
   }

   try {
      auto buyAuthKey = BinaryData::CreateFromHex(buyAuthKeyHex);
      auto sellAuthKey = BinaryData::CreateFromHex(sellAuthKeyHex);

      Tx payoutTx(signedPayout);
      if (!payoutTx.isInitialized())
         throw std::runtime_error("TX not initialized");

      auto payoutTxHash = payoutTx.getThisHash();

      // check that there is 1 input and 1 ouput
      if (payoutTx.getNumTxIn() != 1) {
         return Result::error(fmt::format("unexpected number of inputs: {}", payoutTx.getNumTxIn()));
      }

      if (payoutTx.getNumTxOut() != 1) {
         return Result::error(fmt::format("unexpected number of outputs: {}", payoutTx.getNumTxOut()));
      }

      // check that it is from payin hash
      const TxIn in = payoutTx.getTxInCopy(0);
      const OutPoint op = in.getOutPoint();

      //check both outpoint hash and index
      if (op.getTxHash() != payinHash || op.getTxOutIndex() != 0) {
         return Result::error(fmt::format("payout uses unexpected outpoint: {}:{}. Expected: {}:{}"
            , op.getTxHash().toHexStr(), op.getTxOutIndex(), payinHash.toHexStr(), 0));
      }


      // ok, if we use payin hash, that mean that input amount is verified on earlier stage
      // so we need to get output amount and check fee for payout
      const uint64_t receiveValue = payoutTx.getTxOutCopy(0).getValue();
      if (receiveValue > tradeAmount) {
         return Result::error(fmt::format("payout try to spend {} when trade amount is {}", receiveValue, tradeAmount));
      }

      const uint64_t totalFee = tradeAmount - receiveValue;
      const uint64_t txSize = payoutTx.getTxWeight();

      float feePerByteMin = getAllowedFeePerByteMin(feePerByte);
      const float estimatedFeeMin = feePerByteMin * txSize;

      if (totalFee < estimatedFeeMin) {
         return Result::error(fmt::format("fee is too small: {} ({} s/b). Expected: {} ({} s/b)"
            , totalFee, static_cast<float>(totalFee) / txSize, estimatedFeeMin, feePerByteMin));
      }

      // xxx : add a check for fees that are too high

      // check that it is signed by buyer
      const auto settlementIdBin = BinaryData::CreateFromHex(settlementId);

      auto buySaltedKey = CryptoECDSA::PubKeyScalarMultiply(buyAuthKey, settlementIdBin);
      auto sellSaltedKey = CryptoECDSA::PubKeyScalarMultiply(sellAuthKey, settlementIdBin);

      std::string errorMsg;
      const auto signedBy = whichSignature(payoutTx, tradeAmount
         , bs::Address::fromAddressString(settlementAddress), buySaltedKey, sellSaltedKey, &errorMsg);
      if (signedBy != bs::PayoutSignatureType::ByBuyer) {
         return Result::error(fmt::format("payout signature status: {}, errorMsg: '{}'"
            , toString(signedBy), errorMsg));
      }

      auto result = std::make_shared<Result>();
      result->success = true;
      result->payoutTxHashHex = payoutTx.getThisHash().toHexStr();
      return result;

   } catch (const std::exception &e) {
      return Result::error(fmt::format("exception during payout processing: {}", e.what()));
   } catch (...) {
      return Result::error("undefined exception during payout processing");
   }
}

std::shared_ptr<bs::TradesVerification::Result> bs::TradesVerification::verifySignedPayin(const BinaryData &signedPayin
   , const BinaryData &payinHash, const std::vector<UTXO> &prevUTXOs)
{
   if (signedPayin.empty()) {
      return Result::error("no signed payin provided");
   }

   if (payinHash.empty()) {
      return Result::error("there is no saved payin hash");
   }

   try {
      Tx payinTx(signedPayin);
      if (!payinTx.isInitialized())
         throw std::runtime_error("TX not initialized");

      if (payinTx.getThisHash() != payinHash) {
         return Result::error(fmt::format("payin hash mismatch. Expected: {}. From signed payin: {}"
            , payinHash.toHexStr(), payinTx.getThisHash().toHexStr()));
      }
      if (payinTx.getTxWeight() == 0) {
         return Result::error("failed to get TX weight");
      }

      auto result = std::make_shared<Result>();

      std::map<BinaryData, std::map<unsigned, UTXO>> prevUtxoMap;
      for (const auto &utxo : prevUTXOs) {
         prevUtxoMap[utxo.getTxHash()][utxo.getTxOutIndex()] = utxo;
      }
      try {
         const auto &bctx = BCTX::parse(signedPayin);
         TransactionVerifier tsv(*bctx, prevUtxoMap);
         auto tsvFlags = tsv.getFlags();
         tsvFlags |= SCRIPT_VERIFY_P2SH_SHA256 | SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_SEGWIT;
         tsv.setFlags(tsvFlags);
         result->success = tsv.verify();
         if (!result->success) {
            result->errorMsg = "TX verification against previous UTXOs failed";
         }
      }
      catch (const std::exception &e) {
         result->success = false;
         result->errorMsg = std::string("TX verify error: ") + e.what();
      }
      catch (...) {
         result->success = false;
         result->errorMsg = "TX verify unknown error";
      }
      return result;
   }
   catch (const std::exception &e) {
      return Result::error(fmt::format("exception during payin processing: {}", e.what()));
   }
   catch (...) {
      return Result::error("undefined exception during payin processing");
   }
}

//only  TXOUT_SCRIPT_P2WPKH and (TXOUT_SCRIPT_P2SH | TXOUT_SCRIPT_P2WPKH) accepted
bool bs::TradesVerification::XBTInputsAcceptable(const std::vector<UTXO>& utxoList)
try {
   for (const auto& input : utxoList) {
      const auto scrType = BtcUtils::getTxOutScriptType(input.getScript());
      if (scrType == TXOUT_SCRIPT_P2WPKH) {
         continue;
      }

      if (scrType != TXOUT_SCRIPT_P2SH) {
         return false;
      }

#if 0    // no further preimage checks as it's not passed anymore
      // check underlying script type
      auto address = bs::Address::fromScript(input.getScript());

      //xxx: shouldn't be using string addresses when this could be binary addresses.
      const auto it = preImages.find(address.display());
      if (it == preImages.end()) {
         return false;
      }

      auto underlyingScriptType = BtcUtils::getTxOutScriptType(it->second);
      if (underlyingScriptType != TXOUT_SCRIPT_P2WPKH) {
         return false;
      }

      // check that preimage belong to that address
      const auto& hash = BtcUtils::getHash160(it->second);

      BinaryWriter bw;
      bw.put_uint8_t(NetworkConfig::getScriptHashPrefix());
      bw.put_BinaryData(hash);
      const auto& prefixedHash = bw.getData();

      if (prefixedHash != address.prefixed()) {
         return false;
      }
#endif   //0
   }

   return true;
} catch (...) {
   return false;
}

float bs::TradesVerification::getAllowedFeePerByteMin(float feePerByte)
{
   // Allow fee to be slightly less than expected (but not less than 1 sat/byte)
   return std::max(1.0f, feePerByte * (1 - kFeeRateIncreaseThreshold));
}
