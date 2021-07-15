/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __ASSET__MANAGER_H__
#define __ASSET__MANAGER_H__

#include "CommonTypes.h"

#include <memory>
#include <unordered_map>

#include <QDateTime>
#include <QMutex>
#include <QObject>

namespace Blocksettle {
   namespace Communication {
      namespace ProxyTerminalPb {
         class Response;
         class Response_UpdateOrdersAndObligations;
         class Response_UpdateOrder;
      }
   }
}

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      class Wallet;
      class WalletsManager;
   }
   namespace types {
      class Order;
   }
}
class MDCallbacksQt;
class CelerClientQt;

struct AssetCallbackTarget
{
   virtual void onCcPriceChanged(const std::string& currency) {}
   virtual void onXbtPriceChanged(const std::string& currency) {}

   virtual void onFxBalanceLoaded() {}
   virtual void onFxBalanceCleared() {}

   virtual void onBalanceChanged(const std::string& currency) {}
   virtual void onTotalChanged() {}
   virtual void onSecuritiesChanged() {}
};

class AssetManager : public QObject, public AssetCallbackTarget
{
    Q_OBJECT

public:
   [[deprecated]] AssetManager(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::sync::WalletsManager> &
      , const std::shared_ptr<MDCallbacksQt> &
      , const std::shared_ptr<CelerClientQt> &);
   AssetManager(const std::shared_ptr<spdlog::logger>&, AssetCallbackTarget *);
   ~AssetManager() override;

   virtual void init();

public:
   std::vector<std::string> currencies();
   virtual std::vector<std::string> privateShares(bool forceExternal = false);
   virtual double getBalance(const std::string& currency, bool includeZc, const std::shared_ptr<bs::sync::Wallet> &wallet) const;
   bool checkBalance(const std::string &currency, double amount, bool includeZc) const;
   double getPrice(const std::string& currency) const;
   double getTotalAssets();
   double getCashTotal();
   double getCCTotal();
   uint64_t getCCLotSize(const std::string &cc) const;
   bs::Address getCCGenesisAddr(const std::string &cc) const;

   double futuresBalanceDeliverable() const { return futuresBalanceDeliverable_; }
   double futuresBalanceCashSettled() const { return futuresBalanceCashSettled_; }
   int64_t futuresXbtAmountDeliverable() const { return futuresXbtAmountDeliverable_; }
   int64_t futuresXbtAmountCashSettled() const { return futuresXbtAmountCashSettled_; }

   bool hasSecurities() const { return securitiesReceived_; }
   std::vector<QString> securities(bs::network::Asset::Type = bs::network::Asset::Undefined) const;

   bs::network::Asset::Type GetAssetTypeForSecurity(const std::string &security) const;

   bool HaveAssignedAccount() const { return !assignedAccount_.empty(); }
   std::string GetAssignedAccount() const { return assignedAccount_; }

   static double profitLoss(int64_t futuresXbtAmount, double futuresBalance, double currentPrice);
   double profitLossDeliverable(double currentPrice);
   double profitLossCashSettled(double currentPrice);

signals:
   void ccPriceChanged(const std::string& currency);
   void xbtPriceChanged(const std::string& currency);

   void fxBalanceLoaded();
   void fxBalanceCleared();

   void balanceChanged(const std::string& currency);
   void netDeliverableBalanceChanged();

   void totalChanged();
   void securitiesChanged();

 public slots:
    void onCCSecurityReceived(bs::network::CCSecurityDef);
    void onMDUpdate(bs::network::Asset::Type, const QString &security, bs::network::MDFields);
    void onMDSecurityReceived(const std::string &security, const bs::network::SecurityDef &sd);
    void onMDSecuritiesReceived();
    void onAccountBalanceLoaded(const std::string& currency, double value);
    void onMessageFromPB(const Blocksettle::Communication::ProxyTerminalPb::Response &response);

   void onCelerConnected();
   void onCelerDisconnected();
   void onWalletChanged();

protected:
   bool securityDef(const std::string &security, bs::network::SecurityDef &) const;

private:
  void sendUpdatesOnXBTPrice(const std::string& ccy);
  void processUpdateOrders(const Blocksettle::Communication::ProxyTerminalPb::Response_UpdateOrdersAndObligations &msg);
  void processUpdateOrder(const Blocksettle::Communication::ProxyTerminalPb::Response_UpdateOrder &msg);

  void onCcPriceChanged(const std::string& currency) override { emit ccPriceChanged(currency); }
  void onXbtPriceChanged(const std::string& currency) override { emit xbtPriceChanged(currency); }
  void onFxBalanceLoaded() override { emit fxBalanceLoaded(); }
  void onFxBalanceCleared() override { emit fxBalanceCleared(); }

  void onBalanceChanged(const std::string& currency) override { emit balanceChanged(currency); }
  void onTotalChanged() override { emit totalChanged(); }
  void onSecuritiesChanged() override { emit securitiesChanged(); }

protected:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<bs::sync::WalletsManager> walletsManager_;
   std::shared_ptr<MDCallbacksQt>         mdCallbacks_;
   std::shared_ptr<CelerClientQt>         celerClient_;
   AssetCallbackTarget* act_{ nullptr };

   bool     securitiesReceived_ = false;
   std::vector<std::string>   currencies_;
   QMutex   mtxCurrencies_;
   std::unordered_map<std::string, double> balances_;
   std::unordered_map<std::string, double> prices_;
   std::unordered_map<std::string, bs::network::SecurityDef>   securities_;
   std::unordered_map<std::string, bs::network::CCSecurityDef> ccSecurities_;

   std::string assignedAccount_;

   std::unordered_map<std::string, QDateTime>  xbtPriceUpdateTimes_;

   std::map<std::string, bs::types::Order> orders_;

   double futuresBalanceDeliverable_{};
   double futuresBalanceCashSettled_{};
   int64_t futuresXbtAmountDeliverable_{};
   int64_t futuresXbtAmountCashSettled_{};

};

#endif // __ASSET__MANAGER_H__
