#include <QFont>
#include <QTreeView>
#include "SignerKeysModel.h".h"
#include "EncryptionUtils.h"
#include "ArmoryConnection.h"

namespace {
   int kSignerKeysColumns = 3;
}

SignerKeysModel::SignerKeysModel(const std::shared_ptr<ApplicationSettings> &appSettings
                                               , QObject *parent)
   : QAbstractTableModel(parent)
   , appSettings_(appSettings)
{
   update();
   connect(appSettings.get(), &ApplicationSettings::settingChanged, this, [this](int setting, QVariant value){
      if (setting == ApplicationSettings::remoteSignerKeys) {
         update();
      }
   });
}

int SignerKeysModel::columnCount(const QModelIndex&) const
{
   return kSignerKeysColumns;
}

int SignerKeysModel::rowCount(const QModelIndex&) const
{
   return signerKeys_.size();
}

QVariant SignerKeysModel::data(const QModelIndex &index, int role) const
{
   if (index.row() >= signerKeys_.size()) return QVariant();
   SignerKey signerKey = signerKeys_.at(index.row());

   if (role == Qt::DisplayRole) {
      switch (index.column()) {
      case ColumnName:
         return signerKey.name;
      case ColumnAddress:
         return signerKey.address;
      case ColumnKey:
         return signerKey.key;
      default:
         break;
      }
   }
   return QVariant();
}

QVariant SignerKeysModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   if (orientation != Qt::Horizontal) {
      return QVariant();
   }

   if (role == Qt::DisplayRole) {
      switch(static_cast<ArmoryServersViewViewColumns>(section)) {
      case ArmoryServersViewViewColumns::ColumnName:
         return tr("Name");
      case ArmoryServersViewViewColumns::ColumnAddress:
         return tr("Address");
      case ArmoryServersViewViewColumns::ColumnKey:
         return tr("Key");
      default:
         return QVariant();
      }
   }

   return QVariant();
}

void SignerKeysModel::addSignerKey(const SignerKey &key)
{
   QList<SignerKey> keysCopy = signerKeys_;

   keysCopy.append(key);
   //appSettings_->set(ApplicationSettings::remoteSignerKeys, keysCopy);
   update();
}

void SignerKeysModel::deleteSignerKey(int index)
{
   QList<SignerKey> signerKeysCopy = signerKeys_;
   signerKeysCopy.removeAt(index);

   saveSignerKeys(signerKeysCopy);
   update();
}

void SignerKeysModel::modifySignerKey(int index, const SignerKey &key)
{
   if (index < 0 || index > signerKeys_.size()) {
      return;
   }
   QList<SignerKey> signerKeysCopy = signerKeys_;
   signerKeysCopy[index] = key;

   saveSignerKeys(signerKeysCopy);
   update();
}

void SignerKeysModel::saveSignerKeys(QList<SignerKey> signerKeys)
{
   QStringList signerKeysString;
   for (const SignerKey &key : signerKeys) {
      QString s = QStringLiteral("%1:%2:%3").arg(key.name).arg(key.address).arg(key.key);
      signerKeysString.append(s);
   }

   appSettings_->set(ApplicationSettings::remoteSignerKeys, signerKeysString);
}

void SignerKeysModel::update()
{
   beginResetModel();

   signerKeys_.clear();
   QStringList keysString = appSettings_->get(ApplicationSettings::remoteSignerKeys).toString().split(QStringLiteral(","));

   SignerKey signerKey;
   for (const QString &s: keysString) {
      const QStringList &keyString = s.split(QStringLiteral(""));
      if (keyString.size() != 3) {
         continue;
      }
      signerKey.name = keysString.at(0);
      signerKey.address = keysString.at(1);
      signerKey.key = keysString.at(2);

      signerKeys_.append(signerKey);
   }

   endResetModel();
}

void SignerKeysModel::getSignerKeys()
{

}


