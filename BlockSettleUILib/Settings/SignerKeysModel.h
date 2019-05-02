#ifndef __SIGNER_KEYS_MODEL_H__
#define __SIGNER_KEYS_MODEL_H__

#include <QAbstractTableModel>
#include <memory>

#include "AuthAddress.h"
#include "AuthAddressManager.h"
#include "BinaryData.h"
#include "ApplicationSettings.h"

struct SignerKey
{
   QString name;
   QString address;
   QString key;
};

class SignerKeysModel : public QAbstractTableModel
{
public:
   SignerKeysModel(const std::shared_ptr<ApplicationSettings>& appSettings
                          , QObject *parent = nullptr);
   ~SignerKeysModel() noexcept = default;

   SignerKeysModel(const SignerKeysModel&) = delete;
   SignerKeysModel& operator = (const SignerKeysModel&) = delete;

   SignerKeysModel(SignerKeysModel&&) = delete;
   SignerKeysModel& operator = (SignerKeysModel&&) = delete;

public:
   int columnCount(const QModelIndex &parent = QModelIndex()) const override;
   int rowCount(const QModelIndex &parent = QModelIndex()) const override;

   QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
   QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;


   void addSignerKey(const SignerKey &key);
   void deleteSignerKey(int index);
   void editSignerKey(int index, const SignerKey &key);

   void saveSignerKeys(QList<SignerKey> signerKeys);

   QList<SignerKey> signerKeys_;
public slots:
   void update();

private:
   std::shared_ptr<ApplicationSettings> appSettings_;


   enum ArmoryServersViewViewColumns : int
   {
      ColumnName,
      ColumnAddress,
      ColumnKey
   };
};

#endif // __SIGNER_KEYS_MODEL_H__
