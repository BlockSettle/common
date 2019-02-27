#include <QFont>
#include <QTreeView>
#include "ArmoryServersViewModel.h"
#include "EncryptionUtils.h"
#include "ArmoryConnection.h"

namespace {
   int kArmoryServerColumns = 5;
}

ArmoryServersViewModel::ArmoryServersViewModel(const std::shared_ptr<ApplicationSettings> &appSettings
                                               , QObject *parent)
   : QAbstractTableModel(parent)
   , appSettings_(appSettings)
{
   reloadServers();
}

int ArmoryServersViewModel::columnCount(const QModelIndex&) const
{
   return kArmoryServerColumns;
}

int ArmoryServersViewModel::rowCount(const QModelIndex&) const
{
   return servers_.size();
}

QVariant ArmoryServersViewModel::data(const QModelIndex &index, int role) const
{
   if (index.row() >= servers_.size()) return QVariant();
   QString server = servers_.at(index.row());

   QStringList values = server.split(QStringLiteral(":"));
   if (values.size() != kArmoryServerColumns) return QVariant();

   if (role == Qt::DisplayRole) {
      if (index.column() == 1) {
         return values.at(index.column()) == QStringLiteral("0") ? tr("MainNet") : tr("TestNet");
      }
      return values.at(index.column());
   }
   return QVariant();
}

QVariant ArmoryServersViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   if (orientation != Qt::Horizontal) {
      return QVariant();
   }

   if (role == Qt::DisplayRole) {
      switch(static_cast<ArmoryServersViewViewColumns>(section)) {
      case ArmoryServersViewViewColumns::ColumnName:
         return tr("Name");
      case ArmoryServersViewViewColumns::ColumnType:
         return tr("Network");
      case ArmoryServersViewViewColumns::ColumnAddress:
         return tr("Address");
      case ArmoryServersViewViewColumns::ColumnPort:
         return tr("Port");
      case ArmoryServersViewViewColumns::ColumnKey:
         return tr("Key");
      default:
         return QVariant();
      }
   }

   return QVariant();
}

void ArmoryServersViewModel::reloadServers()
{
   beginResetModel();

   servers_ = appSettings_->get<QStringList>(ApplicationSettings::armoryServers);

   // prepend default BlockSettle servers
   QString mainNetServer = QStringLiteral("%1:%2:%3:%4:%5")
         .arg(QStringLiteral(MAINNET_ARMORY_BLOCKSETTLE_NAME))
         .arg(QStringLiteral("0"))
         .arg(QStringLiteral(MAINNET_ARMORY_BLOCKSETTLE_ADDRESS))
         .arg(QString::number(MAINNET_ARMORY_BLOCKSETTLE_PORT))
         .arg(QStringLiteral(MAINNET_ARMORY_BLOCKSETTLE_KEY));

   QString testNetServer = QStringLiteral("%1:%2:%3:%4:%5")
         .arg(QStringLiteral(TESTNET_ARMORY_BLOCKSETTLE_NAME))
         .arg(QStringLiteral("0"))
         .arg(QStringLiteral(TESTNET_ARMORY_BLOCKSETTLE_ADDRESS))
         .arg(QString::number(TESTNET_ARMORY_BLOCKSETTLE_PORT))
         .arg(QStringLiteral(TESTNET_ARMORY_BLOCKSETTLE_KEY));

   servers_.prepend(testNetServer);
   servers_.prepend(mainNetServer);

   endResetModel();
}

