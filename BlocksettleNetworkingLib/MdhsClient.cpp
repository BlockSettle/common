/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "MdhsClient.h"

#include "ConnectionManager.h"
#include "FastLock.h"
#include "RequestReplyCommand.h"

#include "market_data_history.pb.h"

#include <spdlog/logger.h>

MdhsClient::MdhsClient(const std::shared_ptr<ConnectionManager>& connectionManager
   , const std::shared_ptr<spdlog::logger>& logger
   , const std::string &host
   , const std::string &port
   , QObject* pParent)
   : QObject(pParent)
   , connectionManager_(connectionManager)
   , logger_(logger)
   , host_(host)
   , port_(port)
{
}

MdhsClient::~MdhsClient() noexcept = default;

void MdhsClient::SendRequest(const MarketDataHistoryRequest& request)
{
   requestId_ += 1;
   int requestId = requestId_;

   auto apiConnection = connectionManager_->CreateSecureWsConnection();
   auto command = std::make_unique<RequestReplyCommand>("MdhsClient", apiConnection, logger_);

   command->SetReplyCallback([requestId, this](const std::string& data) -> bool {
      QMetaObject::invokeMethod(this, [this, requestId, data] {
         activeCommands_.erase(requestId);
         emit DataReceived(data);
      });
      return true;
   });

   command->SetErrorCallback([requestId, this](const std::string& message) {
      logger_->error("Failed to get history data from mdhs: {}", message);
      QMetaObject::invokeMethod(this, [this, requestId] {
         activeCommands_.erase(requestId);
      });
   });

   if (!command->ExecuteRequest(host_, port_, request.SerializeAsString())) {
      logger_->error("Failed to send request for mdhs.");
      return;
   }

   activeCommands_.emplace(requestId, std::move(command));
}
