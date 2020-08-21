/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthAddressLogic.h"

using namespace ArmorySigner;

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
   //don't overwrite existing target
   if (!target || onUpdate) {
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

   std::string registerAddresses(const std::vector<bs::Address> &addrVec) override
   {
      std::vector<BinaryData> pfxAddrs;
      pfxAddrs.reserve(addrVec.size());
      for (const auto &addr : addrVec) {
         pfxAddrs.push_back(addr.prefixed());
      }
      return walletObj_->registerAddresses(pfxAddrs, false);
   }

   unsigned int topBlock() const override
   {
      return connPtr->topBlock();
   }

   void pushZC(const BinaryData &tx) override
   {
      connPtr->pushZC(tx);
   }

   void getOutpointsForAddresses(const std::vector<bs::Address> &addrs
      , const OutpointsCb &cb, unsigned int topBlock = 0
      , unsigned int zcIndex = 0) override
   {
      const auto &opLbd = [this, cb](const OutpointBatch &batch)
      {
         if (cb) {
            cb(batch);
         }
      };
      std::vector<BinaryData> addrsPrefixed;
      addrsPrefixed.reserve(addrs.size());
      for (const auto &addr : addrs) {
         addrsPrefixed.push_back(addr.prefixed());
      }
      connPtr->getOutpointsFor(addrsPrefixed, opLbd, topBlock, zcIndex);
   }

   void getSpendableTxOuts(const UTXOsCb &cb) override
   {
      if (!connPtr || (connPtr->state() != ArmoryState::Ready)) {
         if (cb) {
            cb({});
         }
         return;
      }
      const auto &spendableCb = [this, cb]
         (ReturnMessage<std::vector<UTXO>> utxoVec)->void
      {
         try {
            const auto& utxos = utxoVec.get();
            if (cb) {
               cb(utxos);
            }
         } catch (const std::exception &e) {
            if (cb) {
               cb({});
            }
         }
      };
      walletObj_->getSpendableTxOutListForValue(UINT64_MAX, spendableCb);
   }

   void getUTXOsForAddress(const bs::Address &addr, const UTXOsCb &cb
      , bool withZC) override
   {
      auto utxoLbd = [this, cb](const std::vector<UTXO> &utxos)
      {
         if (cb) {
            cb(utxos);
         }
      };
      connPtr->getUTXOsForAddress(addr.prefixed(), utxoLbd, withZC);
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
   //use default ACT if none is set
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
AuthAddressValidator::~AuthAddressValidator()
{
   stopped_ = true;
   if (updateThread_.joinable()) {
      updateThread_.join();
   }
   refreshQueue_.terminate();
   if (lambdas_) {
      lambdas_->shutdown();
   }
}

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
      try
      {
         auto&& notifId = refreshQueue_.pop_front(std::chrono::seconds{ 5 });
         if ((notifId == idRef) || stopped_) {
            break;
         }
      }
      catch (const ArmoryThreading::StopBlockingLoop&) {
         break;
      }
      catch (const ArmoryThreading::StackTimedOutException &) {
         if (stopped_) {
            break;
         }
      }
   }
}

////
std::shared_ptr<ValidationAddressStruct>
AuthAddressValidator::getValidationAddress(const bs::Address &addr) const
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
   /*
   goOnline should be called from the same thread that populates the 
   list of validation address.
   */
   if (ready_.load(std::memory_order_relaxed)) {
      throw std::runtime_error("cannot modify validation address list "
                              "after going online");
   }
   validationAddresses_[addr] = std::make_shared<ValidationAddressStruct>();
}

////
bool AuthAddressValidator::goOnline(const ResultCb &cb)
{  /*
   For the sake of simplicity, this assumes the BDV is already online.
   This process is therefor equivalent to registering the validation addresses,
   waiting for the notification and grabbing all txouts for each address.

   Again, for the sake of simplicity, this method blocks until the setup
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
   std::vector<bs::Address> addrVec;
   for (auto& addrPair : validationAddresses_) {
      addrVec.push_back(addrPair.first);
   }
   const auto &regID = lambdas_->registerAddresses(addrVec);

   if (updateThreadRunning_) {
      if (cb) {
         cb(true);
      }
      return true;
   }
   else {   // Can't assign to a non-joined thread
      if (updateThread_.joinable()) {
         updateThread_.join();
      }
   }

   updateThreadRunning_ = true;
   updateThread_ = std::thread([this, regID, cb] {
      waitOnRefresh(regID);

      if (stopped_) {
         if (cb) {
            cb(false);
         }
         return;
      }
      update();

      //find & set first outpoints
      for (auto& maPair : validationAddresses_) {
         const auto& maStruct = *maPair.second.get();
         if (stopped_) {
            if (cb) {
               cb(false);
            }
            return;
         }

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
            if (cb) {
               cb(false);
            }
            throw AuthLogicException(
               "validation address has no valid first outpoint");
         }

         maPair.second->firstOutpointHash_ = txHash;
         maPair.second->firstOutpointIndex_ = aopPtr->txOutIndex();
      }

      //set ready & return outpoint count
      ready_.store(true, std::memory_order_relaxed);

      if (cb) {
         cb(true);
      }
      updateThreadRunning_ = false;
   });
   return true;
}

////
unsigned AuthAddressValidator::update()
{
   if (stopped_) {
      return 0;
   }
   //keep track of txout changes in validation addresses since last seen block
   auto promPtr = std::make_shared<std::promise<OutpointBatch>>();
   auto futPtr = promPtr->get_future();
   const auto &opLbd = [this, promPtr](const OutpointBatch &batch)
   {
      promPtr->set_value(batch);
   };
   getValidationOutpointsBatch(opLbd);
   try {
      checkFutureWait(futPtr);
   }
   catch (const std::exception &) {
      return 0;
   }
   return update(futPtr.get());
}

void AuthAddressValidator::getValidationOutpointsBatch(const std::function<void(OutpointBatch)> &cb)
{
   if (!lambdas_) {
      return;
   }
   std::vector<bs::Address> addrVec;
   for (auto& addrPair : validationAddresses_) {
      addrVec.push_back(addrPair.first);
   }
   //grab all txouts
   lambdas_->getOutpointsForAddresses(addrVec, cb, topBlock_, zcIndex_);
}

unsigned AuthAddressValidator::update(const OutpointBatch &batch)
{
   std::unique_lock<std::mutex> lock(updateMutex_);
   if (!lambdas_ || stopped_) {
      return UINT32_MAX;
   }
   unsigned opCount = 0;
   for (const auto &outpointPair : batch.outpoints_) {
      if (stopped_) {
         return UINT32_MAX;
      }
      const auto& outpointVec = outpointPair.second;
      if (outpointVec.size() == 0) {
         continue;
      }
      opCount += outpointVec.size();

      //create copy of validation address struct
      auto updateValidationAddrStruct = std::make_shared<ValidationAddressStruct>();

      //get existing address struct
      auto maIter = validationAddresses_.find(bs::Address::fromPrefixed(outpointPair.first));
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
         throw AuthLogicException("missing validation address");
      }

      //populate new outpoints
      for (const auto& op : outpointVec) {
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

   return opCount;
}

////
bool AuthAddressValidator::isValidMasterAddress(const bs::Address &addr) const
{
   auto maStructPtr = getValidationAddress(addr);
   if (maStructPtr == nullptr) {
      return false;
   }
   auto firstOutpoint = maStructPtr->getFirsOutpoint();
   if (firstOutpoint == nullptr) {
      throw AuthLogicException("uninitialized first output");
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
      const auto &addr = bs::Address::fromUTXO(utxo);

      //filter by desired validation address if one was provided
      if (!validationAddr.empty() && (addr != validationAddr)) {
         continue;
      }
      auto maStructPtr = getValidationAddress(addr);
      if (maStructPtr == nullptr) {
         continue;
      }
      //is validation address valid?
      if (!isValidMasterAddress(addr)) {
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
      const auto &addr = bs::Address::fromUTXO(utxo);

      //filter by desired validation address if one was provided
      if (!validationAddr.empty() && (addr != validationAddr)) {
         continue;
      }
      auto maStructPtr = getValidationAddress(addr);
      if (maStructPtr == nullptr) {
         continue;
      }
      //is validation address valid?
      if (!isValidMasterAddress(addr)) {
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
{
   if (!lambdas_) {
      throw std::runtime_error("no lambdas set");
   }
   const auto &opBatch = getOutpointsForAddresses({ addr });
   if (!opBatch.outpoints_.empty()) {
      throw AuthLogicException("can only vet virgin user addresses");
   }

   std::unique_lock<std::mutex> lock(vettingMutex_);

   //#2: grab a utxo from a validation address

   const auto &utxos = getSpendableTxOuts();
   const auto utxo = getVettingUtxo(validationAddr, utxos);
   if (!utxo.isInitialized()) {
      throw AuthLogicException("missing vetting UTXO");
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
   ArmorySigner::Signer signer;
   signer.setFeed(feedPtr);

   //spender
   auto spenderPtr = std::make_shared<ArmorySigner::ScriptSpender>(vettingUtxo);
   signer.addSpender(spenderPtr);

   //vetting output
   signer.addRecipient(addr.getRecipient(bs::XBTAmount{ kAuthValueThreshold }));

   const auto &vettingAddr = bs::Address::fromUTXO(vettingUtxo);
   const auto addrIter = validationAddresses_.find(vettingAddr);
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
   ArmorySigner::Signer signer;
   signer.setFeed(feedPtr);

   //vetting outputs
   for (const auto &addr : addrs) {
      signer.addRecipient(addr.getRecipient(bs::XBTAmount{ kAuthValueThreshold }));
   }

   int64_t changeVal = 0;
   //spenders
   for (const auto &vettingUtxo : vettingUtxos) {
      auto spenderPtr = std::make_shared<ArmorySigner::ScriptSpender>(vettingUtxo);
      signer.addSpender(spenderPtr);

      const auto &addr = bs::Address::fromUTXO(vettingUtxo);
      const auto &addrIter = validationAddresses_.find(addr);
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
      throw std::runtime_error("no lambdas set");
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
      throw std::runtime_error("no lambdas set");
   }

   //find the MA
   auto maStructPtr = getValidationAddress(addr);
   if (maStructPtr == nullptr) {
      throw AuthLogicException("unknown validation address!");
   }
   std::unique_lock<std::mutex> lock(vettingMutex_);

   //grab UTXOs
   const auto &utxos = getSpendableTxOuts();
   if (utxos.size() == 0) {
      throw AuthLogicException("no utxo to revoke");
   }

   UTXO firstUtxo;
   for (const auto &utxo : utxos) {
      if (maStructPtr->isFirstOutpoint(
         utxo.getTxHash(), utxo.getTxOutIndex())) {
         firstUtxo = utxo;
         break;
      }
   }
   if (!firstUtxo.isInitialized()) {
      throw AuthLogicException("could not select first outpoint");
   }

   //spend it
   ArmorySigner::Signer signer;
   signer.setFeed(feedPtr);

   //spender
   auto spenderPtr = std::make_shared<ArmorySigner::ScriptSpender>(firstUtxo);
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
      throw std::runtime_error("no lambdas set");
   }

   //1: find validation address vetting this address
   auto paths = AuthAddressLogic::getAddrPathsStatus(*this, addr);
   const auto& validatoinOutpoint = paths.getValidationOutpoint();

   //this will throw if the validation address can't be found
   auto& validationAddr = 
      findValidationAddressForTxHash(validatoinOutpoint.txHash_);
   auto validationAddrPtr = getValidationAddress(validationAddr);

   std::unique_lock<std::mutex> lock(vettingMutex_);

   //2: get utxo from the validation address
   const auto &utxos = getUTXOsForAddress(validationAddr);
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
   ArmorySigner::Signer signer;
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
   const auto& maStruct = getValidationAddress(addr);

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
   auto iter = validationAddresses_.find(addr);
   if (iter == validationAddresses_.end()) {
      throw AuthLogicException("unknown validation address");
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
const bs::Address &AuthAddressValidator::findValidationAddressForUTXO(
   const UTXO& utxo) const
{
   return findValidationAddressForTxHash(utxo.getTxHash());
}

////
const bs::Address &AuthAddressValidator::findValidationAddressForTxHash(
   const BinaryData& txHash) const
{
   for (auto& maPair : validationAddresses_) {
      auto iter = maPair.second->spenderHashes_.find(txHash);
      if (iter == maPair.second->spenderHashes_.end()) {
         continue;
      }
      return maPair.first;
   }
   throw AuthLogicException("no validation address spends to that hash");
}

unsigned int AuthAddressValidator::topBlock() const
{
   return lambdas_ ? lambdas_->topBlock() : UINT32_MAX;
}

OutpointBatch AuthAddressValidator::getOutpointsFor(const bs::Address &addr) const
{
   return getOutpointsForAddresses({ addr });
}

void AuthAddressValidator::getOutpointsFor(const bs::Address &addr
   , const std::function<void(const OutpointBatch &)> &cb) const
{
   if (!lambdas_) {
      return;
   }
   lambdas_->getOutpointsForAddresses({ addr }, cb);
}

std::vector<UTXO> AuthAddressValidator::getUTXOsFor(const bs::Address &addr
   , bool withZC) const
{
   return getUTXOsForAddress(addr, withZC);
}

void AuthAddressValidator::pushZC(const BinaryData &tx) const
{
   if (lambdas_) {
      lambdas_->pushZC(tx);
   }
}

OutpointBatch AuthAddressValidator::getOutpointsForAddresses(const std::vector<bs::Address> &addrs
   , unsigned int topBlock, unsigned int zcIndex) const
{
   if (!lambdas_) {
      return {};
   }
   //keep track of txout changes in validation addresses since last seen block
   auto promPtr = std::make_shared<std::promise<OutpointBatch>>();
   auto futPtr = promPtr->get_future();
   const auto &opLbd = [this, promPtr](const OutpointBatch &batch)
   {
      promPtr->set_value(batch);
   };
   //grab all txouts
   lambdas_->getOutpointsForAddresses(addrs, opLbd, topBlock, zcIndex);
   checkFutureWait(futPtr);
   return futPtr.get();
}

std::vector<UTXO> AuthAddressValidator::getSpendableTxOuts() const
{
   if (!lambdas_) {
      throw std::runtime_error("no lambdas set");
   }
   auto promPtr = std::make_shared<std::promise<std::vector<UTXO>>>();
   auto fut = promPtr->get_future();

   const auto &spendableCb = [this, promPtr]
   (const std::vector<UTXO> &utxos)->void
   {
      promPtr->set_value(utxos);
   };
   lambdas_->getSpendableTxOuts(spendableCb);
   checkFutureWait(fut);

   const auto &utxos = fut.get();
   if (utxos.empty()) {
      throw AuthLogicException("no utxos available");
   }
   return utxos;
}

std::vector<UTXO> AuthAddressValidator::getUTXOsForAddress(const bs::Address &addr
   , bool withZC) const
{
   if (!lambdas_) {
      return {};
   }
   auto promPtr = std::make_shared<std::promise<std::vector<UTXO>>>();
   auto fut = promPtr->get_future();
   auto utxoLbd = [this, promPtr](const std::vector<UTXO> &utxos)
   {
      promPtr->set_value(utxos);
   };
   lambdas_->getUTXOsForAddress(addr, utxoLbd, withZC);
   checkFutureWait(fut);

   const auto &utxos = fut.get();
   if (utxos.empty()) {
      throw AuthLogicException("no UTXOs");
   }
   return utxos;
}


///////////////////////////////////////////////////////////////////////////////
AuthAddressLogic::AddrPathsStatus AuthAddressLogic::getAddrPathsStatus(
   const AuthAddressValidator &aav, const bs::Address &addr)
{
   const auto &outpoints = aav.getOutpointsFor(addr);
   return getAddrPathsStatus(aav, outpoints);
}

AuthAddressLogic::AddrPathsStatus AuthAddressLogic::getAddrPathsStatus(
   const AuthAddressValidator &aav, const OutpointBatch &batch)
{
   /*
   This code can be sped up for revoked/invalidated addresses by returning at
   the first fail condition. It returns the full path status at the moment.
   */
   AuthAddressLogic::AddrPathsStatus paths;
   //get txout history for address
   const auto &opMap = batch.outpoints_;
   if (opMap.size() == 0) {
      //no data for this address
      paths.pathCount_ = 0;
      return paths;
   } else if (opMap.size() != 1) {
      //this is an error state, don't initialize the path
      return paths;
   }

   auto& opVec = opMap.begin()->second;
   paths.pathCount_ = opVec.size();

   //check all spent outputs vs ValidationAddressManager
   for (unsigned i = 0; i < opVec.size(); i++) {
      auto& outpoint = opVec[i];
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
         if (!aav.isValidMasterAddress(validationAddr)) {
            paths.invalidPaths_.push_back(i);
            continue;
         }

         /*
         Is the validation output spent? Spending it revokes the
         address.
         */
         if (outpoint.isSpent_) {
            paths.revokedPaths_.push_back(i);
            continue;
         }

         paths.validPaths_.emplace(i, std::move(outpoint));
      } catch (const AuthLogicException &) {
         continue;
      }
   }
   return paths;
}

////////////////////////////////////////////////////////////////////////////////
bool AuthAddressLogic::isValid(
   const AuthAddressValidator &aav, const bs::Address &addr)
{
   return (getAuthAddrState(aav, addr) == AddressVerificationState::Verified);
}

////
AddressVerificationState AuthAddressLogic::getAuthAddrState(const AuthAddressValidator &aav
   , const OutpointBatch &batch)
{   /***
   Validity is unique. This means there should be only one output chain
   defining validity. Any concurent path, whether partial or full,
   invalidates the user address.
   ***/

   auto currentTop = aav.topBlock();
   if (currentTop == UINT32_MAX) {
      throw std::runtime_error("invalid top height");
   }

   const auto &pathState = getAddrPathsStatus(aav, batch);
   if (!pathState.isInitialized()) {
      // uninitialized path state, this happens on corrupt data from db
      return AddressVerificationState::VerificationFailed;
   }

   try {
      //grab the outpoint for the validation path
      const auto& outpoint = pathState.getValidationOutpoint();

      //does it have enough confirmations?
      auto opHeight = outpoint.txHeight_;
      if (currentTop >= opHeight &&
         (1 + currentTop - opHeight) >= VALIDATION_CONF_COUNT) {
         return AddressVerificationState::Verified;
      }
      return AddressVerificationState::Verifying;
   } catch (const AuthLogicException&) {
      //failed to grab the validation output, this address is invalid

      if (pathState.pathCount_ == 0) {
         //address has no history
         return AddressVerificationState::Virgin;
      }

      if (!pathState.invalidPaths_.empty()) {
         //has a validation output from a revoked validation address
         return AddressVerificationState::Invalidated_Implicit;
      }

      if (pathState.validPaths_.size() > 1) {
         //has multiple validation outputs (we explicitly revoked this)
         return AddressVerificationState::Invalidated_Explicit;
      }

      if (!pathState.revokedPaths_.empty()) {
         //validation output was spent by user
         return AddressVerificationState::Revoked;
      }
      //address has history and no validation outputs
      return AddressVerificationState::Tainted;
   }
   //logic error in getAddrPathsStatus, cannot proceed
   return AddressVerificationState::VerificationFailed;
}

AddressVerificationState AuthAddressLogic::getAuthAddrState(
   const AuthAddressValidator &aav, const bs::Address& addr)
{
   return getAuthAddrState(aav, aav.getOutpointsFor(addr));
}

////////////////////////////////////////////////////////////////////////////////
std::pair<bs::Address, UTXO> AuthAddressLogic::getRevokeData(
   const AuthAddressValidator &aav, const bs::Address &addr)
{
   //get valid paths for address
   auto addrState = getAddrPathsStatus(aav, addr);

   //is there only 1 valid path?
   const auto& outpoint = addrState.getValidationOutpoint();

   /*
   We do not check auth output maturation when revoking.
   A yet to be confirmed valid path can be revoked.
   */

   //grab UTXOs for address
   const auto &utxos = aav.getUTXOsFor(addr, true);
   UTXO revokeUtxo;
   for (auto& utxo : utxos) {
      if (utxo.getTxHash() == outpoint.txHash_ &&
         utxo.getTxOutIndex() == outpoint.txOutIndex_) {
         revokeUtxo = utxo;
         break;
      }
   }

   if (!revokeUtxo.isInitialized()) {
      throw AuthLogicException("missing validation utxo to revoke user address with");
   }

   //we're sending the coins back to the relevant validation address
   auto& validationAddr = aav.findValidationAddressForUTXO(revokeUtxo);
   auto addrObj = bs::Address::fromHash(validationAddr);
   return { addrObj, revokeUtxo };
}

////
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

////
BinaryData AuthAddressLogic::revoke(const bs::Address &
   , const std::shared_ptr<ResolverFeed> &feedPtr
   , const bs::Address &, const UTXO &revokeUtxo)
{
   //User side revoke: burn the validation UTXO as an OP_RETURN
   ArmorySigner::Signer signer;
   signer.setFeed(feedPtr);
   signer.addSpender(std::make_shared<ArmorySigner::ScriptSpender>(revokeUtxo));

   //don't waste space, OP_RETURNs are useless to the chain
   const std::string opReturnMsg = "BSTrevoke";
   signer.addRecipient(std::make_shared<Recipient_OPRETURN>(BinaryData::fromString(opReturnMsg)));

   signer.sign();
   return signer.serializeSignedTx();
}

////////////////////////////////////////////////////////////////////////////////
bool AuthAddressLogic::AddrPathsStatus::isInitialized() const
{
   return pathCount_ != UINT32_MAX;
}

////
bool AuthAddressLogic::AddrPathsStatus::isValid() const
{
   //if there are no invalid or revoked paths
   if (invalidPaths_.empty() && revokedPaths_.empty()) {
      
      //and we have exactly 1 valid path
      if (validPaths_.size() != 1) {
         return false;
      }

      //and it is the first output on the address
      auto pathIter = validPaths_.find(0);
      if (pathIter != validPaths_.end()) {
         return true;
      }
   }

   return false;
}

////
const OutpointData& AuthAddressLogic::AddrPathsStatus::getValidationOutpoint() const
{
   if (!isValid())
      throw AuthLogicException("addr isn't valid");

   auto iter = validPaths_.find(0);
   if (iter == validPaths_.end())
      throw AuthLogicException("validation logic inconsistency");

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void ValidationAddressStruct::prettyPrint(void) const
{
   std::stringstream ss;
   if (firstOutpointHash_.empty()) {
      ss << " - missing first outpoint" << std::endl;
   } else {
      ss << " - first outpoint: " << firstOutpointHash_.toHexStr() << 
         ", id: " << firstOutpointIndex_ << std::endl;


      //order by height and txindex
      size_t count = 0;
      std::map<unsigned, std::map<unsigned, BinaryDataRef>> heightIdMap;

      for (const auto& hashPair : outpoints_) {
         if (hashPair.second.empty()) {
            continue;
         }

         //tally count
         count += hashPair.second.size();

         //populate height and index map
         const auto& firstOp = hashPair.second.begin()->second;
         auto height = firstOp->txHeight();
         auto txIndex = firstOp->txIndex();

         auto& idMap = heightIdMap[height];
         idMap.emplace(txIndex, hashPair.first.getRef());
      }

      ss << " - outpoints count: " << count << std::endl;
      for (const auto& heightPair : heightIdMap) {
         for (auto& idPair : heightPair.second) {
            
            auto hashIter = outpoints_.find(idPair.second);
            if (hashIter == outpoints_.end()) {
               throw std::runtime_error("missing outpoint hash");
            }

            ss << "  hash: " << hashIter->first.toHexStr() << std::endl;
            
            for (const auto& opPair : hashIter->second) {
               opPair.second->prettyPrint(ss);
            }
         }
      }
   }

   ss << std::endl;
   std::cout << ss.str();
}

////////////////////////////////////////////////////////////////////////////////
void AuthOutpoint::prettyPrint(std::ostream& st) const
{
   if (value_ == UINT64_MAX) {
      st << "   N/A" << std::endl;
      return;
   }

   st << "  . id: " << txOutIndex_ << ", height: " << txHeight_ << 
      ", txId: " << txIndex_ << std::endl;

   st << "    value: " << value_ << ", spender: ";

   if (isSpent_) {
      st << spenderHash_.toHexStr() << std::endl;
   } else {
      st << "N/A" << std::endl;
   }
}