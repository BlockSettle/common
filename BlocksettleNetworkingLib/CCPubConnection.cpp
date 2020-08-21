/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CCPubConnection.h"

#include "RequestReplyCommand.h"

#include "bs_communication.pb.h"

#include <spdlog/spdlog.h>

using namespace Blocksettle::Communication;

CCPubConnection::CCPubConnection(const std::shared_ptr<spdlog::logger> &logger)
   : logger_{logger}
{}

void CCPubConnection::OnDataReceived(const std::string& data)
{
   if (data.empty()) {
      return;
   }
   ResponsePacket response;

   if (!response.ParseFromString(data)) {
      logger_->error("[CCPubConnection::OnDataReceived] failed to parse response from public bridge");
      return;
   }

   switch (response.responsetype()) {
   case RequestType::GetCCGenesisAddressesType:
      ProcessGenAddressesResponse(response.responsedata(), response.datasignature());
      break;
   default:
      break;
   }
}
