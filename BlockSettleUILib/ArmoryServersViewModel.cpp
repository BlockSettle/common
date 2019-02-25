#include <QFont>
#include <QTreeView>
#include "ArmoryServersViewModel.h"
#include "EncryptionUtils.h"

namespace {
   int kArmoryServerColumns = 5;
}

ArmoryServersViewModel::ArmoryServersViewModel(const std::shared_ptr<ApplicationSettings> &appSettings
                                               , bool onlyAddressAndPort
                                               , QObject *parent)
   : QAbstractTableModel(parent)
   , appSettings_(appSettings)
   , onlyAddressAndPort_(onlyAddressAndPort)
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
   const QString& server = servers_.at(index.row());
   QStringList values = server.split(QStringLiteral(":"));
   if (values.size() != kArmoryServerColumns) return QVariant();

   if (role == Qt::DisplayRole) {
      // join address and port for combobox
      if (onlyAddressAndPort_) {
         if (index.column() == 0 || index.column() == 1) {
            return values.at(2) + QStringLiteral(":") + values.at(3);
         }
         else {
            return values.at(index.column());
         }
      }
      else {
         if (index.column() == 1) {
            return values.at(index.column()) == QStringLiteral("0") ? tr("MainNet") : tr("TestNet");
         }
         return values.at(index.column());
      }
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
         return tr("Network \ntype");
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
   endResetModel();
}

