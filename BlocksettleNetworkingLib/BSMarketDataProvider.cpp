/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BSMarketDataProvider.h"

#include "ConnectionManager.h"
#include "CurrencyPair.h"
#include "WsDataConnection.h"
#include <QCoreApplication>

#include <spdlog/spdlog.h>

#include <vector>

static const std::string kUsdCcyName = "USD";

BSMarketDataProvider::BSMarketDataProvider(const std::shared_ptr<ConnectionManager> &connectionManager
      , const std::shared_ptr<spdlog::logger> &logger, MDCallbackTarget *callbacks
      , bool secureConnection, bool acceptUsdPairs)
 : MarketDataProvider(logger, callbacks)
 , connectionManager_{connectionManager}
 , acceptUsdPairs_{acceptUsdPairs}
 , secureConnection_(secureConnection)
{}

bool BSMarketDataProvider::StartMDConnection()
{
   if (mdConnection_ != nullptr) {
      logger_->error("[BSMarketDataProvider::StartMDConnection] already connected");
      return false;
   }

   mdConnection_ = secureConnection_ ?
      connectionManager_->CreateSecureWsConnection() : connectionManager_->CreateInsecureWsConnection();

   logger_->debug("[BSMarketDataProvider::StartMDConnection] start connecting to PB updates");

   callbacks_->startConnecting();
   if (!mdConnection_->openConnection(host_, port_, this)) {
      logger_->error("[BSMarketDataProvider::StartMDConnection] failed to start connection");
      callbacks_->disconnected();
      return false;
   }

   return true;
}

void BSMarketDataProvider::StopMDConnection()
{
   callbacks_->onMDUpdate(bs::network::Asset::Undefined, {}, {});

   if (mdConnection_ != nullptr) {
      mdConnection_->closeConnection();
      mdConnection_ = nullptr;
   }
   callbacks_->disconnected();
}

void BSMarketDataProvider::OnDataReceived(const std::string &data)
{
   Blocksettle::Communication::BlocksettleMarketData::UpdateHeader header;

   if (!header.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::onDataFromMD] failed to parse header");
      return ;
   }

   switch (header.type()) {
   case Blocksettle::Communication::BlocksettleMarketData::FullSnapshotType:
      OnFullSnapshot(header.data());
      break;
   case Blocksettle::Communication::BlocksettleMarketData::IncrementalUpdateType:
      OnIncrementalUpdate(header.data());
      break;
   case Blocksettle::Communication::BlocksettleMarketData::NewSettledTreadeUpdateType:
      OnNewTradeUpdate(header.data());
      break;
   }
}

void BSMarketDataProvider::OnConnected()
{
   callbacks_->connected();
}

void BSMarketDataProvider::OnDisconnected()
{
   // Can't stop connection from callback.
   // FIXME: Do not use qApp here.
   QMetaObject::invokeMethod(qApp, [this] {
      callbacks_->disconnecting();
      callbacks_->onMDUpdate(bs::network::Asset::Undefined, {}, {});

      mdConnection_ = nullptr;
      callbacks_->disconnected();
   });
}

void BSMarketDataProvider::OnError(DataConnectionListener::DataConnectionError errorCode)
{
}

bool BSMarketDataProvider::IsConnectionActive() const
{
   return mdConnection_ != nullptr;
}

bool BSMarketDataProvider::DisconnectFromMDSource()
{
   if (mdConnection_ == nullptr) {
      return true;
   }
   callbacks_->disconnecting();

   if (mdConnection_ != nullptr)
      mdConnection_->closeConnection();

   return true;
}

bs::network::MDFields GetMDFields(const Blocksettle::Communication::BlocksettleMarketData::ProductPriceInfo& productInfo)
{
   bs::network::MDFields result;

   if (!qFuzzyIsNull(productInfo.offer())) {
      result.emplace_back( bs::network::MDField{ bs::network::MDField::PriceOffer, productInfo.offer(), QString()} );
   }
   if (!qFuzzyIsNull(productInfo.bid())) {
      result.emplace_back( bs::network::MDField{ bs::network::MDField::PriceBid, productInfo.bid(), QString()} );
   }
   if (!qFuzzyIsNull(productInfo.last_price())) {
      result.emplace_back( bs::network::MDField{ bs::network::MDField::PriceLast, productInfo.last_price(), QString()} );
   }

   result.emplace_back( bs::network::MDField{ bs::network::MDField::DailyVolume, productInfo.volume(), QString()} );

   return result;
}

void BSMarketDataProvider::OnProductSnapshot(const bs::network::Asset::Type& assetType
   , const Blocksettle::Communication::BlocksettleMarketData::ProductPriceInfo& productInfo
   , double timestamp)
{
   callbacks_->onMDSecurityReceived(productInfo.product_name(), {assetType});
   auto fields = GetMDFields(productInfo);
   fields.emplace_back(bs::network::MDField{bs::network::MDField::MDTimestamp, timestamp, {}});
   callbacks_->onMDUpdate(assetType, productInfo.product_name(), GetMDFields(productInfo));
}

void BSMarketDataProvider::OnFullSnapshot(const std::string& data)
{
   Blocksettle::Communication::BlocksettleMarketData::MDSnapshot snapshot;
   if (!snapshot.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::OnFullSnapshot] failed to parse snapshot");
      return ;
   }

   double timestamp = static_cast<double>(snapshot.timestamp());

   for (int i=0; i < snapshot.fx_products_size(); ++i) {
      const auto& productInfo = snapshot.fx_products(i);

      if (!acceptUsdPairs_) {
         CurrencyPair cp{productInfo.product_name()};
         if (cp.NumCurrency() == kUsdCcyName || cp.DenomCurrency() == kUsdCcyName) {
            continue;
         }
      }

      OnProductSnapshot(bs::network::Asset::Type::SpotFX, productInfo, timestamp);
   }

   for (int i=0; i < snapshot.xbt_products_size(); ++i) {
      const auto& productInfo = snapshot.xbt_products(i);

      if (!acceptUsdPairs_) {
         CurrencyPair cp{productInfo.product_name()};
         if (cp.DenomCurrency() == kUsdCcyName) {
            continue;
         }
      }

      OnProductSnapshot(bs::network::Asset::Type::SpotXBT, productInfo, timestamp);
   }

   for (int i=0; i < snapshot.cc_products_size(); ++i) {
      OnProductSnapshot(bs::network::Asset::Type::PrivateMarket, snapshot.cc_products(i), timestamp);
   }
}

void BSMarketDataProvider::OnProductUpdate(const bs::network::Asset::Type& assetType
   , const Blocksettle::Communication::BlocksettleMarketData::ProductPriceInfo& productInfo
   , double timestamp)
{
   auto fields = GetMDFields(productInfo);
   if (!fields.empty()) {
      fields.emplace_back(bs::network::MDField{bs::network::MDField::MDTimestamp, timestamp, {}});
      callbacks_->onMDUpdate(assetType, productInfo.product_name(), fields);
   }
}

void BSMarketDataProvider::OnIncrementalUpdate(const std::string& data)
{
   Blocksettle::Communication::BlocksettleMarketData::MDSnapshot update;
   if (!update.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::OnIncrementalUpdate] failed to parse update");
      return ;
   }

   double timestamp = static_cast<double>(update.timestamp());

   for (int i=0; i < update.fx_products_size(); ++i) {
      const auto& productInfo = update.fx_products(i);

      if (!acceptUsdPairs_) {
         CurrencyPair cp{productInfo.product_name()};
         if (cp.NumCurrency() == kUsdCcyName || cp.DenomCurrency() == kUsdCcyName) {
            continue;
         }
      }

      OnProductUpdate(bs::network::Asset::Type::SpotFX, productInfo, timestamp);
   }

   for (int i=0; i < update.xbt_products_size(); ++i) {
      const auto& productInfo = update.xbt_products(i);

      if (!acceptUsdPairs_) {
         CurrencyPair cp{productInfo.product_name()};
         if (cp.DenomCurrency() == kUsdCcyName) {
            continue;
         }
      }

      OnProductUpdate(bs::network::Asset::Type::SpotXBT, productInfo, timestamp);
   }

   for (int i=0; i < update.cc_products_size(); ++i) {
      OnProductUpdate(bs::network::Asset::Type::PrivateMarket, update.cc_products(i), timestamp);
   }
}

void BSMarketDataProvider::OnNewTradeUpdate(const std::string& data)
{
   Blocksettle::Communication::BlocksettleMarketData::NewTradeUpdate update;
   if (!update.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::OnNewTradeUpdate] failed to parse update");
      return ;
   }

   switch (update.trade_type()) {
   case Blocksettle::Communication::BlocksettleMarketData::MDTradeType::FXTradeType:
      OnNewFXTradeUpdate(update.trade());
      break;
   case Blocksettle::Communication::BlocksettleMarketData::MDTradeType::XBTTradeType:
      OnNewXBTTradeUpdate(update.trade());
      break;
   case Blocksettle::Communication::BlocksettleMarketData::MDTradeType::PMTradeType:
      OnNewPMTradeUpdate(update.trade());
      break;
   default:
      logger_->error("[BSMarketDataProvider::OnNewTradeUpdate] undefined trade type: {}"
         , static_cast<int>(update.trade_type()));
      break;
   }
}

void BSMarketDataProvider::OnNewFXTradeUpdate(const std::string& data)
{
   Blocksettle::Communication::BlocksettleMarketData::MDTradeRecord trade_record;
   if (!trade_record.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::OnNewFXTradeUpdate] failed to parse trade");
      return ;
   }

   logger_->debug("[BSMarketDataProvider::OnNewFXTradeUpdate] loaded trade: {}"
      , trade_record.DebugString());

   bs::network::NewTrade trade;

   trade.product = trade_record.product();
   trade.price = trade_record.price();
   trade.amount = trade_record.amount();
   trade.timestamp = trade_record.timestamp();

   callbacks_->onNewFXTrade(trade);
}

void BSMarketDataProvider::OnNewXBTTradeUpdate(const std::string& data)
{
   Blocksettle::Communication::BlocksettleMarketData::MDTradeRecord trade_record;
   if (!trade_record.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::OnNewXBTTradeUpdate] failed to parse trade");
      return ;
   }

   bs::network::NewTrade trade;

   trade.product = trade_record.product();
   trade.price = trade_record.price();
   trade.amount = trade_record.amount();
   trade.timestamp = trade_record.timestamp();

   callbacks_->onNewXBTTrade(trade);
}

void BSMarketDataProvider::OnNewPMTradeUpdate(const std::string& data)
{
   Blocksettle::Communication::BlocksettleMarketData::MDPMTradeRecord trade_record;
   if (!trade_record.ParseFromString(data)) {
      logger_->error("[BSMarketDataProvider::OnNewPMTradeUpdate] failed to parse trade");
      return ;
   }

   bs::network::NewPMTrade trade;

   trade.product = trade_record.product();
   trade.price = trade_record.price();
   trade.amount = trade_record.amount();
   trade.timestamp = trade_record.timestamp();

   callbacks_->onNewPMTrade(trade);
}
