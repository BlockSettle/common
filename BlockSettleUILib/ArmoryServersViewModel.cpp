#include <QFont>
#include <QTreeView>
#include "ArmoryServersViewModel.h"
#include "EncryptionUtils.h"

namespace {
   int kArmoryServerColumns = 3;
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
            return values.at(0) + QStringLiteral(":") + values.at(1);
         }
         else {
            return values.at(index.column());
         }
      }
      else {
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

