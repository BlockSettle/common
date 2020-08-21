/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CHECK_RECIP_SIGNER_H__
#define __CHECK_RECIP_SIGNER_H__

#include "Address.h"
#include "BinaryData.h"
#include "BtcDefinitions.h"
#include "Signer.h"
#include "TxClasses.h"
#include "ValidityFlag.h"

#include <functional>
#include <memory>

namespace spdlog {
   class logger;
}
class ArmoryConnection;


namespace bs {
   class TxAddressChecker
   {
   public:
      TxAddressChecker(const bs::Address &addr, const std::shared_ptr<ArmoryConnection> &armory)
         : address_(addr), armory_(armory) {}
      void setArmory(const std::shared_ptr<ArmoryConnection> &armory) { armory_ = armory; }
      void containsInputAddress(Tx, std::function<void(bool)>
         , uint64_t lotsize = 1, uint64_t value = 0, unsigned int inputId = 0);

   private:
      const bs::Address                   address_;
      std::shared_ptr<ArmoryConnection>   armory_;
   };


   class CheckRecipSigner : public ArmorySigner::Signer
   {
   public:
      CheckRecipSigner(const std::shared_ptr<ArmoryConnection> &armory = nullptr)
         : Signer(), armory_(armory) {}
      CheckRecipSigner(const Codec_SignerState::SignerState &state, const std::shared_ptr<ArmoryConnection> &armory = nullptr)
         : Signer(), armory_(armory) {
         deserializeState(state);
      }
      CheckRecipSigner(Signer&& signer)
         : Signer(std::move(signer))
      {}
      ~CheckRecipSigner() override = default;

      using cbFindRecip = std::function<void(uint64_t valOutput, uint64_t valReturn, uint64_t valInput)>;
      bool findRecipAddress(const Address &address, cbFindRecip cb) const;

      void hasInputAddress(const Address &, std::function<void(bool)>, uint64_t lotsize = 1);
      uint64_t estimateFee(float &feePerByte, uint64_t fixedFee = 0) const;

      std::vector<std::shared_ptr<ArmorySigner::ScriptSpender>> spenders() const { return spenders_; }
      bool isRBF() const;

      bool GetInputAddressList(const std::shared_ptr<spdlog::logger> &logger, std::function<void(std::vector<bs::Address>)>);

      static bs::Address getRecipientAddress(const std::shared_ptr<ArmorySigner::ScriptRecipient> &recip) {
         return bs::Address::fromScript(getRecipientOutputScript(recip));
      }

      bool verifyPartial(void);

      void reset();

   private:
      static BinaryData getRecipientOutputScript(const std::shared_ptr<ArmorySigner::ScriptRecipient> &recip) {
         const auto &recipScr = recip->getSerializedScript();
         const auto scr = recipScr.getSliceRef(8, (uint32_t)recipScr.getSize() - 8);
         if (scr.getSize() != (size_t)(scr[0] + 1)) {
            return{};
         }
         return scr.getSliceCopy(1, scr[0]);
      }

      bool hasReceiver() const;

   private:
      std::shared_ptr<ArmoryConnection>   armory_;
      std::set<BinaryData> txHashSet_;
      std::map<BinaryData, std::set<uint32_t>>  txOutIdx_;
      std::atomic_bool     resultFound_;
      ValidityFlag validityFlag_;
   };


   class TxChecker
   {
   public:
      TxChecker(const Tx &tx) : tx_(tx) {}

      int receiverIndex(const bs::Address &) const;
      bool hasReceiver(const bs::Address &) const;
      void hasSpender(const bs::Address &, const std::shared_ptr<ArmoryConnection> &
         , std::function<void(bool)>) const;
      bool hasInput(const BinaryData &txHash) const;

   private:
      const Tx tx_;
   };

}  //namespace bs

NetworkType getNetworkType();

#endif //__CHECK_RECIP_SIGNER_H__
