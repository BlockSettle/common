/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TransactionData.h"

#include "ArmoryConnection.h"
#include "BTCNumericTypes.h"
#include "CoinSelection.h"
#include "SelectedTransactionInputs.h"
#include "ScriptRecipient.h"
#include "RecipientContainer.h"
#include "Wallets/SyncHDGroup.h"
#include "Wallets/SyncWallet.h"
#include "Wallets/SyncWalletsManager.h"

#include <vector>
#include <map>
#include <spdlog/spdlog.h>

using namespace ArmorySigner;

static const size_t kMaxTxStdWeight = 400000;


TransactionData::TransactionData(const onTransactionChanged &changedCallback
   , const std::shared_ptr<spdlog::logger> &logger , bool isSegWitInputsOnly, bool confOnly)
   : changedCallback_(changedCallback)
   , logger_(logger)
   , feePerByte_(0)
   , nextId_(0)
   , isSegWitInputsOnly_(isSegWitInputsOnly)
   , confirmedInputs_(confOnly)
{}

TransactionData::~TransactionData() noexcept
{
   changedCallback_ = {};
}

void TransactionData::SetCallback(onTransactionChanged changedCallback)
{
   changedCallback_ = std::move(changedCallback);
}

bool TransactionData::setWallet(const std::shared_ptr<bs::sync::Wallet> &wallet
   , uint32_t topBlock, bool resetInputs, const std::function<void()> &cbInputsReset)
{
   if (!wallet) {
      return false;
   }
   if (summary_.fixedInputs) {
      wallet_ = wallet;
      group_.reset();
      if (cbInputsReset) {
         cbInputsReset();
      }
      return true;
   }
   if (wallet != wallet_) {
      wallet_ = wallet;
      group_ = nullptr;

      selectedInputs_ = std::make_shared<SelectedTransactionInputs>(wallet_
         , isSegWitInputsOnly_, confirmedInputs_
         , [this]() {
         InvalidateTransactionData();
      }, cbInputsReset);

      coinSelection_ = std::make_shared<CoinSelection>([this](uint64_t) {
         return selectedInputs_->GetSelectedTransactions();
      }
         , std::vector<AddressBookEntry>{}
      , static_cast<uint64_t>(wallet->getSpendableBalance() * BTCNumericTypes::BalanceDivider)
         , topBlock);
      InvalidateTransactionData();
   }
   else if (resetInputs) {
      if (selectedInputs_) {
         selectedInputs_->ResetInputs(cbInputsReset);
      }
      else {
         selectedInputs_ = std::make_shared<SelectedTransactionInputs>(wallet_
            , isSegWitInputsOnly_, confirmedInputs_
            , [this] { InvalidateTransactionData(); }
         , cbInputsReset);
      }
      InvalidateTransactionData();
   }
   return true;
}

bool TransactionData::setUTXOs(const std::vector<std::string>& walletsId
   , uint32_t topBlock, const std::vector<UTXO>& utxos, bool resetInputs
   , const std::function<void()>& cbInputsReset)
{
   if (walletsId.empty()) {
      return false;
   }
   if (summary_.fixedInputs) {
      walletsId_ = walletsId;
      if (cbInputsReset) {
         cbInputsReset();
      }
      return true;
   }
   if (walletsId != walletsId_) {
      walletsId_ = walletsId;

      selectedInputs_ = std::make_shared<SelectedTransactionInputs>(utxos
         , [this]() {
         InvalidateTransactionData();
      });

      coinSelection_ = std::make_shared<CoinSelection>([this](uint64_t) {
         return selectedInputs_->GetSelectedTransactions();
      }
         , std::vector<AddressBookEntry>{}, UINT64_MAX, topBlock);
      InvalidateTransactionData();
   } else if (resetInputs) {
      if (selectedInputs_) {
         selectedInputs_->ResetInputs(cbInputsReset);
      } else {
         selectedInputs_ = std::make_shared<SelectedTransactionInputs>(wallet_
            , isSegWitInputsOnly_, confirmedInputs_
            , [this] { InvalidateTransactionData(); }
         , cbInputsReset);
      }
      InvalidateTransactionData();
   }
   return true;
}

bool TransactionData::setGroup(const std::shared_ptr<bs::sync::hd::Group> &group
   , uint32_t topBlock, bool excludeLegacy, bool resetInputs, const std::function<void()> &cbInputsReset)
{
   if (!group) {
      return false;
   }
   if (summary_.fixedInputs) {
      group_ = group;
      wallet_.reset();
      if (cbInputsReset) {
         cbInputsReset();
      }
      return true;
   }
   std::vector<std::shared_ptr<bs::sync::Wallet>> wallets;
   BTCNumericTypes::balance_type spendableBalance = 0;
   const auto leaves = group->getLeaves();
   for (const auto &leaf : leaves) {
      if (excludeLegacy && leaf->purpose() == bs::hd::Purpose::NonSegWit) {
         continue;
      }

      spendableBalance += leaf->getSpendableBalance();
      wallets.push_back(leaf);
   }

   if (group != group_) {
      wallet_ = nullptr;
      group_ = group;

      if (!leaves.empty()) {
         wallet_ = leaves.front();
      }

      selectedInputs_ = std::make_shared<SelectedTransactionInputs>(wallets
         , isSegWitInputsOnly_, confirmedInputs_
         , [this]() {
         InvalidateTransactionData();
      }, cbInputsReset);

      coinSelection_ = std::make_shared<CoinSelection>([this](uint64_t) {
         return selectedInputs_->GetSelectedTransactions();
      }
         , std::vector<AddressBookEntry>{}
         , static_cast<uint64_t>(spendableBalance * BTCNumericTypes::BalanceDivider)
         , topBlock);
      InvalidateTransactionData();
   } else if (resetInputs) {
      if (selectedInputs_) {
         selectedInputs_->ResetInputs(cbInputsReset);
      } else {
         selectedInputs_ = std::make_shared<SelectedTransactionInputs>(wallets
            , isSegWitInputsOnly_, confirmedInputs_
            , [this] { InvalidateTransactionData(); }
         , cbInputsReset);
      }
      InvalidateTransactionData();
   }
   return true;
}
bool TransactionData::setGroupAndInputs(const std::shared_ptr<bs::sync::hd::Group> &group
   , const std::vector<UTXO> &utxos, uint32_t topBlock)
{
   wallet_.reset();
   if (!group) {
      return false;
   }
   const auto leaves = group->getAllLeaves();
   if (leaves.empty()) {
      return false;
   }
   group_ = group;
   return setWalletAndInputs(leaves.front(), utxos, topBlock);
}

bool TransactionData::setWalletAndInputs(const std::shared_ptr<bs::sync::Wallet> &wallet
   , const std::vector<UTXO> &utxos, uint32_t topBlock)
{
   if (!wallet) {
      return false;
   }
   wallet_ = wallet;

   selectedInputs_ = std::make_shared<SelectedTransactionInputs>(
      utxos, [this] { InvalidateTransactionData(); });

   coinSelection_ = std::make_shared<CoinSelection>([this](uint64_t) {
      return selectedInputs_->GetSelectedTransactions();
   }
      , std::vector<AddressBookEntry>{}
   , static_cast<uint64_t>(wallet->getSpendableBalance() * BTCNumericTypes::BalanceDivider)
      , topBlock);
   InvalidateTransactionData();
   return true;
}

TransactionData::TransactionSummary TransactionData::GetTransactionSummary() const
{
   return summary_;
}

void TransactionData::InvalidateTransactionData()
{
   if (!summary_.fixedInputs) {
      usedUTXO_.clear();
      summary_ = TransactionSummary{};
   }
   maxAmount_.SetValue(0);

   UpdateTransactionData();

   if (changedCallback_) {
      changedCallback_();
   }
}

bool TransactionData::UpdateTransactionData()
{
   if (!selectedInputs_) {
      return false;
   }
   uint64_t availableBalance = 0;
   std::vector<UTXO> transactions = decorateUTXOs();
   if (!summary_.fixedInputs) {
      for (const auto &tx : transactions) {
         availableBalance += tx.getValue();
      }
      summary_.availableBalance = availableBalance / BTCNumericTypes::BalanceDivider;
      summary_.isAutoSelected = selectedInputs_->UseAutoSel();
   }

   bool maxAmount = true;
   std::map<unsigned, std::vector<std::shared_ptr<ScriptRecipient>>> recipientsMap;
   if (RecipientsReady()) {
      for (const auto& it : recipients_) {
         if (!it.second->IsReady()) {
            return false;
         }
         maxAmount &= it.second->IsMaxAmount();
         const auto &recip = it.second->GetScriptRecipient();
         if (!recip) {
            return false;
         }
         recipientsMap.emplace(it.first,
            std::vector<std::shared_ptr<ScriptRecipient>>({recip}));
      }
   }
   if (recipientsMap.empty()) {
      return false;
   }

   const auto totalFee = totalFee_ ? totalFee_ : minTotalFee_;
   PaymentStruct payment = (!totalFee_ && !qFuzzyIsNull(feePerByte_))
      ? PaymentStruct(recipientsMap, 0, feePerByte_, 0)
      : PaymentStruct(recipientsMap, totalFee, 0, 0);
   summary_.balanceToSpend = payment.spendVal() / BTCNumericTypes::BalanceDivider;

   if (summary_.fixedInputs) {
      if (!summary_.txVirtSize && !usedUTXO_.empty()) {
         transactions = usedUTXO_;
         bs::Address::decorateUTXOs(transactions);
         UtxoSelection selection(transactions);
         selection.computeSizeAndFee(payment);
         summary_.txVirtSize = getVirtSize(selection);
         if (summary_.txVirtSize > kMaxTxStdWeight) {
            if (logger_) {
               logger_->error("Bad virtual size value {} - set to 0", summary_.txVirtSize);
            }
            summary_.txVirtSize = 0;
         }
      }
      summary_.totalFee = totalFee_;
      if (summary_.txVirtSize) {
         summary_.feePerByte = totalFee_ / summary_.txVirtSize;
      }
      summary_.hasChange = summary_.availableBalance > (summary_.balanceToSpend + totalFee_ / BTCNumericTypes::BalanceDivider);
   }
   else if (payment.spendVal() <= availableBalance) {
      if (maxAmount) {
         const UtxoSelection selection = computeSizeAndFee(transactions, payment);
         summary_.txVirtSize = getVirtSize(selection);
         if (summary_.txVirtSize > kMaxTxStdWeight) {
            if (logger_) {
               logger_->error("Bad virtual size value {} - set to 0", summary_.txVirtSize);
            }
            summary_.txVirtSize = 0;
         }
         summary_.totalFee = availableBalance - payment.spendVal();
         summary_.feePerByte =
            std::round((float)summary_.totalFee / (float)summary_.txVirtSize);
         summary_.hasChange = false;
         summary_.selectedBalance = availableBalance / BTCNumericTypes::BalanceDivider;
      } else if (selectedInputs_->UseAutoSel()) {
         UtxoSelection selection;
         try {
            selection = coinSelection_->getUtxoSelectionForRecipients(payment
               , transactions);
         } catch (const std::runtime_error &err) {
            if (logger_) {
               logger_->error("UpdateTransactionData (auto-selection) - coinSelection exception: {}"
                  , err.what());
            }
            return false;
         } catch (...) {
            if (logger_) {
               logger_->error("UpdateTransactionData (auto-selection) - coinSelection exception");
            }
            return false;
         }

         usedUTXO_ = selection.utxoVec_;
         summary_.txVirtSize = getVirtSize(selection);
         summary_.totalFee = selection.fee_;
         summary_.feePerByte = selection.fee_byte_;
         summary_.hasChange = selection.hasChange_;
         summary_.selectedBalance = selection.value_ / BTCNumericTypes::BalanceDivider;
      } else {
         UtxoSelection selection = computeSizeAndFee(transactions, payment);
         summary_.txVirtSize = getVirtSize(selection);
         if (summary_.txVirtSize > kMaxTxStdWeight) {
            if (logger_) {
               logger_->error("Bad virtual size value {} - set to 0", summary_.txVirtSize);
            }
            summary_.txVirtSize = 0;
         }
         summary_.totalFee = selection.fee_;
         summary_.feePerByte = selection.fee_byte_;
         summary_.hasChange = selection.hasChange_;
         summary_.selectedBalance = selection.value_ / BTCNumericTypes::BalanceDivider;
      }
      summary_.usedTransactions = usedUTXO_.size();
   }

   if (minTotalFee_ && (summary_.totalFee < minTotalFee_)) {
      summary_.totalFee = minTotalFee_;
   }

   summary_.outputsCount = recipients_.size();
   summary_.initialized = true;

   return true;
}

// Calculate the maximum fee for a given recipient.
bs::XBTAmount TransactionData::CalculateMaxAmount(const bs::Address &recipient, bool force) const
{
   if (!coinSelection_) {
      if (logger_) {
         logger_->error("[TransactionData::CalculateMaxAmount] wallet is missing");
      }
      return bs::XBTAmount{ UINT64_MAX };
   }
   if ((maxAmount_.GetValue() != 0) && !force) {
      return maxAmount_;
   }

   maxAmount_.SetValue(0);

   if ((feePerByte_ == 0) && totalFee_) {
      const int64_t availableBalance = (GetTransactionSummary().availableBalance - \
         GetTransactionSummary().balanceToSpend) * BTCNumericTypes::BalanceDivider;
      const uint64_t totalFee = (totalFee_ < minTotalFee_) ? minTotalFee_ : totalFee_;
      if (availableBalance > totalFee) {
         maxAmount_.SetValue(availableBalance - totalFee);
      }
   }
   else {
      std::vector<UTXO> transactions = decorateUTXOs();

      if (transactions.size() == 0) {
         if (logger_) {
            logger_->debug("[TransactionData::CalculateMaxAmount] empty input list");
         }
         return {};
      }

      std::map<unsigned int, std::vector<std::shared_ptr<ScriptRecipient>>> recipientsMap;
      unsigned int recipId = 0;
      for (const auto &recip : recipients_) {
         const auto recipPtr = recip.second->GetScriptRecipient();
         if (!recipPtr || !recipPtr->getValue()) {
            continue;
         }
         recipientsMap[recipId++] = std::vector<std::shared_ptr<ScriptRecipient>>({recipPtr});
      }
      if (!recipient.empty()) {
         const auto recipPtr = recipient.getRecipient(bs::XBTAmount{ 0.001 });  // spontaneous output amount, shouldn't be 0
         if (recipPtr) {
            recipientsMap[recipId++] = std::vector<std::shared_ptr<ScriptRecipient>>({recipPtr});
         }
      }
      if (recipientsMap.empty()) {
         return {};
      }

      const PaymentStruct payment = (!totalFee_ && !qFuzzyIsNull(feePerByte_))
         ? PaymentStruct(recipientsMap, 0, feePerByte_, 0)
         : PaymentStruct(recipientsMap, totalFee_, feePerByte_, 0);

      // Accept the fee returned by Armory. The fee returned may be a few
      // satoshis higher than is strictly required by Core but that's okay.
      // If truly required, the fee can be tweaked later.
      try {
         uint64_t fee = coinSelection_->getFeeForMaxVal(payment.size(), feePerByte_
            , transactions);
         if (fee < minTotalFee_) {
            fee = minTotalFee_;
         }

         const int64_t availableBalance = (GetTransactionSummary().availableBalance - \
            GetTransactionSummary().balanceToSpend) * BTCNumericTypes::BalanceDivider;
         if (availableBalance >= fee) {
            maxAmount_.SetValue(availableBalance - fee);
         }
      } catch (const std::exception &e) {
         if (logger_) {
            logger_->error("[TransactionData::CalculateMaxAmount] failed to get fee for max val: {}", e.what());
         }
      }
   }
   return maxAmount_;
}

void TransactionData::setSelectedUtxo(const std::vector<UTXO>& utxos)
{
   UtxoHashes utxosHashes;
   utxosHashes.reserve(utxos.size());
   for (auto utxo : utxos) {
      utxosHashes.push_back({ utxo.getTxHash(), utxo.getTxOutIndex() });
   }
   setSelectedUtxo(utxosHashes);
}

void TransactionData::setSelectedUtxo(const UtxoHashes& utxosHashes)
{
   for (const auto &utxo : utxosHashes) {
      bool result = selectedInputs_->SetUTXOSelection(utxo.first, utxo.second);
      if (!result) {
         SPDLOG_LOGGER_WARN(logger_, "selecting input failed for predefined utxo set");
      }
      else {
         selectedInputs_->SetUseAutoSel(false);
      }
   }
   if (!selectedInputs_->UseAutoSel()) {
      InvalidateTransactionData();
   }
}

void TransactionData::setFixedInputs(const std::vector<UTXO> &utxos, size_t txVirtSize)
{
   usedUTXO_ = utxos;
   summary_.isAutoSelected = false;
   summary_.fixedInputs = true;
   summary_.usedTransactions = utxos.size();
   summary_.availableBalance = 0;
   summary_.txVirtSize = txVirtSize;

   for (const auto &utxo : utxos) {
      summary_.availableBalance += utxo.getValue() / BTCNumericTypes::BalanceDivider;
   }
   summary_.selectedBalance = summary_.availableBalance;
}

bool TransactionData::RecipientsReady() const
{
   if (recipients_.empty()) {
      return false;
   }

   for (const auto& it : recipients_) {
      if (!it.second->IsReady()) {
         return false;
      }
   }

   return true;
}

// A function equivalent to CoinSelectionInstance::decorateUTXOs() in Armory. We
// need it for proper initialization of the UTXO structs when computing TX sizes
// and fees.
// IN:  None
// OUT: None
// RET: A vector of fully initialized UTXO objects, one for each selected (and
//      non-filtered) input.
std::vector<UTXO> TransactionData::decorateUTXOs() const
{
   if (!selectedInputs_) {
      return {};
   }

   auto inputUTXOs = selectedInputs_->GetSelectedTransactions();

   bs::Address::decorateUTXOs(inputUTXOs);
   return inputUTXOs;
}

// Frontend for UtxoSelection::computeSizeAndFee(). Necessary due to some
// nuances in how it's invoked.
// IN:  UTXO vector used to initialize UtxoSelection. (std::vector<UTXO>)
// OUT: None
// RET: A fully initialized UtxoSelection object, with size and fee data.
UtxoSelection TransactionData::computeSizeAndFee(const std::vector<UTXO>& inUTXOs
   , const PaymentStruct& inPS) const
{
   // When creating UtxoSelection object, initialize it with a copy of the
   // UTXO vector. Armory will "move" the data behind-the-scenes, and we
   // still need the data.
   usedUTXO_ = inUTXOs;
   auto usedUTXOCopy{ usedUTXO_ };
   UtxoSelection selection{ usedUTXOCopy };

   try {
      selection.computeSizeAndFee(inPS);
   }
   catch (const std::runtime_error &err) {
      if (logger_) {
         logger_->error("UpdateTransactionData - UtxoSelection exception: {}"
            , err.what());
      }
   }
   catch (...) {
      if (logger_) {
         logger_->error("UpdateTransactionData - UtxoSelection exception");
      }
   }

   return selection;
}

// A temporary private function that calculates the virtual size of an incoming
// UtxoSelection object. This needs to be removed when a particular PR
// (https://github.com/goatpig/BitcoinArmory/pull/538) is accepted upstream.
// Note that this function assumes SegWit will be used. It's fine for our
// purposes but it's a bad assumption in general.
size_t TransactionData::getVirtSize(const UtxoSelection& inUTXOSel) const
{
   size_t nonWitSize = inUTXOSel.size_ - inUTXOSel.witnessSize_;
   return std::ceil(static_cast<float>(3 * nonWitSize + inUTXOSel.size_) / 4.0f);
}

void TransactionData::setFeePerByte(float feePerByte)
{
   // Our fees estimation is not 100% accurate (we can't know how much witness size will have,
   // because we don't know signature(s) size in advance, it could be 73, 72, and 71).
   // As the result we might hit "min fee relay not meet" error (when actual fees is lower then 1 sat/bytes).
   // Let's add a workaround for this: don't allow feePerByte be less than 1.01f (that's just empirical estimate)
   const float minRelayFeeFixed = 1.01f;

   const auto prevFee = feePerByte_;
   if (feePerByte >= 1.0f && feePerByte < minRelayFeeFixed) {
      feePerByte_ = minRelayFeeFixed;
   } else {
      feePerByte_ = feePerByte;
   }
   totalFee_ = 0;
   if (!qFuzzyCompare(prevFee, feePerByte_)) {
      InvalidateTransactionData();
   }
}

void TransactionData::setTotalFee(uint64_t fee, bool overrideFeePerByte)
{
   if (overrideFeePerByte) {
      feePerByte_ = 0;
   }
   if (totalFee_ != fee) {
      totalFee_ = fee;
      InvalidateTransactionData();
   }
}

float TransactionData::feePerByte() const
{
   if (!qFuzzyIsNull(feePerByte_) && (feePerByte_ > 0)) {
      return feePerByte_;
   }

   if (summary_.initialized) {
	   if (summary_.txVirtSize) {
		   return totalFee_ / summary_.txVirtSize;
	   }
   }

   return 0;
}

uint64_t TransactionData::totalFee() const
{
   if (totalFee_) {
      return totalFee_;
   }
   if (summary_.totalFee) {
      return summary_.totalFee;
   }
   if (summary_.txVirtSize) {
      return (uint64_t)(feePerByte_ * summary_.txVirtSize);
   }
   return 0;
}

void TransactionData::clear()
{
   totalFee_ = 0;
   feePerByte_ = 0;
   recipients_.clear();
   usedUTXO_.clear();
   summary_ = {};
}

std::vector<UTXO> TransactionData::inputs() const
{
   return usedUTXO_;
}

bool TransactionData::IsTransactionValid() const
{
   return (((wallet_ || !walletsId_.empty()) && selectedInputs_) || summary_.fixedInputs)
      && summary_.usedTransactions != 0
      && (!qFuzzyIsNull(feePerByte_) || totalFee_ != 0 || summary_.totalFee != 0)
      && RecipientsReady();
}

size_t TransactionData::GetRecipientsCount() const
{
   return recipients_.size();
}

unsigned int TransactionData::RegisterNewRecipient()
{
   unsigned int id = nextId_;
   ++nextId_;

   auto newRecipient = std::make_shared<RecipientContainer>();
   recipients_.emplace(id, newRecipient);

   return id;
}

std::vector<unsigned int> TransactionData::allRecipientIds() const
{
   std::vector<unsigned int> result;
   result.reserve(recipients_.size());
   for (const auto &recip : recipients_) {
      result.push_back(recip.first);
   }
   return result;
}

void TransactionData::RemoveRecipient(unsigned int recipientId)
{
   recipients_.erase(recipientId);
   InvalidateTransactionData();
}

void TransactionData::ClearAllRecipients()
{
   if (!recipients_.empty()) {
      recipients_.clear();
      InvalidateTransactionData();
   }
}

bool TransactionData::UpdateRecipientAddress(unsigned int recipientId, const bs::Address &address)
{
   auto it = recipients_.find(recipientId);
   if (it == recipients_.end()) {
      return false;
   }

   bool result = it->second->SetAddress(address);
   if (result) {
      InvalidateTransactionData();
   }

   return result;
}

void TransactionData::ResetRecipientAddress(unsigned int recipientId)
{
   auto it = recipients_.find(recipientId);
   if (it != recipients_.end()) {
      it->second->ResetAddress();
   }
}

bool TransactionData::UpdateRecipient(unsigned int recipientId
   , const bs::XBTAmount &amount, const bs::Address &address)
{
   auto it = recipients_.find(recipientId);
   if (it == recipients_.end()) {
      return false;
   }

   const bool result = it->second->SetAddress(address) & it->second->SetAmount(amount);
   if (result) {
      InvalidateTransactionData();
   }
   return result;
}

bool TransactionData::UpdateRecipientAmount(unsigned int recipientId
   , const bs::XBTAmount &amount, bool isMax)
{
   auto it = recipients_.find(recipientId);
   if (it == recipients_.end()) {
      return false;
   }

   bool result = it->second->SetAmount(amount, isMax);
   if (result) {
      InvalidateTransactionData();
   }

   return result;
}

std::shared_ptr<ScriptRecipient> TransactionData::GetScriptRecipient(unsigned int recipientId) const
{
   const auto &itRecip = recipients_.find(recipientId);
   if (itRecip == recipients_.end()) {
      return nullptr;
   }

   return itRecip->second->GetScriptRecipient();
}

bs::Address TransactionData::GetRecipientAddress(unsigned int recipientId) const
{
   const auto &itRecip = recipients_.find(recipientId);
   if (itRecip == recipients_.end()) {
      return bs::Address();
   }
   return itRecip->second->GetAddress();
}

bs::XBTAmount TransactionData::GetRecipientAmount(unsigned int recipientId) const
{
   const auto &itRecip = recipients_.find(recipientId);
   if (itRecip == recipients_.end()) {
      return {};
   }
   return itRecip->second->GetAmount();
}

bs::XBTAmount TransactionData::GetTotalRecipientsAmount() const
{
   bs::XBTAmount result;
   for (const auto &recip : recipients_) {
      result.SetValue(result.GetValue() + recip.second->GetAmount().GetValue());
   }
   return result;
}

bool TransactionData::IsMaxAmount(unsigned int recipientId) const
{
   const auto &itRecip = recipients_.find(recipientId);
   if (itRecip == recipients_.end()) {
      return false;
   }
   return itRecip->second->IsMaxAmount();
}

std::vector<std::shared_ptr<ScriptRecipient>> TransactionData::GetRecipientList() const
{
   if (!IsTransactionValid()) {
      throw std::logic_error("transaction is invalid");
   }
   if (inputs().empty()) {
      throw std::logic_error("missing inputs");
   }

   std::vector<std::shared_ptr<ScriptRecipient>> recipientList;
   for (const auto& it : recipients_) {
      if (!it.second->IsReady()) {
         throw std::logic_error("recipient[s] not ready");
      }
      recipientList.emplace_back(it.second->GetScriptRecipient());
   }

   return recipientList;
}

bs::core::wallet::TXSignRequest TransactionData::createTXRequest(bool isRBF
   , const bs::Address &changeAddr) const
{
   std::vector<std::shared_ptr<bs::sync::Wallet>> wallets;
   if (group_) {
      for (const auto &wallet : group_->getLeaves()) {
         wallets.push_back(wallet);
      }
   } else if (wallet_) {
      wallets.push_back(wallet_);
   }

   bs::core::wallet::TXSignRequest txReq;
   const auto fee = summary_.totalFee ? summary_.totalFee : totalFee();

   if (wallets.empty() && !walletsId_.empty()) {   // new code
      txReq = bs::sync::wallet::createTXRequest(walletsId_, inputs(), GetRecipientList()
         , true, changeAddr, {}, fee, isRBF);
   }
   else {
      if (!changeAddr.empty()) {
         bool changeAddrFound = false;
         for (const auto& wallet : wallets) {
            if (!wallet->getAddressIndex(changeAddr).empty()) {
               wallet->setAddressComment(changeAddr, bs::sync::wallet::Comment::toString(bs::sync::wallet::Comment::ChangeAddress));
               changeAddrFound = true;
               break;
            }
         }
         if (!changeAddrFound) { // shouldn't return if change address is not from our wallet
            if (logger_) {
               SPDLOG_LOGGER_ERROR(logger_, "can't find change address index");
            }
         }
      }

      txReq = bs::sync::wallet::createTXRequest(wallets, inputs(), GetRecipientList()
         , true, changeAddr, fee, isRBF);
      if (group_) {
         txReq.walletIds.clear();
         std::set<std::string> walletIds;
         const auto& leaves = group_->getAllLeaves();
         for (const auto& input : inputs()) {
            std::string inputLeafId;
            for (const auto& leaf : leaves) {
               if (leaf->containsAddress(bs::Address::fromUTXO(input))) {
                  inputLeafId = leaf->walletId();
                  break;
               }
            }
            if (inputLeafId.empty()) {
               throw std::runtime_error("orphaned input " + input.getTxHash().toHexStr(true)
                  + " without wallet");
            }
            walletIds.insert(inputLeafId);
         }
         txReq.walletIds.insert(txReq.walletIds.end(), walletIds.cbegin(), walletIds.cend());
      }
   }
   return txReq;
}

bs::core::wallet::TXSignRequest TransactionData::createUnsignedTransaction(bool isRBF, const bs::Address &changeAddress)
{
   if (!wallet_) {
      return {};
   }
   auto unsignedTxReq = wallet_->createTXRequest(inputs(), GetRecipientList(), true, summary_.totalFee, isRBF, changeAddress);
   if (!unsignedTxReq.isValid()) {
      throw std::runtime_error("missing unsigned TX");
   }

   return unsignedTxReq;
}
