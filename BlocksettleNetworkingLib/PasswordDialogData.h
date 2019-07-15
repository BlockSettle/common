#ifndef __PASSWORD_DIALOG_DATA_H__
#define __PASSWORD_DIALOG_DATA_H__

#include <QObject>
#include <QVariantMap>
#include "headless.pb.h"

namespace bs {
namespace sync {

class PasswordDialogData : public QObject
{
   Q_OBJECT

public:
   PasswordDialogData(QObject *parent = nullptr) : QObject(parent) {}
   PasswordDialogData(const Blocksettle::Communication::Internal::PasswordDialogData &info, QObject *parent = nullptr);
   PasswordDialogData(const PasswordDialogData &src);

   Blocksettle::Communication::Internal::PasswordDialogData toProtobufMessage() const;

   QVariantMap values() const;
   void setValues(const QVariantMap &values);

   QVariant value(const QString &key) const;
   QVariant value(const char *key) const;

   void setValue(const QString &key, const QVariant &value);
   void setValue(const char *key, const QVariant &value);
   void setValue(const char *key, const char *value);

signals:
   void dataChanged();

private:
   QVariantMap values_;

//   // Details
//   QString productGroup_;
//   QString security_;
//   QString product_;
//   QString side_;
//   QString quantity_;
//   QString price_;
//   QString totalValue_;

//   // Settlement details
//   QString payment_;
//   QString genesisAddress_;

//   // XBT Settlement details
//   QString requesterAuthAddress_;
//   QString responderAuthAddress_;
//   QString wallet_;
//   QString transaction_;

//   // Transaction details
//   QString transactionAmount_;
//   QString networkFee_;
//   QString totalSpent_;
};


} // namespace sync
} // namespace bs
#endif // __SETTLEMENT_INFO_H__
