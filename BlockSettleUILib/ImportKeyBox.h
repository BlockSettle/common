#ifndef __IMPORT_KEY_BOX_H__
#define __IMPORT_KEY_BOX_H__

#include <QDialog>
#include <QVariant>
#include "BSMessageBox.h"
#include "BinaryData.h"
#include <memory>

namespace Ui
{
   class ImportKeyBox;
};

class ImportKeyBox : public QDialog
{
Q_OBJECT

public:
   ImportKeyBox(BSMessageBox::Type mbType, const QString& title
      , QWidget* parent = nullptr);
   ~ImportKeyBox() override;

   void setConfirmButtonText(const QString &text);
   void setCancelButtonText(const QString &text);
   void setLabelTextFormat(Qt::TextFormat tf);

   void showEvent(QShowEvent *) override;

   void setOkVisible(bool visible);
   void setCancelVisible(bool visible);

   void setAddrPort(const std::string &srvAddrPort);

   void setAddress(const QString &address);
   void setPort(const QString &port);
   void setNewKey(const QString &newKey);
   void setOldKey(const QString &oldKey);

   void setNewKey(const std::string &newKey) { setNewKey(QString::fromStdString(newKey)); }
   void setOldKey(const std::string &oldKey) { setOldKey(QString::fromStdString(oldKey)); }

   void setNewKeyFromBinary(const BinaryData &binaryKey) { setNewKey(QString::fromLatin1(QByteArray::fromStdString(binaryKey.toBinStr()).toHex())); }
   void setOldKeyFromBinary(const BinaryData &binaryKey) { setOldKey(QString::fromLatin1(QByteArray::fromStdString(binaryKey.toBinStr()).toHex())); }

protected slots:

private:
   void setType(BSMessageBox::Type type);


private:
   std::unique_ptr<Ui::ImportKeyBox> ui_;
};
#endif // __IMPORT_KEY_BOX_H__
