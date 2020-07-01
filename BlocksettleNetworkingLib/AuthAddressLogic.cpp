/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthAddressLogic.h"

namespace {
   constexpr uint64_t kAuthValueThreshold = 1000;

   const auto kMaxFutureWaitTime = std::chrono::seconds(30);

   template<class T>
   void checkFutureWait(std::future<T> &f)
   {
      if (f.wait_for(kMaxFutureWaitTime) != std::future_status::ready) {
         throw std::runtime_error("future wait timeout");
      }
   }

}

///////////////////////////////////////////////////////////////////////////////
void ValidationAddressACT::onRefresh(const std::vector<BinaryData>& ids, bool online)
{
   auto dbns = std::make_shared<DBNotificationStruct>(DBNS_Refresh);
   dbns->ids_ = ids;
   dbns->online_ = online;

   notifQueue_.push_back(std::move(dbns));
}

////
void ValidationAddressACT::onZCReceived(const std::string& , const std::vector<bs::TXEntry>& zcs)
{
   auto dbns = std::make_shared<DBNotificationStruct>(DBNS_ZC);
   dbns->zc_ = zcs;

   notifQueue_.push_back(std::move(dbns));
}

////
void ValidationAddressACT::onNewBlock(unsigned int height, unsigned int)
{
   auto dbns = std::make_shared<DBNotificationStruct>(DBNS_NewBlock);
   dbns->block_ = height;

   notifQueue_.push_back(std::move(dbns));
}

////
void ValidationAddressACT::processNotification()
{
   while (true) {
      std::shared_ptr<DBNotificationStruct> dbNotifPtr;
      try {
         dbNotifPtr = notifQueue_.pop_front();
      }
      catch (ArmoryThreading::StopBlockingLoop&) {
         break;
      }

      const auto callbacks = callbacks_.lock();
      if (!callbacks) {
         break;
      }

      switch (dbNotifPtr->type_) {
      case DBNS_NewBlock:
      case DBNS_ZC:
         if (callbacks->onUpdate) {
            callbacks->onUpdate();
         }
         break;

      case DBNS_Refresh:
         if (callbacks->onRefresh) {
            callbacks->onRefresh(dbNotifPtr->ids_);
         }
         break;

      default:
         throw std::runtime_error("unexpected notification type");
      }
   }
}

////
void ValidationAddressACT::start()
{
   const auto callbacks = callbacks_.lock();
   if (!callbacks) {
      throw std::runtime_error("null validation address manager ptr");
   }
   auto thrLbd = [this](void)->void
   {
      processNotification();
   };

   processThr_ = std::thread(thrLbd);
}

////
void ValidationAddressACT::stop()
{
   notifQueue_.terminate();

   if (processThr_.joinable()) {
      processThr_.join();
   }
}


void AuthValidatorCallbacks::setTarget(AuthAddressValidator *target)
{
   if (!target) {
      return;
   }
   onUpdate = [target] {target->update(); };
   onRefresh = [target](const std::vector<BinaryData> &ids)
   {
      target->pushRefreshID(ids);
   };
}


struct VAMLambdas : public AuthValidatorCallbacks
{
   explicit VAMLambdas(const std::shared_ptr<ArmoryConnection> &conn)
      : connPtr(conn)
   {
      assert(conn);
      const auto &wltId = CryptoPRNG::generateRandom(12);
      walletObj_ = connPtr->instantiateWallet(wltId.toHexStr());
   }

   bool isInited() const override
   {
      return (connPtr && walletObj_);
   }

   std::string registerAddresses(const std::vector<BinaryData> &addrVec) override
   {
      return walletObj_->registerAddresses(addrVec, false);
   }

   unsigned int topBlock() const override
   {
      return connPtr->topBlock();
   }

   void pushZC(const BinaryData &tx) override
   {
      connPtr->pushZC(tx);
   }

   OutpointBatch getOutpointsForAddresses(const std::vector<BinaryData> &addrs
      , unsigned int topBlock = 0, unsigned int zcIndex = 0) const override
   {
      //keep track of txout changes in validation addresses since last seen block
      auto promPtr = std::make_shared<std::promise<OutpointBatch>>();
      auto futPtr = promPtr->get_future();
      const auto &opLbd = [this, promPtr](const OutpointBatch &batch)
      {
         promPtr->set_value(batch);
      };
      //grab all txouts
      connPtr->getOutpointsFor(addrs, opLbd, topBlock, zcIndex);
      checkFutureWait(futPtr);
      return futPtr.get();
   }

   std::vector<UTXO> getSpendableTxOuts() const override
   {
      if (!connPtr || (connPtr->state() != ArmoryState::Ready)) {
         return {};
      }
      auto promPtr = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto fut = promPtr->get_future();

      const auto &spendableCb = [this, promPtr]
         (ReturnMessage<std::vector<UTXO>> utxoVec)->void
      {
         try {
            const auto& utxos = utxoVec.get();
            if (utxos.empty()) {
               throw AuthLogicException("no utxos available");
            }
            promPtr->set_value(utxos);
         } catch (const std::exception &e) {
            promPtr->set_value({});
         }
      };
      walletObj_->getSpendableTxOutListForValue(UINT64_MAX, spendableCb);
      checkFutureWait(fut);
      return fut.get();
   }

   std::vector<UTXO> getUTXOsForAddress(const BinaryData &addr, bool withZC) const override
   {
      auto promPtr = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto fut = promPtr->get_future();
      auto utxoLbd = [this, promPtr](
         const std::vector<UTXO> &utxos)->void
      {
         try {
            if (utxos.empty()) {
               throw AuthLogicException("no UTXOs");
            }
            promPtr->set_value(utxos);
         } catch (const std::exception &) {
            promPtr->set_exception(std::current_exception());
         }
      };
      connPtr->getUTXOsForAddress(addr, utxoLbd, withZC);
      checkFutureWait(fut);
      return fut.get();
   }

   std::shared_ptr<ArmoryConnection> connPtr;

private:
   std::shared_ptr<AsyncClient::BtcWallet> walletObj_;
};


///////////////////////////////////////////////////////////////////////////////
ValidationAddressManager::ValidationAddressManager(
   const std::shared_ptr<ArmoryConnection> &conn)
   : AuthAddressValidator(std::make_shared<VAMLambdas>(conn))
{}

ValidationAddressManager::~ValidationAddressManager()
{
   if (actPtr_ != nullptr) {
      actPtr_->stop();
   }
}

////
void ValidationAddressManager::setCustomACT(const
   std::shared_ptr<ValidationAddressACT> &actPtr)
{
   //have to set the ACT before going online
   if (isReady()) {
      throw std::runtime_error("ValidationAddressManager is already online");
   }
   if (lambdas_) {
      actPtr_ = actPtr;
      actPtr_->setCallbacks(lambdas_);
      lambdas_->setTarget(this);
      actPtr_->start();
   }
}

void ValidationAddressManager::prepareCallbacks()
{
   //use default ACT is none is set
   if (actPtr_) {
      return;
   }
   const auto lambdas = std::dynamic_pointer_cast<VAMLambdas>(lambdas_);
   if (lambdas) {
      actPtr_ = std::make_shared<ValidationAddressACT>(lambdas->connPtr.get());

      //set the act manager ptr to process notifications
      actPtr_->setCallbacks(lambdas);
      actPtr_->start();
   }
}

////
void AuthAddressValidator::pushRefreshID(const std::vector<BinaryData> &idVec)
{
   for (auto id : idVec) {
      refreshQueue_.push_back(std::move(id));
   }
}

////
void AuthAddressValidator::waitOnRefresh(const std::string& id)
{
   BinaryDataRef idRef;
   idRef.setRef(id);

   while (true) {
      auto&& notifId = refreshQueue_.pop_front();
      if (notifId == idRef) {
         break;
      }
   }
}

////
std::shared_ptr<ValidationAddressStruct> 
AuthAddressValidator::getValidationAddress(const BinaryData& addr)
{
   auto iter = validationAddresses_.find(addr);
   if (iter == validationAddresses_.end()) {
      return nullptr;
   }
   //acquire to make sure we see update thread changes
   auto ptrCopy = std::atomic_load_explicit(
      &iter->second, std::memory_order_acquire);
   return ptrCopy;
}

////
const std::shared_ptr<ValidationAddressStruct>
AuthAddressValidator::getValidationAddress(const BinaryData& addr) const
{
   auto iter = validationAddresses_.find(addr);
   if (iter == validationAddresses_.end()) {
      return nullptr;
   }
   //acquire to make sure we see update thread changes
   auto ptrCopy = std::atomic_load_explicit(
      &iter->second, std::memory_order_acquire);
   return ptrCopy;
}

////
void AuthAddressValidator::addValidationAddress(const bs::Address &addr)
{
   validationAddresses_.insert({ addr.prefixed()
      , std::make_shared<ValidationAddressStruct>() });
}

////
bool AuthAddressValidator::goOnline()
{  /*
   For the sake of simplicity, this assumes the BDV is already online.
   This process is therefor equivalent to registering the validation addresses,
   waiting for the notification and grabbing all txouts for each address.

   Again, for the sake of simplicity, this method blocks untill the setup
   is complete.

   You cannot change the validation address list post setup. You need to 
   destroy this object and create a new one with the updated list.
   */

   if (!lambdas_ || !lambdas_->isInited()) {
      return false;
   }

   //pthread_once behavior
   if (ready_.load(std::memory_order_relaxed)) {
      return true;
   }
   prepareCallbacks();
   lambdas_->setTarget(this);

   //register validation addresses
   std::vector<BinaryData> addrVec;

   for (auto& addrPair : validationAddresses_) {
      addrVec.push_back(addrPair.first);
   }
   auto &&regID = lambdas_->registerAddresses(addrVec);
   waitOnRefresh(regID);

   update();

   //find & set first outpoints
   for (auto& maPair : validationAddresses_) {
      const auto& maStruct = *maPair.second.get();

      std::shared_ptr<AuthOutpoint> aopPtr;
      BinaryDataRef txHash;
      for (auto& hashPair : maStruct.outpoints_) {
         for (auto& opPair : hashPair.second) {
            if (*opPair.second < aopPtr) {
               aopPtr = opPair.second;
               txHash = hashPair.first.getRef();
            }
         }
      }

      if (aopPtr == nullptr || aopPtr->isZc()) {
         throw std::runtime_error(
            "validation address has no valid first outpoint");
      }

      maPair.second->firstOutpointHash_ = txHash;
      maPair.second->firstOutpointIndex_ = aopPtr->txOutIndex();
   }

   //set ready & return outpoint count
   ready_.store(true, std::memory_order_relaxed);
   return true;
}

////
void AuthAddressValidator::update()
{
   if (!lambdas_) {
      return;
   }
   std::vector<BinaryData> addrVec;
   for (auto& addrPair : validationAddresses_) {
      addrVec.push_back(addrPair.first);
   }
   const auto &batch = lambdas_->getOutpointsForAddresses(addrVec, topBlock_, zcIndex_);

   unsigned opCount = 0;
   for (auto& outpointPair : batch.outpoints_) {
      auto& outpointVec = outpointPair.second;
      if (outpointVec.size() == 0) {
         continue;
      }
      opCount += outpointVec.size();

      //create copy of validation address struct
      auto updateValidationAddrStruct = std::make_shared<ValidationAddressStruct>();

      //get existing address struct
      auto maIter = validationAddresses_.find(outpointPair.first);
      if (maIter != validationAddresses_.end()) {
         /*
         Copy the existing struct over to the new one.

         While all notification based callers of update() come from the
         same thread, it is called by goOnline() once, from a thread we
         do not control, therefor the copy of the existing struct into
         the new one is preceded by an acquire operation.
         */
         auto maStruct = std::atomic_load_explicit(
            &maIter->second, std::memory_order_acquire);
         *updateValidationAddrStruct = *maStruct;
      } else {
         //can't be missing a validation address
         throw std::runtime_error("missing validation address");
      }

      //populate new outpoints
      for (auto& op : outpointVec) {
         auto aop = std::make_shared<AuthOutpoint>(
            op.txHeight_, op.txIndex_, op.txOutIndex_,
            op.value_, op.isSpent_, op.spenderHash_);

         auto hashIter = updateValidationAddrStruct->outpoints_.find(op.txHash_);
         if (hashIter == updateValidationAddrStruct->outpoints_.end()) {
            hashIter = updateValidationAddrStruct->outpoints_.insert(std::make_pair(
               op.txHash_,
               std::map<unsigned, std::shared_ptr<AuthOutpoint>>())).first;
         }

         //update existing outpoints if the spent flag is set
         auto fIter = hashIter->second.find(aop->txOutIndex());
         if (fIter != hashIter->second.end()) {
            aop->updateFrom(*fIter->second);

            //remove spender hash entry as the ref will die after this swap
            if (fIter->second->isSpent()) {
               updateValidationAddrStruct->spenderHashes_.erase(
                  fIter->second->spenderHash().getRef());
            }
            fIter->second = aop;
            if (op.isSpent_) {
               //set valid spender hash ref
               updateValidationAddrStruct->spenderHashes_.insert(
                  fIter->second->spenderHash().getRef());
            }
            continue;
         }

         hashIter->second.emplace(std::make_pair(aop->txOutIndex(), aop));
         if (op.isSpent_) {
            //we can just insert the spender hash without worry, as it wont fail to
            //replace an expiring reference
            updateValidationAddrStruct->spenderHashes_.insert(aop->spenderHash().getRef());
         }
      }

      //store with release semantics to make the changes visible to reader threads
      std::atomic_store_explicit(
         &maIter->second, updateValidationAddrStruct, std::memory_order_release);
   }

   //update cutoffs
   topBlock_ = batch.heightCutoff_ + 1;
   zcIndex_ = batch.zcIndexCutoff_;
}

////
bool AuthAddressValidator::isValid(const bs::Address& addr) const
{
   return isValid(addr.prefixed());
}

bool AuthAddressValidator::isValid(const BinaryData& addr) const
{
   auto maStructPtr = getValidationAddress(addr);
   if (maStructPtr == nullptr) {
      return false;
   }
   auto firstOutpoint = maStructPtr->getFirsOutpoint();
   if (firstOutpoint == nullptr) {
      throw std::runtime_error("uninitialized first output");
   }
   if (!firstOutpoint->isValid()) {
      return false;
   }
   if (firstOutpoint->isSpent()) {
      return false;
   }
   return true;
}

////
UTXO AuthAddressValidator::getVettingUtxo(const bs::Address &validationAddr
   , const std::vector<UTXO> &utxos, size_t nbOutputs) const
{
   const uint64_t amountThreshold = nbOutputs * kAuthValueThreshold + 1000;
   for (const auto& utxo : utxos) {
      //find the validation address for this utxo
      auto scrAddr = utxo.getRecipientScrAddr();

      //filter by desired validation address if one was provided
      if (!validationAddr.empty() && (scrAddr != validationAddr.prefixed())) {
         continue;
      }
      auto maStructPtr = getValidationAddress(scrAddr);
      if (maStructPtr == nullptr) {
         continue;
      }
      //is validation address valid?
      if (!isValid(scrAddr)) {
         continue;
      }
      //The first utxo of a validation address isn't eligible to vet
      //user addresses with. Filter that out.

      if (maStructPtr->isFirstOutpoint(utxo.getTxHash(), utxo.getTxOutIndex())) {
         continue;
      }
      //utxo should have enough value to cover vetting amount +
      //vetting tx fee + return tx fee
      if (utxo.getValue() < amountThreshold) {
         continue;
      }
      return utxo;
   }
   return {};
}

std::vector<UTXO> AuthAddressValidator::filterVettingUtxos(
   const bs::Address &validationAddr, const std::vector<UTXO> &utxos) const
{
   std::vector<UTXO> result;
   const uint64_t amountThreshold = kAuthValueThreshold + 1000;
   for (const auto& utxo : utxos) {
      //find the validation address for this utxo
      auto scrAddr = utxo.getRecipientScrAddr();

      //filter by desired validation address if one was provided
      if (!validationAddr.empty() && (scrAddr != validationAddr.prefixed())) {
         continue;
      }
      auto maStructPtr = getValidationAddress(scrAddr);
      if (maStructPtr == nullptr) {
         continue;
      }
      //is validation address valid?
      if (!isValid(scrAddr)) {
         continue;
      }
      //The first utxo of a validation address isn't eligible to vet
      //user addresses with. Filter that out.

      if (maStructPtr->isFirstOutpoint(utxo.getTxHash(), utxo.getTxOutIndex())) {
         continue;
      }
      //utxo should have enough value to cover vetting amount +
      //vetting tx fee + return tx fee
      if (utxo.getValue() < amountThreshold) {
         continue;
      }
      result.push_back(utxo);
   }
   return result;
}

////
BinaryData AuthAddressValidator::fundUserAddress(
   const bs::Address& addr,
   std::shared_ptr<ResolverFeed> feedPtr,
   const bs::Address& validationAddr) const
{  /*
   To vet a user address, send it coins from a validation address.
            const auto vettingUtxos = filterVettingUtxos(validationAddr, utxos);
            if (vettingUtxos.empty()) {
               throw AuthLogicException("no vetting UTXOs found");
            }
   */

   if (!lambdas_) {
      throw AuthLogicException("no lambdas set");
   }
   const auto &opBatch = lambdas_->getOutpointsForAddresses({ addr });
   if (!opBatch.outpoints_.empty()) {
      throw AuthLogicException("can only vet virgin user addresses");
   }

   std::unique_lock<std::mutex> lock(vettingMutex_);

   //#2: grab a utxo from a validation address

   const auto &utxos = lambdas_->getSpendableTxOuts();
   const auto utxo = getVettingUtxo(validationAddr, utxos);
   if (!utxo.isInitialized()) {
      throw AuthLogicException("vetting UTXO is uninited");
   }

   return fundUserAddress(addr, feedPtr, utxo);
}

// fundUserAddress was divided because actual signing will be performed
// in OT which doesn't have access to ArmoryConnection
BinaryData AuthAddressValidator::fundUserAddress(
   const bs::Address& addr,
   std::shared_ptr<ResolverFeed> feedPtr,
   const UTXO &vettingUtxo) const
{
   //#3: create vetting tx
   Signer signer;
   signer.setFeed(feedPtr);

   //spender
   auto spenderPtr = std::make_shared<ScriptSpender>(vettingUtxo);
   signer.addSpender(spenderPtr);

   //vetting output
   signer.addRecipient(addr.getRecipient(bs::XBTAmount{ kAuthValueThreshold }));

   const auto scrAddr = vettingUtxo.getRecipientScrAddr();
   const auto addrIter = validationAddresses_.find(scrAddr);
   if (addrIter == validationAddresses_.end()) {
      throw AuthLogicException("input addr not found in validation addresses");
   }

   //change: vetting coin value + fee
   const int64_t changeVal = vettingUtxo.getValue() - kAuthValueThreshold - 1000;
   if (changeVal < 0) {
      throw AuthLogicException("insufficient spend volume");
   }
   else if (changeVal > 0) {
      auto&& addrObj = bs::Address::fromHash(addrIter->first);
      signer.addRecipient(addrObj.getRecipient(bs::XBTAmount{ static_cast<uint64_t>(changeVal) }));
   }

   //sign & serialize tx
   signer.sign();
   return signer.serializeSignedTx();
}

BinaryData AuthAddressValidator::fundUserAddresses(
   const std::vector<bs::Address> &addrs
   , const bs::Address &validationAddress
   , std::shared_ptr<ResolverFeed> feedPtr
   , const std::vector<UTXO> &vettingUtxos, int64_t totalFee) const
{
   Signer signer;
   signer.setFeed(feedPtr);

   //vetting outputs
   for (const auto &addr : addrs) {
      signer.addRecipient(addr.getRecipient(bs::XBTAmount{ kAuthValueThreshold }));
   }

   int64_t changeVal = 0;
   //spenders
   for (const auto &vettingUtxo : vettingUtxos) {
      auto spenderPtr = std::make_shared<ScriptSpender>(vettingUtxo);
      signer.addSpender(spenderPtr);

      const auto scrAddr = vettingUtxo.getRecipientScrAddr();
      const auto addrIter = validationAddresses_.find(scrAddr);
      if (addrIter == validationAddresses_.end()) {
         throw AuthLogicException("input addr not found in validation addresses");
      }
      changeVal += vettingUtxo.getValue();
   }
   changeVal -= addrs.size() * kAuthValueThreshold;
   changeVal -= totalFee;

   if (changeVal < 0) {
      throw AuthLogicException("attempting to spend more than allowed");
   }
   else if (changeVal > 0) {
      signer.addRecipient(validationAddress.getRecipient(bs::XBTAmount{ static_cast<uint64_t>(changeVal) }));
   }

   //sign & serialize tx
   signer.sign();
   return signer.serializeSignedTx();
}

BinaryData AuthAddressValidator::vetUserAddress(const bs::Address& addr
   , std::shared_ptr<ResolverFeed> feedPtr, const bs::Address& validationAddr) const
{
   if (!lambdas_) {
      throw AuthLogicException("no lambdas set");
   }
   const auto signedTx = fundUserAddress(addr, feedPtr, validationAddr);

   //broadcast the zc
   lambdas_->pushZC(signedTx);

   Tx txObj(signedTx);
   return txObj.getThisHash();
}

////
BinaryData AuthAddressValidator::revokeValidationAddress(
   const bs::Address& addr, std::shared_ptr<ResolverFeed> feedPtr) const
{  /*
   To revoke a validation address, spend its first UTXO.
   */
   if (!lambdas_) {
      throw AuthLogicException("no lambdas set");
   }

   //find the MA
   auto maStructPtr = getValidationAddress(addr);
   if (maStructPtr == nullptr) {
      throw AuthLogicException("unknown validation address!");
   }
   std::unique_lock<std::mutex> lock(vettingMutex_);

   //grab UTXOs
   const auto &utxos = lambdas_->getSpendableTxOuts();
   if (utxos.size() == 0) {
      throw AuthLogicException("no utxo to revoke");
   }

   UTXO firstUtxo;
   for (const auto &utxo : utxos) {
      if (!maStructPtr->isFirstOutpoint(
         utxo.getTxHash(), utxo.getTxOutIndex())) {
         firstUtxo = utxo;
         break;
      }
   }
   if (!firstUtxo.isInitialized()) {
      throw AuthLogicException("could not select first outpoint");
   }

   //spend it
   Signer signer;
   signer.setFeed(feedPtr);

   //spender
   auto spenderPtr = std::make_shared<ScriptSpender>(firstUtxo);
   signer.addSpender(spenderPtr);

   //revocation output, no need for change
   const uint64_t revokeAmount = firstUtxo.getValue() - 1000;
   signer.addRecipient(addr.getRecipient(bs::XBTAmount{revokeAmount}));

   //sign & serialize tx
   signer.sign();
   auto signedTx = signer.serializeSignedTx();
   if (signedTx.empty()) {
      throw AuthLogicException("failed to sign");
   }
   //broadcast the zc
   lambdas_->pushZC(signedTx);

   Tx txObj(signedTx);
   return txObj.getThisHash();
}

BinaryData AuthAddressValidator::revokeUserAddress(
   const bs::Address& addr, std::shared_ptr<ResolverFeed> feedPtr)
{
   /*
   To revoke a user address from a validation address, send it coins from
   its own validation address.
   */
   if (!lambdas_) {
      throw AuthLogicException("no lambdas set");
   }

   //1: find validation address vetting this address
   size_t foo;
   auto paths = AuthAddressLogic::getValidPaths(*this, addr, foo);
   if (paths.size() != 1) {
      throw AuthLogicException("invalid user auth address");
   }
   auto& validationAddr = findValidationAddressForTxHash(paths[0].txHash_);
   if (validationAddr.empty()) {
      throw AuthLogicException("invalidated validation address");
   }
   auto validationAddrPtr = getValidationAddress(validationAddr);

   std::unique_lock<std::mutex> lock(vettingMutex_);

   //2: get utxo from the validation address
   const auto &utxos = lambdas_->getUTXOsForAddress(validationAddr);
   UTXO addrUtxo;
   for (const auto &utxo : utxos) {
      //cannot use the validation address first utxo
      if (validationAddrPtr->isFirstOutpoint(
         utxo.getTxHash(), utxo.getTxOutIndex())) {
         continue;
      }
      if (utxo.getValue() < kAuthValueThreshold + 1000ULL) {
         continue;
      }
      addrUtxo = utxo;
      break;
   }

   //3: spend to the user address
   Signer signer;
   signer.setFeed(feedPtr);

   //spender
   auto spenderPtr = std::make_shared<ScriptSpender>(addrUtxo);
   signer.addSpender(spenderPtr);

   //revocation output
   signer.addRecipient(addr.getRecipient(bs::XBTAmount{ kAuthValueThreshold }));

   //change
   {
      const bs::XBTAmount changeAmount{ addrUtxo.getValue() - kAuthValueThreshold - 1000 };
      auto addrObj = bs::Address::fromHash(validationAddr);
      signer.addRecipient(addrObj.getRecipient(changeAmount));
   }

   //sign & serialize tx
   signer.sign();
   auto signedTx = signer.serializeSignedTx();

   //broadcast the zc
   lambdas_->pushZC(signedTx);

   Tx txObj(signedTx);
   return txObj.getThisHash();
}

////
bool AuthAddressValidator::hasSpendableOutputs(const bs::Address& addr) const
{
   auto& maStruct = getValidationAddress(addr);

   for (auto& outpointSet : maStruct->outpoints_) {
      for (auto& outpoint : outpointSet.second) {
         //ZC outputs are not eligible to vet with
         if (!outpoint.second->isSpent() && !outpoint.second->isZc()) {
            //nor is the first outpoint
            if (maStruct->isFirstOutpoint(
               outpointSet.first, outpoint.second->txOutIndex())) {
               continue;
            }
            return true;
         }
      }
   }
   return false;
}

////
bool AuthAddressValidator::hasZCOutputs(const bs::Address& addr) const
{
   auto iter = validationAddresses_.find(addr.prefixed());
   if (iter == validationAddresses_.end()) {
      throw std::runtime_error("unknown validation address");
   }
   for (auto& outpointSet : iter->second->outpoints_) {
      for (auto& outpoint : outpointSet.second) {
         if (outpoint.second->isZc()) {
            return true;
         }
      }
   }
   return false;
}

////
const BinaryData& AuthAddressValidator::findValidationAddressForUTXO(
   const UTXO& utxo) const
{
   return findValidationAddressForTxHash(utxo.getTxHash());
}

////
const BinaryData& AuthAddressValidator::findValidationAddressForTxHash(
   const BinaryData& txHash) const
{
   for (auto& maPair : validationAddresses_) {
      auto iter = maPair.second->spenderHashes_.find(txHash);
      if (iter == maPair.second->spenderHashes_.end()) {
         continue;
      }
      return maPair.first;
   }
   throw std::runtime_error("no validation address spends to that hash");
}

unsigned int AuthAddressValidator::topBlock() const
{
   return lambdas_ ? lambdas_->topBlock() : UINT32_MAX;
}

OutpointBatch AuthAddressValidator::getOutpointsFor(const bs::Address &addr) const
{
   return lambdas_ ? lambdas_->getOutpointsForAddresses({ addr }) : OutpointBatch{};
}

std::vector<UTXO> AuthAddressValidator::getUTXOsFor(const bs::Address &addr
   , bool withZC) const
{
   return lambdas_ ? lambdas_->getUTXOsForAddress(addr.prefixed(), withZC)
      : std::vector<UTXO>{};
}

void AuthAddressValidator::pushZC(const BinaryData &tx) const
{
   if (lambdas_) {
      lambdas_->pushZC(tx);
   }
}


///////////////////////////////////////////////////////////////////////////////
std::vector<OutpointData> AuthAddressLogic::getValidPaths(
   const AuthAddressValidator &aav, const bs::Address &addr, size_t &nbPaths)
{
   std::vector<OutpointData> validPaths;
   nbPaths = 0;

   //get txout history for address
   const auto &opMap = aav.getOutpointsFor(addr).outpoints_;
   if (opMap.size() != 1) {
      //sanity check on the address history
      throw AuthLogicException(
         "unexpected result from getOutpointsForAddresses");
   }

   auto& opVec = opMap.begin()->second;
   nbPaths = opVec.size();

   //check all spent outputs vs ValidationAddressManager
   for (auto& outpoint : opVec) {
      try {
         /*
         Does this txHash spend from a validation address output? It will
         throw if not.
         */
         auto& validationAddr = 
            aav.findValidationAddressForTxHash(outpoint.txHash_);

         /*
         If relevant validation address is invalid, this address is invalid,
         regardless of any other path states.
         */
         if (!aav.isValid(validationAddr)) {
            throw AuthLogicException(
               "Address is vetted by invalid validation address");
         }

         /*
         Is the validation output spent? Spending it revokes the
         address.
         */
         if (outpoint.isSpent_) {
            throw AuthLogicException(
               "Address has been revoked");
         }

         validPaths.push_back(outpoint);
      }
      catch (const std::exception &) {
         continue;
      }
   }
   return validPaths;
}

bool AuthAddressLogic::isValid(const AuthAddressValidator &aav, const bs::Address &addr)
{
   return (getAuthAddrState(aav, addr) == AddressVerificationState::Verified);
}

////
AddressVerificationState AuthAddressLogic::getAuthAddrState(
   const AuthAddressValidator &aav, const bs::Address& addr)
{  /***
   Validity is unique. This means there should be only one output chain
   defining validity. Any concurent path, whether partial or full,
   invalidates the user address.
   ***/

   auto currentTop = aav.topBlock();
   if (currentTop == UINT32_MAX) {
      throw std::runtime_error("invalid top height");
   }

   try {
      size_t nbPaths = 0;
      auto&& validPaths = getValidPaths(aav, addr, nbPaths);

      //is there only 1 valid path?
      if (validPaths.empty()) {
         return (nbPaths > 0) ? AddressVerificationState::Revoked
            : AddressVerificationState::NotSubmitted;
      }
      else if (validPaths.size() > 1) {
         return AddressVerificationState::Revoked;
      }
      auto& outpoint = validPaths[0];

      //does this path have enough confirmations?
      auto opHeight = outpoint.txHeight_;
      if (currentTop >= opHeight &&
         (1 + currentTop - opHeight) >= VALIDATION_CONF_COUNT) {
         return AddressVerificationState::Verified;
      }
      return AddressVerificationState::PendingVerification;
   }
   catch (const AuthLogicException &) { }

   return AddressVerificationState::NotSubmitted;
}

////
std::pair<bs::Address, UTXO> AuthAddressLogic::getRevokeData(
   const AuthAddressValidator &aav, const bs::Address &addr)
{
   //get valid paths for address
   size_t foo;
   auto&& validPaths = getValidPaths(aav, addr, foo);

   //is there only 1 valid path?
   if (validPaths.size() != 1) {
      throw AuthLogicException("address has no valid paths");
   }
   auto& outpoint = validPaths[0];

   /*
   We do not check auth output maturation when revoking.
   A yet to be confirmed valid path can be revoked.
   */

   //grab UTXOs for address
   auto promPtr = std::make_shared<std::promise<UTXO>>();
   auto fut = promPtr->get_future();
   auto utxosLbd = [&outpoint, promPtr]
   (const std::vector<UTXO> utxos)->void
   {
      try {
         if (utxos.empty()) {
            throw std::runtime_error("no UTXOs found");
         }
         /*
         Throw if we can't find the outpoint to revoke within the
         address' utxos, as this indicates our auth state is
         corrupt.
         */
         throw std::runtime_error("could not find utxo to revoke");
      } catch (const std::exception_ptr &e) {
         promPtr->set_exception(e);
      }
   };

   const auto &utxos = aav.getUTXOsFor(addr, true);
   UTXO revokeUtxo;
   for (auto& utxo : utxos) {
      if (utxo.getTxHash() == outpoint.txHash_ &&
         utxo.getTxOutIndex() == outpoint.txOutIndex_) {
         revokeUtxo = utxo;
         break;
      }
   }

   //we're sending the coins back to the relevant validation address
   auto& validationAddr = aav.findValidationAddressForUTXO(revokeUtxo);
   auto addrObj = bs::Address::fromHash(validationAddr);
   return { addrObj, revokeUtxo };
}

BinaryData AuthAddressLogic::revoke(const AuthAddressValidator &aav,
   const bs::Address &addr, const std::shared_ptr<ResolverFeed> &feedPtr)
{
   const auto revokeData = getRevokeData(aav, addr);
   const auto signedTx = revoke(addr, feedPtr, revokeData.first, revokeData.second);

   //sign and broadcast, return the txHash
   Tx txObj(signedTx);
   aav.pushZC(signedTx);

   return txObj.getThisHash();
}

BinaryData AuthAddressLogic::revoke(const bs::Address &
   , const std::shared_ptr<ResolverFeed> &feedPtr
   , const bs::Address &, const UTXO &revokeUtxo)
{
   //User side revoke: burn the validation UTXO as an OP_RETURN
   Signer signer;
   signer.setFeed(feedPtr);
   signer.addSpender(std::make_shared<ScriptSpender>(revokeUtxo));

   const std::string opReturnMsg = "BlockSettle Terminal revoke";
   signer.addRecipient(std::make_shared<Recipient_OPRETURN>(BinaryData::fromString(opReturnMsg)));

   signer.sign();
   return signer.serializeSignedTx();
}

std::vector<UTXO> AuthAddressValidator::filterAuthFundingUTXO(const std::vector<UTXO>& authInputs)
{
   std::vector<UTXO> result;

   for (const auto& utxo : authInputs) try {
      const auto authAddr = utxo.getRecipientScrAddr();
      auto maStructPtr = getValidationAddress(authAddr);
      if (maStructPtr == nullptr) {
         continue;
      }

      if (!isValid(authAddr)) {
         continue;
      }

      if (maStructPtr->isFirstOutpoint(utxo.getTxHash(), utxo.getTxOutIndex())) {
         continue;
      }

      result.emplace_back(utxo);
   } catch (...) {
      continue;
   }

   return result;
}
