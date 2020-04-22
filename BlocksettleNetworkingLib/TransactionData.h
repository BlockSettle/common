/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __TRANSACTION_DATA_H__
#define __TRANSACTION_DATA_H__

#include "Address.h"
#include "CoreWallet.h"
#include "UtxoReservation.h"
#include "ValidityFlag.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      namespace hd {
         class Group;
      }
      class Wallet;
   }
}
class CoinSelection;
class RecipientContainer;
class ScriptRecipient;
class SelectedTransactionInputs;

struct PaymentStruct;
struct UtxoSelection;


class TransactionData
{
public:
   struct TransactionSummary
   {
      bool     initialized{};
      // usedTransactions - count of utxo that will be used in transaction
      size_t   usedTransactions{};

      // outputsCount - number of recipients ( without change )
      size_t   outputsCount{};

      // availableBalance - total available balance. in case of manual selection - same as selectedBalance
      double   availableBalance{};

      // selectedBalance - balance of selected inputs
      double   selectedBalance{};

      // balanceToSpent - total amount received by recipients
      double   balanceToSpend{};

      size_t   txVirtSize{};
      uint64_t totalFee{};
      double   feePerByte{};

      bool     hasChange{};
      bool     isAutoSelected{};
   };

   using onTransactionChanged = std::function<void()>;

public:
   TransactionData(const onTransactionChanged &changedCallback = nullptr
      , const std::shared_ptr<spdlog::logger> &logger = nullptr
      , bool isSegWitInputsOnly = false, bool confirmedOnly = false);
   ~TransactionData() noexcept;

   TransactionData(const TransactionData&) = delete;
   TransactionData& operator = (const TransactionData&) = delete;
   TransactionData(TransactionData&&) = delete;
   TransactionData& operator = (TransactionData&&) = delete;

   // should be used only if you could not set CB in ctor
   void SetCallback(onTransactionChanged changedCallback);

   bool setWallet(const std::shared_ptr<bs::sync::Wallet> &, uint32_t topBlock
      , bool resetInputs = false, const std::function<void()> &cbInputsReset = nullptr);
   bool setGroup(const std::shared_ptr<bs::sync::hd::Group> &, uint32_t topBlock, bool excludeLegacy
      , bool resetInputs = false, const std::function<void()> &cbInputsReset = nullptr);
   bool setWalletAndInputs(const std::shared_ptr<bs::sync::Wallet> &
      , const std::vector<UTXO> &, uint32_t topBlock);
   bool setGroupAndInputs(const std::shared_ptr<bs::sync::hd::Group> &
      , const std::vector<UTXO> &, uint32_t topBlock);
   std::shared_ptr<bs::sync::Wallet> getWallet() const { return wallet_; }
   std::shared_ptr<bs::sync::hd::Group> getGroup() const { return group_; }
   void setFeePerByte(float feePerByte);
   void setTotalFee(uint64_t fee, bool overrideFeePerByte = true);
   void setMinTotalFee(uint64_t fee) { minTotalFee_ = fee; }
   float feePerByte() const;
   uint64_t totalFee() const;

   bool IsTransactionValid() const;

   size_t GetRecipientsCount() const;

   unsigned int RegisterNewRecipient();
   std::vector<unsigned int> allRecipientIds() const;
   bool UpdateRecipientAddress(unsigned int recipientId, const bs::Address &);
   void ResetRecipientAddress(unsigned int recipientId);
   bool UpdateRecipientAmount(unsigned int recipientId, double amount, bool isMax = false);
   bool UpdateRecipient(unsigned int recipientId, double amount, const bs::Address &);
   void RemoveRecipient(unsigned int recipientId);

   void ClearAllRecipients();

   bs::Address GetRecipientAddress(unsigned int recipientId) const;
   std::shared_ptr<ScriptRecipient> GetScriptRecipient(unsigned int recipientId) const;
   BTCNumericTypes::balance_type GetRecipientAmount(unsigned int recipientId) const;
   BTCNumericTypes::balance_type  GetTotalRecipientsAmount() const;
   bool IsMaxAmount(unsigned int recipientId) const;

   // If there is change then changeAddr must be set
   bs::core::wallet::TXSignRequest createUnsignedTransaction(bool isRBF = false, const bs::Address &changeAddr = {});

   // If there is change then changeAddr must be set
   bs::core::wallet::TXSignRequest createTXRequest(bool isRBF = false
      , const bs::Address &changeAddr = {}) const;

   std::shared_ptr<SelectedTransactionInputs> getSelectedInputs() { return selectedInputs_; }
   TransactionSummary GetTransactionSummary() const;

   double CalculateMaxAmount(const bs::Address &recipient = {}, bool force = false) const;

   void clear();
   std::vector<UTXO> inputs() const;

private:
   void InvalidateTransactionData();
   bool UpdateTransactionData();
   bool RecipientsReady() const;
   std::vector<UTXO> decorateUTXOs() const;
   UtxoSelection computeSizeAndFee(const std::vector<UTXO>& inUTXOs
      , const PaymentStruct& inPS) const;

   // Temporary function until some Armory changes are accepted upstream.
   size_t getVirtSize(const UtxoSelection& inUTXOSel) const;

   std::vector<std::shared_ptr<ScriptRecipient>> GetRecipientList() const;

private:
   onTransactionChanged             changedCallback_;
   std::shared_ptr<spdlog::logger>  logger_;

   std::shared_ptr<bs::sync::Wallet>            wallet_;
   std::shared_ptr<bs::sync::hd::Group>         group_;
   std::shared_ptr<SelectedTransactionInputs>   selectedInputs_;

   float       feePerByte_;
   uint64_t    totalFee_ = 0;
   uint64_t    minTotalFee_ = 0;
   mutable double maxAmount_ = 0;
   // recipients
   unsigned int nextId_;
   std::unordered_map<unsigned int, std::shared_ptr<RecipientContainer>> recipients_;
   std::shared_ptr<CoinSelection>   coinSelection_;

   mutable std::vector<UTXO>  usedUTXO_;
   TransactionSummary   summary_;

   const bool  isSegWitInputsOnly_;
   const bool  confirmedInputs_;
};

#endif // __TRANSACTION_DATA_H__
