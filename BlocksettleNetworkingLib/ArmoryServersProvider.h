/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef ARMORY_SERVERS_PROVIDER_H
#define ARMORY_SERVERS_PROVIDER_H

#include <QObject>
#include "ApplicationSettings.h"
#include "BinaryData.h"

#define ARMORY_BLOCKSETTLE_NAME "BlockSettle"

#define MAINNET_ARMORY_BLOCKSETTLE_ADDRESS "armory.blocksettle.com"
#define MAINNET_ARMORY_BLOCKSETTLE_PORT 80

#define TESTNET_ARMORY_BLOCKSETTLE_ADDRESS "armory-testnet.blocksettle.com"
#define TESTNET_ARMORY_BLOCKSETTLE_PORT 80

class BootstrapDataManager;

class ArmoryServersProvider : public QObject
{
   Q_OBJECT
public:
   ArmoryServersProvider(const std::shared_ptr<ApplicationSettings> &appSettings
                         , const std::shared_ptr<BootstrapDataManager>& bootstrapDataManager
                         , QObject *parent = nullptr);

   QList<ArmoryServer> servers() const;
   ArmorySettings getArmorySettings() const;

   int indexOfCurrent() const;   // index of server which set in ini file
   int indexOfConnected() const;   // index of server currently connected
   int indexOf(const QString &name) const;
   int indexOf(const ArmoryServer &server) const;
   int indexOfIpPort(const std::string &srvIPPort) const;
   static int getIndexOfMainNetServer();
   static int getIndexOfTestNetServer();

   bool add(const ArmoryServer &server);
   bool replace(int index, const ArmoryServer &server);
   bool remove(int index);
   void setupServer(int index, bool needUpdate=true);

   void addKey(const QString &address, int port, const QString &key);
   void addKey(const std::string &srvIPPort, const BinaryData &srvPubKey);

   static const int kDefaultServersCount;

   ArmorySettings connectedArmorySettings() const;
   void setConnectedArmorySettings(const ArmorySettings &connectedArmorySettings);

   // if default armory used
   bool isDefault(int index) const;

signals:
   void dataChanged();
private:
   std::shared_ptr<ApplicationSettings>   appSettings_;
   std::shared_ptr<BootstrapDataManager>  bootstrapDataManager_;

   static const QList<ArmoryServer> defaultServers_;

   ArmorySettings connectedArmorySettings_;  // latest connected server
};

#endif // ARMORY_SERVERS_PROVIDER_H
