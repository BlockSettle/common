/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SignContainer.h"

#include "ApplicationSettings.h"

#include <QTcpSocket>
#include <spdlog/spdlog.h>

Q_DECLARE_METATYPE(std::shared_ptr<bs::sync::hd::Wallet>)


SignContainer::SignContainer(const std::shared_ptr<spdlog::logger> &logger
   , SignerCallbackTarget *sct, OpMode opMode)
   : logger_(logger), sct_(sct), mode_(opMode)
{
   qRegisterMetaType<std::shared_ptr<bs::sync::hd::Wallet>>();
}

void SignContainer::syncNewAddress(const std::string &walletId, const std::string &index
   , const std::function<void(const bs::Address &)> &cb)
{
   const auto &cbAddrs = [cb](const std::vector<std::pair<bs::Address, std::string>> &outAddrs) {
      if (outAddrs.size() == 1) {
         if (cb)
            cb(outAddrs[0].first);
      } else {
         if (cb)
            cb({});
      }
   };
   syncNewAddresses(walletId, { index }, cbAddrs);
}


bool SignerConnectionExists(const QString &host, const QString &port)
{
   QTcpSocket sock;
   sock.connectToHost(host, port.toUInt());
   return sock.waitForConnected(30);
}
