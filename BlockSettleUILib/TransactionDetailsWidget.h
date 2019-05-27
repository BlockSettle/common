#ifndef __TRANSACTIONDETAILSWIDGET_H__
#define __TRANSACTIONDETAILSWIDGET_H__

#include "ArmoryObject.h"
#include "BinaryData.h"
#include "CCFileManager.h"
#include "TxClasses.h"

#include <QMap>
#include <QWidget>

// Important note: The concept of endianness doesn't really apply to transaction
// IDs (TXIDs). This is due to how SHA-256 works. Instead, Bitcoin Core uses an
// "internal" byte order for calculation purposes and a byte-flipped "RPC" order
// when responding to remote queries. In turn, virtually all block explorers use
// RPC order, and so does the terminal. However, Armory expects internal-ordered
// TXIDs. Always assume internal ordering with Armory calls! (All that said, the
// internal order is argably little endian since Core was originally written for
// x86. This, in turn, would make RPC order big endian.)

namespace spdlog {
   class logger;
}
namespace Ui {
   class TransactionDetailsWidget;
}
namespace bs {
   namespace sync {
      class WalletsManager;
   }
}
class CustomTreeWidget;
class QTreeWidgetItem;
class QTreeWidget;

// Create a class that takes a TXID and handles the data based on whether we're
// dealing with internal or RPC order. Not 100% robust for now (this assumes
// little endian is used) but it'll suffice until we require more robust data
// handling.
class BinaryTXID {
public:
   BinaryTXID(const bool isTXIDRPC = false) :
      txid_(), txidIsRPC_(isTXIDRPC) {}
   BinaryTXID(const BinaryData &txidData, const bool isTXIDRPC = false) :
      txid_(txidData), txidIsRPC_(isTXIDRPC) {}
   BinaryTXID(const BinaryDataRef &txidData, const bool isTXIDRPC = false) :
      txid_(txidData), txidIsRPC_(isTXIDRPC) {}
   BinaryTXID(const QByteArray &txidData, const bool isTXIDRPC = false) :
      txid_((uint8_t*)(txidData.data()), txidData.size()),
      txidIsRPC_(isTXIDRPC) {}
   BinaryTXID(const QString &txidData, const bool isTXIDRPC = false) :
      txid_(READHEX(txidData.toStdString())), txidIsRPC_(isTXIDRPC) {}
   BinaryTXID(const std::string &txidData, const bool isTXIDRPC = false) :
      txid_(READHEX(txidData.data())), txidIsRPC_(isTXIDRPC) {}

   // Make two separate functs just to make internal vs. RPC clearer to devs.
   BinaryData getRPCTXID();
   BinaryData getInternalTXID();

   bool getTXIDIsRPC() const { return txidIsRPC_; }
   const BinaryData& getRawTXID() const { return txid_; }
   bool operator==(const BinaryData& inTXID) const { return txid_ == inTXID; }
   bool operator!=(const BinaryData& inTXID) const { return txid_ != inTXID; }
   bool operator==(const BinaryTXID& inTXID) const;
   bool operator!=(const BinaryTXID& inTXID) const { return !((*this) == inTXID); }
   bool operator<(const BinaryTXID& inTXID) const;
   bool operator>(const BinaryTXID& inTXID) const;

private:
   BinaryData txid_;
   bool txidIsRPC_;
};

class TransactionDetailsWidget : public QWidget
{
   Q_OBJECT

public:
   explicit TransactionDetailsWidget(QWidget *parent = nullptr);
   ~TransactionDetailsWidget() override;

   void init(const std::shared_ptr<ArmoryObject> &
      , const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::sync::WalletsManager> &
      , const CCFileManager::CCSecurities &);

   void populateTransactionWidget(BinaryTXID rpcTXID,
      const bool& firstPass = true);

    enum TxTreeColumns {
      colAddressId,
      colAmount,
      colWallet
    };

signals:
   void addressClicked(QString addressId);
   void txHashClicked(QString txHash);
   void finished() const;

protected slots:
   void onAddressClicked(QTreeWidgetItem *item, int column);
   void onNewBlock(unsigned int);

protected:
   void loadTreeIn(CustomTreeWidget *tree);
   void loadTreeOut(CustomTreeWidget *tree);

private:
   void getHeaderData(const BinaryData& inHeader);
   void loadInputs();
   void setTxGUIValues();
   void clear();
   void updateCCInputs();

   void processTxData(Tx tx);

   void addItem(QTreeWidget *tree, const QString &address, const uint64_t amount
      , const QString &wallet, const BinaryData &txHash, const int txIndex = -1);

   static void updateTreeCC(QTreeWidget *, const bs::network::CCSecurityDef &);

private:
   std::unique_ptr<Ui::TransactionDetailsWidget>   ui_;
   std::shared_ptr<ArmoryObject>   armory_;
   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<bs::sync::WalletsManager> walletsMgr_;
   CCFileManager::CCSecurities      ccSecurities_;

   Tx curTx_; // The Tx being analyzed in the widget.

   // Data captured from Armory callbacks.
   std::map<BinaryTXID, Tx> prevTxMap_; // Prev Tx hash / Prev Tx map.

   QMap<QString, QTreeWidgetItem *> inputItems_;
   QMap<QString, QTreeWidgetItem *> outputItems_;
};

#endif // TRANSACTIONDETAILSWIDGET_H
