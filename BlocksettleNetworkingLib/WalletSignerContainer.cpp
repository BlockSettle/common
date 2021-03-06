/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WalletSignerContainer.h"


WalletSignerContainer::WalletSignerContainer(const std::shared_ptr<spdlog::logger> &logger
   , SignerCallbackTarget *sct, OpMode opMode)
  : SignContainer(logger, sct, opMode)
{}
