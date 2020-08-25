/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthEidAdapter.h"
#include <spdlog/spdlog.h>

#include "common.pb.h"

using namespace BlockSettle::Common;


AuthEidAdapter::AuthEidAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &user)
   : logger_(logger)
   , user_(user)
{}

bool AuthEidAdapter::process(const bs::message::Envelope &env)
{
   return true;
}
