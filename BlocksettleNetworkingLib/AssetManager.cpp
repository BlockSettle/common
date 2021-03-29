/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include <algorithm>
#include <QMutexLocker>
#include <spdlog/spdlog.h>
#include "AssetManager.h"
#include "Celer/CelerClient.h"
#include "Celer/FindSubledgersForAccountSequence.h"
#include "Celer/GetAssignedAccountsListSequence.h"
#include "CommonTypes.h"
#include "CurrencyPair.h"
#include "MDCallbacksQt.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"

#include "bs_proxy_terminal_pb.pb.h"
#include "com/celertech/piggybank/api/subledger/DownstreamSubLedgerProto.pb.h"

using namespace Blocksettle::Communication;


AssetManager::AssetManager(const std::shared_ptr<spdlog::logger>& logger
      , const std::shared_ptr<bs::sync::WalletsManager>& walletsManager
      , const std::shared_ptr<MDCallbacksQt> &mdCallbacks
      , const std::shared_ptr<CelerClientQt>& celerClient)
   : logger_(logger)
   , walletsManager_(walletsManager)
   , mdCallbacks_(mdCallbacks)
   , celerClient_(celerClient)
   , act_(this)
{}

AssetManager::AssetManager(const std::shared_ptr<spdlog::logger>& logger
   , AssetCallbackTarget *act)
   : logger_(logger), act_(act)
{}

AssetManager::~AssetManager() = default;

void AssetManager::init()
{
   connect(mdCallbacks_.get(), &MDCallbacksQt::MDSecurityReceived, this, &AssetManager::onMDSecurityReceived);
   connect(mdCallbacks_.get(), &MDCallbacksQt::MDSecuritiesReceived, this, &AssetManager::onMDSecuritiesReceived);

   connect(walletsManager_.get(), &bs::sync::WalletsManager::walletChanged, this, &AssetManager::onWalletChanged);
   connect(walletsManager_.get(), &bs::sync::WalletsManager::walletsReady, this, &AssetManager::onWalletChanged);
   connect(walletsManager_.get(), &bs::sync::WalletsManager::blockchainEvent, this, &AssetManager::onWalletChanged);

   connect(celerClient_.get(), &CelerClientQt::OnConnectedToServer, this, &AssetManager::onCelerConnected);
   connect(celerClient_.get(), &CelerClientQt::OnConnectionClosed, this, &AssetManager::onCelerDisconnected);
}

double AssetManager::getBalance(const std::string& currency, bool includeZc, const std::shared_ptr<bs::sync::Wallet> &wallet) const
{
   if (walletsManager_ && (currency == bs::network::XbtCurrency)) {
      if (wallet == nullptr) {
         if (includeZc) {
            return walletsManager_->getSpendableBalance() + walletsManager_->getUnconfirmedBalance();
         }
         return walletsManager_->getSpendableBalance();
      }
      if (includeZc) {
         return wallet->getSpendableBalance() + wallet->getUnconfirmedBalance();
      }
      return wallet->getSpendableBalance();
   }

   const auto itCC = ccSecurities_.find(currency);
   if (itCC != ccSecurities_.end()) {
      const auto &priWallet = walletsManager_->getPrimaryWallet();
      if (priWallet) {
         const auto &group = priWallet->getGroup(bs::hd::BlockSettle_CC);
         if (group) {
            const bs::hd::Path ccLeafPath({ bs::hd::Purpose::Native, bs::hd::CoinType::BlockSettle_CC
               , bs::hd::Path::keyToElem(currency) });
            const auto &wallet = group->getLeaf(ccLeafPath);
            if (wallet) {
               return wallet->getTotalBalance();
            }
         }
      }
      return 1.0 / itCC->second.nbSatoshis;
   }

   auto it = balances_.find(currency);
   if (it != balances_.end()) {
      return it->second;
   }

   return 0.0;
}

double AssetManager::getPrice(const std::string& currency) const
{
   auto it = prices_.find(currency);
   if (it != prices_.end()) {
      return it->second;
   }

   return 0.0;
}

bool AssetManager::checkBalance(const std::string &currency, double amount, bool includeZc) const
{
   if (currency.empty()) {
      return false;
   }
   const auto balance = getBalance(currency, includeZc, nullptr);
   return ((amount <= balance) || qFuzzyCompare(amount, balance));
}


std::vector<std::string> AssetManager::currencies()
{
   if (balances_.size() != currencies_.size()) {
      QMutexLocker lock(&mtxCurrencies_);
      currencies_.clear();

      for (const auto balance : balances_) {
         currencies_.push_back(balance.first);
      }

      std::sort(currencies_.begin(), currencies_.end());
   }

   return currencies_;
}

std::vector<std::string> AssetManager::privateShares(bool forceExternal)
{
   std::vector<std::string> result;

   const auto &priWallet = walletsManager_->getPrimaryWallet();
   if (!forceExternal && priWallet) {
      const auto &group = priWallet->getGroup(bs::hd::BlockSettle_CC);
      if (group) {
         const auto &leaves = group->getAllLeaves();
         for (const auto &leaf : leaves) {
            if (leaf->getSpendableBalance() > 0) {
               result.push_back(leaf->shortName());
            }
         }
      }
   }
   else {
      for (const auto &cc : ccSecurities_) {
         result.push_back(cc.first);
      }
   }
   return result;
}

std::vector<QString> AssetManager::securities(bs::network::Asset::Type assetType) const
{
   std::vector<QString> rv;
   for (auto security : securities_) {
      if ((security.second.assetType == assetType) || (assetType == bs::network::Asset::Undefined)) {
         rv.push_back(QString::fromStdString(security.first));
      }
   }
   return rv;
}

bool AssetManager::securityDef(const std::string &security, bs::network::SecurityDef &sd) const
{
   auto itSec = securities_.find(security);
   if (itSec == securities_.end()) {
      const CurrencyPair cp(security);
      if (cp.DenomCurrency() == bs::network::XbtCurrency) {
         itSec = securities_.find(cp.DenomCurrency() + "/" + cp.NumCurrency());
         if (itSec == securities_.end()) {
            return false;
         }
      }
      else {
         return false;
      }
   }
   sd = itSec->second;
   return true;
}

bs::network::Asset::Type AssetManager::GetAssetTypeForSecurity(const std::string &security) const
{
   bs::network::Asset::Type assetType = bs::network::Asset::Type::Undefined;
   bs::network::SecurityDef sd;
   if (securityDef(security, sd)) {
      assetType = sd.assetType;
   }
   return assetType;
}

void AssetManager::onWalletChanged()
{
   act_->onBalanceChanged(bs::network::XbtCurrency);
   act_->onTotalChanged();
}

void AssetManager::onMDSecurityReceived(const std::string &security, const bs::network::SecurityDef &sd)
{
   if (sd.assetType != bs::network::Asset::PrivateMarket) {
      securities_[security] = sd;
   }
}

void AssetManager::onMDSecuritiesReceived()
{
   securitiesReceived_ = true;
}

void AssetManager::onCCSecurityReceived(bs::network::CCSecurityDef ccSD)
{
   ccSecurities_[ccSD.product] = ccSD;

   bs::network::SecurityDef sd = { bs::network::Asset::PrivateMarket};
   securities_[ccSD.securityId] = sd;
}

void AssetManager::onMDUpdate(bs::network::Asset::Type at, const QString &security, bs::network::MDFields fields)
{
   if ((at == bs::network::Asset::Undefined) || security.isEmpty()) {
      return;
   }

   if (bs::network::Asset::isFuturesType(at)) {
      // ignore price update for a futures.
      return;
   }

   double lastPx = 0;
   double bidPrice = 0;

   double productPrice = 0;
   CurrencyPair cp(security.toStdString());
   std::string ccy;

   switch (at) {
   case bs::network::Asset::PrivateMarket:
      ccy = cp.NumCurrency();
      break;
   case bs::network::Asset::SpotXBT:
      ccy = cp.DenomCurrency();
      break;
   default:
      return;
   }

   if (ccy.empty()) {
      return;
   }

   for (const auto &field : fields) {
      if (field.type == bs::network::MDField::PriceLast) {
         lastPx = field.value;
         break;
      } else  if (field.type == bs::network::MDField::PriceBid) {
         bidPrice = field.value;
      }
   }

   productPrice = (lastPx > 0) ? lastPx : bidPrice;

   if (productPrice > 0) {
      if (ccy == cp.DenomCurrency()) {
         productPrice = 1 / productPrice;
      }
      prices_[ccy] = productPrice;
      if (at == bs::network::Asset::PrivateMarket) {
         act_->onCcPriceChanged(ccy);
         act_->onTotalChanged();
      } else {
         sendUpdatesOnXBTPrice(ccy);
      }
   }
}


double AssetManager::getCashTotal()
{
   double total = 0;

   for (const auto &currency : currencies()) {
      total += getBalance(currency, false, nullptr) * getPrice(currency);
   }
   return total;
}

double AssetManager::getCCTotal()
{
   double total = 0;

   for (const auto &ccSec : ccSecurities_) {
      total += getBalance(ccSec.first, false, nullptr) * getPrice(ccSec.first);
   }
   return total;
}

double AssetManager::getTotalAssets()
{
   return walletsManager_->getTotalBalance() + getCashTotal() + getCCTotal();
}

uint64_t AssetManager::getCCLotSize(const std::string &cc) const
{
   const auto ccIt = ccSecurities_.find(cc);
   if (ccIt == ccSecurities_.end()) {
      return 0;
   }
   return ccIt->second.nbSatoshis;
}

bs::Address AssetManager::getCCGenesisAddr(const std::string &cc) const
{
   const auto ccIt = ccSecurities_.find(cc);
   if (ccIt == ccSecurities_.end()) {
      return {};
   }
   return ccIt->second.genesisAddr;
}

void AssetManager::onCelerConnected()
{
   auto cb = [this](const std::vector<std::string>& accountsList) {
      // Remove duplicated entries if possible
      std::set<std::string> accounts(accountsList.begin(), accountsList.end());
      if (accounts.size() == 1) {
         assignedAccount_ = *accounts.begin();
         logger_->debug("[AssetManager] assigned account: {}", assignedAccount_);
      } else {
         this->logger_->error("[AssetManager::onCelerConnected] too many accounts ({})", accounts.size());
         for (const auto &account : accounts) {
            this->logger_->error("[AssetManager::onCelerConnected] acc: {}", account);
         }
      }
   };

   auto seq = std::make_shared<bs::celer::GetAssignedAccountsListSequence>(logger_, cb);
   celerClient_->ExecuteSequence(seq);
}

void AssetManager::onCelerDisconnected()
{
   std::vector<std::string> securitiesToClear;
   for (const auto &security : securities_) {
      if (security.second.assetType != bs::network::Asset::PrivateMarket) {
         securitiesToClear.push_back(security.first);
      }
   }
   for (const auto &security : securitiesToClear) {
      securities_.erase(security);
   }

   balances_.clear();
   currencies_.clear();
   act_->onSecuritiesChanged();
   act_->onFxBalanceCleared();
   act_->onTotalChanged();
}

void AssetManager::onAccountBalanceLoaded(const std::string& currency, double value)
{
   if (currency == bs::network::XbtCurrency) {
      return;
   }
   balances_[currency] = value;
   act_->onBalanceChanged(currency);
   act_->onTotalChanged();
}

void AssetManager::onMessageFromPB(const ProxyTerminalPb::Response &response)
{
   switch (response.data_case()) {
      case Blocksettle::Communication::ProxyTerminalPb::Response::kUpdateOrdersObligations:
         processUpdateOrders(response.update_orders_obligations());
         break;
      case Blocksettle::Communication::ProxyTerminalPb::Response::kUpdateOrder:
         processUpdateOrder(response.update_order());
         break;
      default:
         break;
   }
}

void AssetManager::sendUpdatesOnXBTPrice(const std::string& ccy)
{
   auto currentTime = QDateTime::currentDateTimeUtc();
   bool emitUpdate = false;

   auto it = xbtPriceUpdateTimes_.find(ccy);

   if (it == xbtPriceUpdateTimes_.end()) {
      emitUpdate = true;
      xbtPriceUpdateTimes_.emplace(ccy, currentTime);
   } else {
      if (it->second.secsTo(currentTime) >= 30) {
         it->second = currentTime;
         emitUpdate = true;
      }
   }

   if (emitUpdate) {
      act_->onXbtPriceChanged(ccy);
      act_->onTotalChanged();
   }
}

double AssetManager::profitLoss(int64_t futuresXbtAmount, double futuresBalance, double currentPrice)
{

   auto sign = futuresXbtAmount > 0 ? 1 : -1;
   auto futuresXbtAmountBitcoin = sign * bs::XBTAmount(static_cast<uint64_t>(std::abs(futuresXbtAmount))).GetValueBitcoin();
   return futuresXbtAmountBitcoin * currentPrice - futuresBalance;
}

double AssetManager::profitLossDeliverable(double currentPrice)
{
   return profitLoss(futuresXbtAmountDeliverable_, futuresBalanceDeliverable_, currentPrice);
}

double AssetManager::profitLossCashSettled(double currentPrice)
{
   return profitLoss(futuresXbtAmountCashSettled_, futuresBalanceCashSettled_, currentPrice);
}

void AssetManager::processUpdateOrders(const ProxyTerminalPb::Response_UpdateOrdersAndObligations &msg)
{
   orders_.clear();
   for (const auto &order : msg.orders()) {
      orders_.insert({order.id(), order});
   }
   updateFuturesBalances();
}

void AssetManager::processUpdateOrder(const ProxyTerminalPb::Response_UpdateOrder &msg)
{
   switch (msg.action()) {
      case bs::types::ACTION_CREATED:
      case bs::types::ACTION_UPDATED:
         orders_[msg.order().id()] = msg.order();
         break;
      case bs::types::ACTION_REMOVED:
         orders_.erase(msg.order().id());
         break;
      default:
         break;
   }
   updateFuturesBalances();
}

void AssetManager::updateFuturesBalances()
{
   int64_t futuresXbtAmountDeliverable = 0;
   int64_t futuresXbtAmountCashSettled = 0;
   futuresBalanceDeliverable_ = 0;
   futuresBalanceCashSettled_ = 0;
   for (const auto &item : orders_) {
      const auto &order = item.second;
      if ((order.trade_type() == bs::network::Asset::DeliverableFutures || order.trade_type() == bs::network::Asset::CashSettledFutures)
         && order.status() == bs::types::ORDER_STATUS_PENDING) {
         auto sign = order.quantity() > 0 ? 1 : -1;
         auto amount = bs::XBTAmount(std::abs(order.quantity()));
         auto amountXbt = sign * amount.GetValueBitcoin();
         auto amountSat = sign * static_cast<int64_t>(amount.GetValue());
         auto balanceChange = amountXbt * order.price();
         if (order.trade_type() == bs::network::Asset::DeliverableFutures) {
            futuresXbtAmountDeliverable += amountSat;
            futuresBalanceDeliverable_ += balanceChange;
         } else {
            futuresXbtAmountCashSettled += amountSat;
            futuresBalanceCashSettled_ += balanceChange;
         }
      }
   }
   if (futuresXbtAmountDeliverable != futuresXbtAmountDeliverable_
      || futuresXbtAmountCashSettled != futuresXbtAmountCashSettled_) {
      futuresXbtAmountDeliverable_ = futuresXbtAmountDeliverable;
      futuresXbtAmountCashSettled_ = futuresXbtAmountCashSettled;
      emit netDeliverableBalanceChanged();
   }
}
