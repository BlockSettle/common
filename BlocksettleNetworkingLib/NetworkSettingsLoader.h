/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef NETWORK_SETTINGS_LOADER_H
#define NETWORK_SETTINGS_LOADER_H

#include <spdlog/spdlog.h>
#include <memory>
#include <QObject>
#include "BIP15xHelpers.h"

class RequestReplyCommand;

struct NetworkAddress {
   std::string host;
   int port{};
};

namespace Blocksettle {
   namespace Communication {
      enum GetNetworkSettingsResponse_Status : int;
   }
}

struct NetworkSettings {
   NetworkAddress  marketData;
   NetworkAddress  mdhs;
   NetworkAddress  chat;
   NetworkAddress  proxy;
   bool            isSet = false;
   Blocksettle::Communication::GetNetworkSettingsResponse_Status status{};
   std::string statusMsg;
};

class NetworkSettingsLoader : public QObject
{
   Q_OBJECT
public:
   NetworkSettingsLoader(const std::shared_ptr<spdlog::logger> &logger
      , const std::string &pubHost, const std::string &pubPort
      , const bs::network::BIP15xNewKeyCb &, QObject *parent = nullptr);
   ~NetworkSettingsLoader() override;

   const NetworkSettings &settings() const { return networkSettings_; }

   // Do not call if network setting was already loaded!
   // It's normal to call multiple times if loading was started but is not ready yet.
   void loadSettings();

signals:
   void failed(const QString &errorMsg);
   void succeed();

private:
   void sendFailedAndReset(const QString &errorMsg);

   std::shared_ptr<spdlog::logger> logger_;
   const bs::network::BIP15xNewKeyCb   cbApprove_{ nullptr };

   const std::string pubHost_;
   const std::string pubPort_;

   std::shared_ptr<RequestReplyCommand> cmdPuBSettings_;

   NetworkSettings networkSettings_;
};

#endif
