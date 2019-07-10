#include "PasswordDialogData.h"

#include <QDataStream>

bs::sync::PasswordDialogData::PasswordDialogData(const Blocksettle::Communication::Internal::PasswordDialogData &info, QObject *parent)
   : QObject (parent)
{
   QByteArray ba;
   QDataStream stream(&ba, QIODevice::ReadOnly);
   stream >> values_;
}

bs::sync::PasswordDialogData::PasswordDialogData(const bs::sync::PasswordDialogData &src)
{
   setParent(src.parent());
   values_ = src.values_;
}

Blocksettle::Communication::Internal::PasswordDialogData bs::sync::PasswordDialogData::toProtobufMessage() const
{
   QByteArray ba;
   QDataStream stream(&ba, QIODevice::WriteOnly);
   stream << values_;

   Blocksettle::Communication::Internal::PasswordDialogData info;
   info.set_valuesmap(ba);

   return info;
}

QVariantMap bs::sync::PasswordDialogData::values() const
{
   return values_;
}

void bs::sync::PasswordDialogData::setValues(const QVariantMap &values)
{
   values_ = values;
   emit dataChanged();
}

void bs::sync::PasswordDialogData::setValue(const QString &key, const QVariant &value)
{
   values_.insert(key, value);
   emit dataChanged();
}

void bs::sync::PasswordDialogData::setValue(const char *key, const QVariant &value)
{
   setValue(QString::fromLatin1(key), value);
}

void bs::sync::PasswordDialogData::setValue(const char *key, const char *value)
{
   setValue(key, QString::fromLatin1(value));
}
