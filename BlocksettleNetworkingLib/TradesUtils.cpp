/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TradesUtils.h"

#include <spdlog/spdlog.h>

#include "CoinSelection.h"
#include "FastLock.h"
#include "UtxoReservation.h"
#include "Wallets/SyncHDGroup.h"
#include "Wallets/SyncHDLeaf.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"

using namespace ArmorySigner;

namespace {

   std::shared_ptr<bs::sync::hd::SettlementLeaf> findSettlementLeaf(const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr, const bs::Address &ourAuthAddress)
   {
      auto wallet = walletsMgr->getPrimaryWallet();
      if (!wallet) {
         return nullptr;
      }

      auto group = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(wallet->getGroup(bs::hd::BlockSettle_Settlement));
      if (!group) {
         return nullptr;
      }

      return group->getLeaf(ourAuthAddress);
   }

} // namespace

bool bs::tradeutils::getSpendableTxOutList(const std::vector<std::shared_ptr<bs::sync::Wallet>> &wallets
   , const std::function<void(const std::map<UTXO, std::string> &)> &cb, bool excludeReservation)
{
   if (wallets.empty()) {
      cb({});
      return true;
   }
   struct Result
   {
      std::map<std::string, std::vector<UTXO>> utxosMap;
      std::function<void(const std::map<UTXO, std::string> &)> cb;
      std::atomic_flag lockFlag = ATOMIC_FLAG_INIT;
   };
   auto result = std::make_shared<Result>();
   result->cb = std::move(cb);

   for (const auto &wallet : wallets) {
      auto cbWrap = [result, size = wallets.size(), walletId = wallet->walletId()]
         (std::vector<UTXO> utxos)
      {
         FastLock lock(result->lockFlag);
         result->utxosMap.emplace(walletId, std::move(utxos));
         if (result->utxosMap.size() != size) {
            return;
         }

         std::map<UTXO, std::string> utxosAll;

         for (auto &item : result->utxosMap) {
            for (const auto &utxo : item.second) {
               utxosAll[utxo] = item.first;
            }
         }
         result->cb(utxosAll);
      };

      // If request for some wallet failed resulted callback won't be called.
      if (!wallet->getSpendableTxOutList(cbWrap, UINT64_MAX, excludeReservation)) {
         return false;
      }
   }
   return true;
}

bs::tradeutils::Result bs::tradeutils::Result::error(std::string msg)
{
   bs::tradeutils::Result result;
   result.errorMsg = std::move(msg);
   return result;
}

bs::tradeutils::PayinResult bs::tradeutils::PayinResult::error(std::string msg)
{
   bs::tradeutils::PayinResult result;
   result.errorMsg = std::move(msg);
   return result;
}

unsigned bs::tradeutils::feeTargetBlockCount()
{
   return 2;
}

uint64_t bs::tradeutils::estimatePayinFeeWithoutChange(const std::vector<UTXO> &inputs, float feePerByte)
{
   // add workaround for computeSizeAndFee (it can't compute exact v-size before signing,
   // sometimes causing "fee not met" error for 1 sat/byte)
   if (feePerByte >= 1.0f && feePerByte < 1.01f) {
      feePerByte = 1.01f;
   }

   std::map<unsigned, std::vector<std::shared_ptr<ScriptRecipient>>> recipientsMap;
   // Use some fake settlement address as the only recipient
   BinaryData prefixed;
   prefixed.append(AddressEntry::getPrefixByte(AddressEntryType_P2WSH));
   prefixed.append(CryptoPRNG::generateRandom(32));
   auto bsAddr = bs::Address::fromHash(prefixed);
   // Select some random amount
   std::vector<std::shared_ptr<ScriptRecipient>> recVec(
      {bsAddr.getRecipient(bs::XBTAmount(uint64_t(1000)))});
   recipientsMap.emplace(0, std::move(recVec));

   auto inputsCopy = bs::Address::decorateUTXOsCopy(inputs);
   PaymentStruct payment(recipientsMap, 0, feePerByte, 0);
   uint64_t result = bs::Address::getFeeForMaxVal(inputsCopy, payment.size(), feePerByte);
   return result;
}

void bs::tradeutils::createPayin(bs::tradeutils::PayinArgs args, bs::tradeutils::PayinResultCb cb)
{
   auto leaf = findSettlementLeaf(args.walletsMgr, args.ourAuthAddress);
   if (!leaf) {
      cb(PayinResult::error("can't find settlement leaf"));
      return;
   }

   if (args.inputXbtWallets.empty()) {
      cb(PayinResult::error("XBT wallets not set"));
      return;
   }

   leaf->setSettlementID(args.settlementId, [args, cb](bool result)
   {
      if (!result) {
         cb(PayinResult::error("setSettlementID failed"));
         return;
      }

      auto cbFee = [args, cb](float fee) {
         auto feePerByteArmory = ArmoryConnection::toFeePerByte(fee);
         auto feePerByte = std::max(feePerByteArmory, args.feeRatePb_);
         if (feePerByte < 1.0f) {
            cb(PayinResult::error("invalid feePerByte"));
            return;
         }

         auto primaryHdWallet = args.walletsMgr->getPrimaryWallet();
         if (!primaryHdWallet) {
            cb(PayinResult::error("can't find primary wallet"));
            return;
         }

         const auto &xbtWallet = args.inputXbtWallets.front();

         auto cbSettlAddr = [args, cb, feePerByte, xbtWallet](const bs::Address &settlAddr)
         {
            if (settlAddr.empty()) {
               cb(PayinResult::error("invalid settl addr"));
               return;
            }

            auto inputsCb = [args, cb, settlAddr, feePerByte, xbtWallet](const std::vector<UTXO> &utxosOrig, bool useAllInputs) {
               auto utxos = bs::Address::decorateUTXOsCopy(utxosOrig);

               std::map<unsigned, std::vector<std::shared_ptr<ScriptRecipient>>> recipientsMap;
               std::vector<std::shared_ptr<ScriptRecipient>> recVec({settlAddr.getRecipient(args.amount)});
               recipientsMap.emplace(0, recVec);
               auto payment = PaymentStruct(recipientsMap, 0, feePerByte, 0);

               auto coinSelection = CoinSelection(nullptr, {}, args.amount.GetValue(), args.armory->topBlock());

               try {
                  UtxoSelection selection;
                  if (useAllInputs) {
                     selection = UtxoSelection(utxos);
                     selection.fee_byte_ = feePerByte;
                     selection.computeSizeAndFee(payment);
                  } else {
                     selection = coinSelection.getUtxoSelectionForRecipients(payment, utxos);
                  }
                  auto selectedInputs = selection.utxoVec_;
                  auto fee = selection.fee_;
                  bool needChange = true;

                  uint64_t inputAmount = 0;
                  for (const auto &utxo : selectedInputs) {
                     inputAmount += utxo.getValue();
                  }
                  const int64_t changeAmount = inputAmount - args.amount.GetValue() - fee;
                  if (changeAmount < 0) {
                     throw std::runtime_error("negative change amount");
                  }
                  if (changeAmount <= bs::Address::getNativeSegwitDustAmount()) {
                     needChange = false;
                     fee += changeAmount;
                  }

                  auto changeCb = [args, selectedInputs, fee, settlAddr, xbtWallet, recVec, cb]
                     (const bs::Address &changeAddr)
                  {
                     auto txReq = std::make_shared<bs::core::wallet::TXSignRequest>(
                        bs::sync::wallet::createTXRequest(args.inputXbtWallets
                           , selectedInputs, recVec, false, changeAddr, fee, false));

                     const auto cbResolvePubData = [args, settlAddr, cb, txReq, xbtWallet, changeAddr]
                        (bs::error::ErrorCode errCode, const Codec_SignerState::SignerState &state)
                     {
                        PayinResult result;
                        result.settlementAddr = settlAddr;
                        result.success = true;

                        try {
                           txReq->armorySigner_.merge(state);
                           result.signRequest = *txReq;
                           result.payinHash = txReq->txId();
                           result.signRequest.txHash = result.payinHash;

                           if (!changeAddr.empty()) {
                              xbtWallet->setAddressComment(changeAddr
                                 , bs::sync::wallet::Comment::toString(bs::sync::wallet::Comment::ChangeAddress));
                           }

                        } catch (const std::exception &e) {
                           cb(PayinResult::error(fmt::format("creating paying request failed: {}", e.what())));
                           return;
                        }

                        std::set<BinaryData> hashes;
                        for (unsigned i=0; i<result.signRequest.armorySigner_.getTxInCount(); i++) {
                           auto spender = result.signRequest.armorySigner_.getSpender(i);
                           hashes.emplace(spender->getOutputHash());
                        }

                        auto supportingTxMapCb = [cb, result = std::move(result)]
                              (const AsyncClient::TxBatchResult& txs, std::exception_ptr eptr) mutable
                        {
                           if (eptr) {
                              cb(PayinResult::error(fmt::format("requesting supporting TXs failed")));
                              return;
                           }

                           for (auto& txPair : txs) {
                              result.signRequest.armorySigner_.addSupportingTx(*txPair.second);
                           }

                           if (!result.signRequest.isValid()) {
                              cb(PayinResult::error("invalid pay-in transaction"));
                              return;
                           }

                           cb(std::move(result));
                        };

                        bool rc = args.armory->getTXsByHash(hashes, supportingTxMapCb, true);
                        if (!rc) {
                           cb(PayinResult::error(fmt::format("getTXsByHash failed")));
                        }
                     };

                     //resolve in all circumstances
                     args.signContainer->resolvePublicSpenders(*txReq, cbResolvePubData);
                  };

                  if (needChange) {
                     xbtWallet->getNewIntAddress(changeCb);
                  }
                  else {
                     changeCb({});
                  }
               } catch (const std::exception &e) {
                  cb(PayinResult::error(fmt::format("unexpected exception: {}", e.what())));
                  return;
               }
            };

            if (args.fixedInputs.empty()) {
               // #UTXO_MANAGER: this shoudn't be possible anymore
               // leave for now, expecting has assert here
               auto inputsCbWrap = [args, cb, inputsCb](std::map<UTXO, std::string> inputs) {
                  std::vector<UTXO> utxos;
                  utxos.reserve(inputs.size());
                  for (const auto &input : inputs) {
                     utxos.emplace_back(std::move(input.first));
                  }
                  if (args.utxoReservation) {
                     std::vector<UTXO> filtered;
                     // Ignore filter return value as it fails if there were no reservations before
                     args.utxoReservation->filter(utxos, filtered);
                  }
                  inputsCb(utxos, false);
               };
               getSpendableTxOutList(args.inputXbtWallets, inputsCbWrap, true);
            } else {
               inputsCb(args.fixedInputs, true);
            }
         };

         const bool myKeyFirst = false;
         primaryHdWallet->getSettlementPayinAddress(args.settlementId, args.cpAuthPubKey, cbSettlAddr, myKeyFirst);
      };

      args.armory->estimateFee(feeTargetBlockCount(), cbFee);
   });
}

uint64_t bs::tradeutils::getEstimatedFeeFor(UTXO input, const bs::Address &recvAddr
   , float feePerByte, unsigned int topBlock)
{
   if (!input.isInitialized()) {
      return 0;
   }
   const auto inputAmount = input.getValue();
   if (input.txinRedeemSizeBytes_ == UINT32_MAX) {
      const auto scrAddr = bs::Address::fromHash(input.getRecipientScrAddr());
      input.txinRedeemSizeBytes_ = (unsigned int)scrAddr.getInputSize();
   }
   CoinSelection coinSelection([&input](uint64_t) -> std::vector<UTXO> { return { input }; }
   , std::vector<AddressBookEntry>{}, inputAmount, topBlock);

   const auto &scriptRecipient = recvAddr.getRecipient(bs::XBTAmount{ inputAmount });
   return coinSelection.getFeeForMaxVal(scriptRecipient->getSize(), feePerByte, { input });
}

bs::core::wallet::TXSignRequest bs::tradeutils::createPayoutTXRequest(UTXO input
   , const bs::Address &recvAddr, float feePerByte, unsigned int topBlock)
{
   bs::core::wallet::TXSignRequest txReq;
   txReq.armorySigner_.addSpender(std::make_shared<ScriptSpender>(input));
   input.isInputSW_ = true;
   input.witnessDataSizeBytes_ = unsigned(bs::Address::getPayoutWitnessDataSize());
   uint64_t fee = getEstimatedFeeFor(input, recvAddr, feePerByte, topBlock);

   uint64_t value = input.getValue();
   if (value < fee) {
      value = 0;
   } else {
      value = value - fee;
   }

   txReq.fee = fee;
   txReq.armorySigner_.addRecipient(recvAddr.getRecipient(bs::XBTAmount{ value }));
   return txReq;
}

UTXO bs::tradeutils::getInputFromTX(const bs::Address &addr
   , const BinaryData &payinHash, unsigned txOutIndex, const bs::XBTAmount& amount)
{
   constexpr uint32_t txHeight = UINT32_MAX;

   return UTXO(amount.GetValue(), txHeight, UINT32_MAX, txOutIndex, payinHash
      , BtcUtils::getP2WSHOutputScript(addr.unprefixed()));
}

void bs::tradeutils::createPayout(bs::tradeutils::PayoutArgs args
   , bs::tradeutils::PayoutResultCb cb, bool myKeyFirst)
{
   auto leaf = findSettlementLeaf(args.walletsMgr, args.ourAuthAddress);
   if (!leaf) {
      cb(PayoutResult::error("can't find settlement leaf"));
      return;
   }

   leaf->setSettlementID(args.settlementId, [args, cb, myKeyFirst]
      (bool result)
   {
      if (!result) {
         cb(PayoutResult::error("setSettlementID failed"));
         return;
      }

      auto cbFee = [args, cb, myKeyFirst](float fee) {
         auto feePerByteArmory = ArmoryConnection::toFeePerByte(fee);
         auto feePerByte = std::max(feePerByteArmory, args.feeRatePb_);
         if (feePerByte < 1.0f) {
            cb(PayoutResult::error("invalid feePerByte"));
            return;
         }

         auto primaryHdWallet = args.walletsMgr->getPrimaryWallet();
         if (!primaryHdWallet) {
            cb(PayoutResult::error("can't find primary wallet"));
            return;
         }

         auto cbSettlAddr = [args, cb, feePerByte](const bs::Address &settlAddr) {
            auto recvAddrCb = [args, cb, feePerByte, settlAddr](const bs::Address &recvAddr) {
               if (settlAddr.empty()) {
                  cb(PayoutResult::error("invalid settl addr"));
                  return;
               }

               auto payinUTXO = getInputFromTX(settlAddr, args.payinTxId, 0, args.amount);

               PayoutResult result;
               result.success = true;
               result.settlementAddr = settlAddr;
               result.signRequest = createPayoutTXRequest(
                  payinUTXO, recvAddr, feePerByte, args.armory->topBlock());

               //this will resolve public data along the way
               result.signRequest.txHash = result.signRequest.txId();
               cb(std::move(result));
            };

            if (!args.recvAddr.empty()) {
               recvAddrCb(args.recvAddr);
            } else {
               // BST-2474: All addresses related to trading, not just change addresses, should use internal addresses
               args.outputXbtWallet->getNewIntAddress(recvAddrCb);
            }
         };
         primaryHdWallet->getSettlementPayinAddress(args.settlementId, args.cpAuthPubKey, cbSettlAddr, myKeyFirst);
      };

      args.armory->estimateFee(feeTargetBlockCount(), cbFee);
   });
}

bs::tradeutils::PayoutVerifyResult bs::tradeutils::verifySignedPayout(bs::tradeutils::PayoutVerifyArgs args)
{
   PayoutVerifyResult result;

   try {
      Tx tx(args.signedTx);

      auto txdata = tx.serialize();
      auto bctx = BCTX::parse(txdata);

      auto utxo = getInputFromTX(args.settlAddr, args.usedPayinHash, 0, args.amount);

      std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;
      utxoMap[utxo.getTxHash()][0] = utxo;

      TransactionVerifier tsv(*bctx, utxoMap);

      auto tsvFlags = tsv.getFlags();
      tsvFlags |= SCRIPT_VERIFY_P2SH_SHA256 | SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_SEGWIT;
      tsv.setFlags(tsvFlags);

      auto verifierState = tsv.evaluateState();

      auto inputState = verifierState.getSignedStateForInput(0);

      auto signatureCount = inputState.getSigCount();

      if (signatureCount != 1) {
         result.errorMsg = fmt::format("signature count: {}", signatureCount);
         return result;
      }

      result.success = true;
      return result;

   } catch (const std::exception &e) {
      result.errorMsg = fmt::format("failed: {}", e.what());
      return result;
   }
}

double bs::tradeutils::reservationQuantityMultiplier()
{
   return 1.2;
}
