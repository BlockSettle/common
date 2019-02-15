#ifndef __BS_QPASSWORDDATA_H__
#define __BS_QPASSWORDDATA_H__

#include <QObject>
#include "MetaData.h"
#include "WalletEncryption.h"


namespace bs {
namespace wallet {

//// PasswordData::password might be either binary ot plain text depends of wallet encryption type
//// for QEncryptionType::Password - it's plain text
//// for QEncryptionType::Auth - it's binary
//// textPassword and binaryPassword properties provides Qt interfaces for PasswordData::password usable in QML
//// password size limited to 32 bytes
class QPasswordData : public QObject, public PasswordData {
   Q_OBJECT
public:
   Q_PROPERTY(QString textPassword READ textPassword WRITE setTextPassword NOTIFY passwordChanged)
   Q_PROPERTY(SecureBinaryData binaryPassword READ binaryPassword WRITE setBinaryPassword NOTIFY passwordChanged)
   Q_PROPERTY(bs::wallet::EncryptionType encType READ getEncType WRITE setEncType NOTIFY encTypeChanged)
   Q_PROPERTY(QString encKey READ getEncKey WRITE setEncKey NOTIFY encKeyChanged)

   QPasswordData(QObject *parent = nullptr) : QObject(parent), PasswordData() {}

   // copy constructors and operator= uses parent implementation
   QPasswordData(const PasswordData &other) : PasswordData(other){}
   QPasswordData(const QPasswordData &other) : PasswordData(static_cast<PasswordData>(other)) {}
   QPasswordData& operator= (const QPasswordData &other) { PasswordData::operator=(other); return *this;}

   QString                 textPassword() const             { return QString::fromStdString(password.toBinStr()); }
   SecureBinaryData        binaryPassword() const           { return password; }
   EncryptionType          getEncType() const               { return encType; }
   QString                 getEncKey() const                { return QString::fromStdString(encKey.toBinStr()); }

   void setTextPassword    (const QString &pw)              { password =  SecureBinaryData(pw.toStdString()); emit passwordChanged(); }
   void setBinaryPassword  (const SecureBinaryData &data)   { password =  data; emit passwordChanged(); }
   void setEncType         (EncryptionType e)               { encType =  e; emit encTypeChanged(e); }
   void setEncKey          (const QString &e)               { encKey =  SecureBinaryData(e.toStdString()); emit encKeyChanged(e); }

signals:
   void passwordChanged();
   void encTypeChanged(EncryptionType);
   void encKeyChanged(QString);
};


} //namespace wallet
}  //namespace bs


#endif // __BS_QPASSWORDDATA_H__
