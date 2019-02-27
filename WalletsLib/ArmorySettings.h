#ifndef __ARMORY_SETTINGS_H__
#define __ARMORY_SETTINGS_H__

#include <string>
#include <QString>
#include <QStringList>

#include <bdmenums.h>
#include "BtcDefinitions.h"


struct ArmorySettings
{
   NetworkType netType;

   SocketType socketType;

   QString name;
   QString armoryDBKey;
   QString armoryDBIp;
   int armoryDBPort;

   bool runLocally;

   QString armoryExecutablePath;
   QString dbDir;
   QString bitcoinBlocksDir;
   QString dataDir;


   static ArmorySettings fromTextSettings(const QString &text) {
      ArmorySettings server;
      if (text.split(QStringLiteral(":")).size() != 5) {
         return server;
      }
      const QStringList &data = text.split(QStringLiteral(":"));
      server.name = data.at(0);
      server.netType = data.at(1) == QStringLiteral("0") ? NetworkType::MainNet : NetworkType::TestNet;
      server.armoryDBIp = data.at(2);
      server.armoryDBPort = data.at(3).toInt();
      server.armoryDBKey = data.at(4);
   }

   QString toTextSettings() {
      return QStringLiteral("%1:%2:%3:%4:%5")
            .arg(name)
            .arg(netType == NetworkType::MainNet ? 0 : 1)
            .arg(armoryDBIp)
            .arg(armoryDBPort)
            .arg(armoryDBKey);
   }
};

#endif // __ARMORY_SETTINGS_H__
