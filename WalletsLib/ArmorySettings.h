#ifndef __ARMORY_SETTINGS_H__
#define __ARMORY_SETTINGS_H__

#include <string>
#include <QString>
#include <QStringList>

#include <bdmenums.h>
#include "BtcDefinitions.h"

struct ArmoryServer
{
   QString name;
   NetworkType netType = NetworkType::Invalid;
   QString armoryDBIp;
   int armoryDBPort = 0;
   QString armoryDBKey;
   bool runLocally = false;

   static ArmoryServer fromTextSettings(const QString &text) {
      ArmoryServer server;
      if (text.split(QStringLiteral(":")).size() != 5) {
         return server;
      }
      const QStringList &data = text.split(QStringLiteral(":"));
      server.name = data.at(0);
      server.netType = data.at(1) == QStringLiteral("0") ? NetworkType::MainNet : NetworkType::TestNet;
      server.armoryDBIp = data.at(2);
      server.armoryDBPort = data.at(3).toInt();
      server.armoryDBKey = data.at(4);

      return server;
   }

   QString toTextSettings() const {
      return QStringLiteral("%1:%2:%3:%4:%5")
            .arg(name)
            .arg(netType == NetworkType::MainNet ? 0 : 1)
            .arg(armoryDBIp)
            .arg(armoryDBPort)
            .arg(armoryDBKey);
   }
};

struct ArmorySettings : public ArmoryServer
{
   SocketType socketType;

   QString armoryExecutablePath;
   QString dbDir;
   QString bitcoinBlocksDir;
   QString dataDir;
};

#endif // __ARMORY_SETTINGS_H__
