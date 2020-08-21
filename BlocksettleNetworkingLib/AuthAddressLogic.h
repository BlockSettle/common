/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef _H_AUTHADDRESSLOGIC
#define _H_AUTHADDRESSLOGIC

#include <atomic>
#include <memory>
#include <set>

#include "Address.h"
#include "AuthAddress.h"
#include "Wallets/SyncWallet.h"


constexpr unsigned int VALIDATION_CONF_COUNT = 6;


class AuthLogicException : public std::runtime_error
{
public:
   AuthLogicException(const std::string& err) :
      std::runtime_error(err)
   {}
};

class ValidationAddressManager;

struct AuthOutpoint
{
private:
   unsigned txOutIndex_ = UINT32_MAX;
   uint64_t value_ = UINT64_MAX;

   //why is it so hard to udpate values in a std::set...
   mutable unsigned txHeight_ = UINT32_MAX;
   mutable unsigned txIndex_ = UINT32_MAX;
   mutable bool isSpent_ = true;
   mutable BinaryData spenderHash_;

public:
   bool operator<(const std::shared_ptr<AuthOutpoint>& rhs) const
   {  /*
      Doesnt work for comparing zc with zc, works with. Works
      with mined vs zc. Only used to setup the first outpoint
      atm.

      ZC outpoints are not eligible for that distinction, so
      there is no need to cover this blind spot (which would
      require more data from the db).
      */

      if (rhs == nullptr) {
         return true;
      }
      if (txHeight_ != rhs->txHeight_) {
         return txHeight_ < rhs->txHeight_;
      }
      if (txIndex_ != rhs->txIndex_) {
         return txIndex_ < rhs->txIndex_;
      }
      return txOutIndex_ < rhs->txOutIndex_;
   }

   AuthOutpoint(
      unsigned txHeight, unsigned txIndex, unsigned txOutIndex,
      uint64_t value, bool isSpent,
      const BinaryData& spenderHash) :
      txHeight_(txHeight), txIndex_(txIndex), txOutIndex_(txOutIndex),
      value_(value), isSpent_(isSpent),
      spenderHash_(spenderHash)
   {}

   AuthOutpoint(void) {}

   ////
   bool isSpent(void) const { return isSpent_; }

   bool isValid(void) const
   {
      return txOutIndex_ != UINT32_MAX;
   }

   bool isZc(void) const
   {
      if (!isValid()) {
         throw std::runtime_error("invalid AuthOutpoint");
      }
      return txHeight_ == UINT32_MAX;
   }

   ////
   unsigned txOutIndex(void) const { return txOutIndex_; }
   unsigned txIndex(void) const { return txIndex_; }
   unsigned txHeight(void) const { return txHeight_; }

   const BinaryData& spenderHash(void) const
   {
      return spenderHash_;
   }

   ////
   void updateFrom(const AuthOutpoint& rhs)
   {  /*
      You only ever update from previous states, so only do
      so to save a spentness marker
      */

      if (!rhs.isSpent_ || isSpent_) {
         return;
      }
      isSpent_ = rhs.isSpent_;
      txHeight_ = rhs.txHeight_;
      spenderHash_ = rhs.spenderHash_;
      txIndex_ = rhs.txIndex_;
   }

   ////
   void prettyPrint(std::ostream&) const;
};

struct AuthValidatorCallbacks;
////
class ValidationAddressACT : public ArmoryCallbackTarget
{
public:
   ValidationAddressACT(ArmoryConnection *armory)
      : ArmoryCallbackTarget()
   {
      init(armory);
   }
   ~ValidationAddressACT() override
   {
      cleanup();
   }

   ////
   void onRefresh(const std::vector<BinaryData> &, bool) override;
   void onZCReceived(const std::string& requestId, const std::vector<bs::TXEntry>& zcs) override;
   void onNewBlock(unsigned int height, unsigned int branchHeight) override;

   ////
   virtual void start();
   virtual void stop();
   virtual void setCallbacks(const std::shared_ptr<AuthValidatorCallbacks> &cbs) { callbacks_ = cbs; }

private:
   void processNotification(void);

private:
   ArmoryThreading::BlockingQueue<std::shared_ptr<DBNotificationStruct>> notifQueue_;
   std::thread processThr_;

protected:
   std::weak_ptr<AuthValidatorCallbacks>  callbacks_;
};

////
struct ValidationAddressStruct
{
   /*
   Validation and master addresses are 2 names for the same thing. 
   Master/Validation addresses are used to validate user addreses.
   */

   //<tx hash, <txoutid, outpoint>>
   std::map<BinaryData,
      std::map<unsigned, std::shared_ptr<AuthOutpoint>>> outpoints_;

   BinaryData firstOutpointHash_;
   unsigned firstOutpointIndex_ = UINT32_MAX;

   //<tx hash>
   std::set<BinaryDataRef> spenderHashes_;

   ValidationAddressStruct(void)
   {}

   std::shared_ptr<AuthOutpoint> getFirsOutpoint(void) const
   {
      auto opIter = outpoints_.find(firstOutpointHash_);
      if (opIter == outpoints_.end()) {
         return nullptr;
      }
      auto ptrIter = opIter->second.find(firstOutpointIndex_);
      if (ptrIter == opIter->second.end()) {
         return nullptr;
      }
      return ptrIter->second;
   }

   bool isFirstOutpoint(const BinaryData& hash, unsigned index) const
   {
      if (firstOutpointIndex_ == UINT32_MAX ||
         firstOutpointHash_.getSize() != 32) {
         throw std::runtime_error("uninitialized first outpoint");
      }
      return index == firstOutpointIndex_ &&
         hash == firstOutpointHash_;
   }
   
   void prettyPrint(void) const;
};


class AuthAddressValidator;
struct AuthValidatorCallbacks
{
   virtual void shutdown() {}
   virtual bool isInited() const { return true; }
   virtual void setTarget(AuthAddressValidator *);

   virtual unsigned int topBlock() const = 0;
   virtual void pushZC(const BinaryData &) = 0;
   virtual std::string registerAddresses(const std::vector<bs::Address> &) = 0;

   using OutpointsCb = std::function<void(OutpointBatch)>;
   virtual void getOutpointsForAddresses(const std::vector<bs::Address> &
      , const OutpointsCb &, unsigned int topBlock = 0, unsigned int zcIndex = 0) = 0;

   using UTXOsCb = std::function<void(const std::vector<UTXO> &)>;
   virtual void getSpendableTxOuts(const UTXOsCb &) = 0;
   virtual void getUTXOsForAddress(const bs::Address &, const UTXOsCb &
      , bool withZC = false) = 0;

   using OnUpdate = std::function<void()>;
   OnUpdate onUpdate{ nullptr };

   using OnRefresh = std::function<void(const std::vector<BinaryData> &)>;
   OnRefresh onRefresh{ nullptr };
};

class AuthAddressValidator
{  /***
   This class tracks the state of validation addresses, which is
   required to check on the state of a user auth address.

   It uses a blocking model for the purpose of demonstrating the
   features in unit tests.
   ***/

public:
   AuthAddressValidator(const std::shared_ptr<AuthValidatorCallbacks> &callbacks)
      : lambdas_(callbacks)
   {}
   virtual ~AuthAddressValidator(void);

   void addValidationAddress(const bs::Address &);

   /*
   These methods return the amount of outpoints received. It
   allows for coverage of the db data flow.
   */
   using ResultCb = std::function<void(bool)>;
   bool goOnline(const ResultCb &);

   unsigned update(void);
   unsigned update(const OutpointBatch &);

   bool isReady() const { return ready_; }

   //utility methods
   void pushRefreshID(const std::vector<BinaryData> &);

   //validation address logic
   bool isValidMasterAddress(const bs::Address&) const;

   bool hasSpendableOutputs(const bs::Address&) const;
   bool hasZCOutputs(const bs::Address&) const;

   const bs::Address &findValidationAddressForUTXO(const UTXO&) const;
   const bs::Address &findValidationAddressForTxHash(const BinaryData&) const;

   //tx generating methods
   BinaryData fundUserAddress(const bs::Address&, std::shared_ptr<ArmorySigner::ResolverFeed>,
      const bs::Address& validationAddr = bs::Address()) const;
   BinaryData fundUserAddress(const bs::Address&, std::shared_ptr<ArmorySigner::ResolverFeed>,
      const UTXO &) const;
   BinaryData fundUserAddresses(const std::vector<bs::Address> &, const bs::Address &validationAddress
      , std::shared_ptr<ArmorySigner::ResolverFeed>, const std::vector<UTXO> &, int64_t totalFee) const;
   BinaryData vetUserAddress(const bs::Address&, std::shared_ptr<ArmorySigner::ResolverFeed>,
      const bs::Address& validationAddr = bs::Address()) const;
   BinaryData revokeValidationAddress(
      const bs::Address&, std::shared_ptr<ArmorySigner::ResolverFeed>) const;
   BinaryData revokeUserAddress(
      const bs::Address&, std::shared_ptr<ArmorySigner::ResolverFeed>);

   std::vector<UTXO> filterVettingUtxos(const bs::Address &validationAddr
      , const std::vector<UTXO> &) const;

   unsigned int topBlock() const;

   OutpointBatch getOutpointsFor(const bs::Address &) const;
   void getOutpointsFor(const bs::Address &
      , const std::function<void(const OutpointBatch &)> &) const;
   std::vector<UTXO> getUTXOsFor(const bs::Address &, bool withZC = false) const;
   void pushZC(const BinaryData &tx) const;
   void getValidationOutpointsBatch(const std::function<void(OutpointBatch)> &);

protected:
   virtual void prepareCallbacks() {}

protected:
   std::shared_ptr<AuthValidatorCallbacks>   lambdas_;

private:
   std::shared_ptr<ValidationAddressStruct>
      getValidationAddress(const bs::Address &) const;

   UTXO getVettingUtxo(const bs::Address &validationAddr
      , const std::vector<UTXO> &, size_t nbOutputs = 1) const;

   void waitOnRefresh(const std::string&);

   OutpointBatch getOutpointsForAddresses(const std::vector<bs::Address> &
      , unsigned int topBlock = 0, unsigned int zcIndex = 0) const;
   std::vector<UTXO> getSpendableTxOuts() const;
   std::vector<UTXO> getUTXOsForAddress(const bs::Address &
      , bool withZC = false) const;

private:
   ArmoryThreading::TimedQueue<BinaryData> refreshQueue_;

   std::map<bs::Address, std::shared_ptr<ValidationAddressStruct>>   validationAddresses_;
   unsigned topBlock_ = 0;
   unsigned zcIndex_ = 0;

   std::atomic_bool  ready_{ false };
   std::atomic_bool  stopped_{ false };
   mutable std::mutex vettingMutex_;
   std::mutex  updateMutex_;
   std::thread updateThread_;
   std::atomic_bool  updateThreadRunning_{ false };
};

////
class ValidationAddressManager : public AuthAddressValidator
{  /***
   This class tracks the state of validation addresses, which is
   required to check on the state of a user auth address.

   It uses a blocking model for the purpose of demonstrating the
   features in unit tests.
   ***/

public:
   ValidationAddressManager(const std::shared_ptr<ArmoryConnection> &);
   virtual ~ValidationAddressManager();

   void setCustomACT(const std::shared_ptr<ValidationAddressACT> &);

protected:
   void prepareCallbacks() override;

private:
   std::shared_ptr<ValidationAddressACT> actPtr_;
};

////////////////////////////////////////////////////////////////////////////////
namespace AuthAddressLogic
{
   struct AddrPathsStatus
   {
      /*
      Paths are signified by the order of outputs on the address
      */
      unsigned pathCount_ = UINT32_MAX;
      std::map<unsigned, OutpointData> validPaths_;
      std::vector<unsigned> invalidPaths_;
      std::vector<unsigned> revokedPaths_;

      bool isInitialized(void) const;
      bool isValid(void) const;
      const OutpointData& getValidationOutpoint(void) const;
   };

   AddressVerificationState getAuthAddrState(const AuthAddressValidator &
      , const bs::Address &);
   AddressVerificationState getAuthAddrState(const AuthAddressValidator &
      , const OutpointBatch &);

   bool isValid(const AuthAddressValidator &, const bs::Address &);

   AddrPathsStatus getAddrPathsStatus(const AuthAddressValidator &
      , const bs::Address &);
   AddrPathsStatus getAddrPathsStatus(const AuthAddressValidator &
      , const OutpointBatch &);
   BinaryData revoke(const AuthAddressValidator &, const bs::Address &
      , const std::shared_ptr<ArmorySigner::ResolverFeed> &);
   std::pair<bs::Address, UTXO> getRevokeData(const AuthAddressValidator &
      , const bs::Address &authAddr);
   BinaryData revoke(const bs::Address & 
      , const std::shared_ptr<ArmorySigner::ResolverFeed> &
      , const bs::Address &validationAddr, const UTXO &);
};

#endif
