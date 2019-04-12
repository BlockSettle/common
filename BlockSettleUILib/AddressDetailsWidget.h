#ifndef __ADDRESSDETAILSWIDGET_H__
#define __ADDRESSDETAILSWIDGET_H__

#include "Address.h"
#include "ArmoryConnection.h"

#include <QWidget>
#include <QItemSelection>

namespace Ui {
   class AddressDetailsWidget;
}
namespace bs {
   namespace sync {
      class PlainWallet;
   }
}
class QTreeWidgetItem;

class AddressDetailsWidget : public QWidget
{
   Q_OBJECT

public:
   explicit AddressDetailsWidget(QWidget *parent = nullptr);
   ~AddressDetailsWidget() override;

   void init(const std::shared_ptr<ArmoryConnection> &armory
      , const std::shared_ptr<spdlog::logger> &inLogger
      , const std::shared_ptr<QTimer> &inTimer);
   void setQueryAddr(const bs::Address& inAddrVal);
   void clear();

   enum AddressTreeColumns {
      colDate = 0,
      colTxId = 1,
      colConfs = 2,
      colInputsNum,
      colOutputsNum,
      colOutputAmt,
      colFees,
      colFeePerByte,
      colTxSize
   };

signals:
   void transactionClicked(QString txId);

private slots:
   void onTxClicked(QTreeWidgetItem *item, int column);
   void OnRefresh(std::vector<BinaryData> ids, bool online);

private:
   void setConfirmationColor(QTreeWidgetItem *item);
   void setOutputColor(QTreeWidgetItem *item);
   void getTxData(const std::shared_ptr<AsyncClient::LedgerDelegate> &);
   void refresh(const std::shared_ptr<bs::sync::PlainWallet> &);
   void loadTransactions();

private:
   // NB: Right now, the code is slightly inefficient. There are two maps with
   // hashes for keys. One has transactions (Armory), and TXEntry objects (BS).
   // This is due to the manner in which we retrieve data from Armory. Pages are
   // returned for addresses, and we then retrieve the appropriate Tx objects
   // from Armory. (Tx searches go directly to Tx object retrieval.) The thing
   // is that the pages are what have data related to # of confs and other
   // block-related data. The Tx objects from Armory don't have block-related
   // data that we need. So, we need two maps, at least for now.
   //
   // In addition, note that the TX hashes returned by Armory are in "internal"
   // byte order, whereas the displayed values need to be in "RPC" byte order.
   // (Look at the BinaryTXID class comments for more info on this phenomenon.)
   // The only time we care about this is when displaying data to the user; the
   // data is consistent otherwise, which makes Armory happy. Don't worry about
   // about BinaryTXID. A simple endian flip in printed strings is all we need.

   std::unique_ptr<Ui::AddressDetailsWidget> ui_; // The main widget object.
   std::unordered_map<std::string, std::shared_ptr<bs::sync::PlainWallet>> dummyWallets_;
   std::map<BinaryData, Tx> txMap_; // A wallet's Tx hash / Tx map.
   std::map<BinaryData, bs::TXEntry> txEntryHashSet_; // A wallet's Tx hash / Tx entry map.
   std::shared_ptr<QTimer>             expTimer_;
   std::shared_ptr<ArmoryConnection>   armory_;
   std::shared_ptr<spdlog::logger>     logger_;
};

#endif // ADDRESSDETAILSWIDGET_H
