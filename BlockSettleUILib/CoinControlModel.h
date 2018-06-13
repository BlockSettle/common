#ifndef __COIN_CONTROL_MODEL_H__
#define __COIN_CONTROL_MODEL_H__

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>

#include "WalletsManager.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CoinControlNode;
class SelectedTransactionInputs;

struct UTXO;

class CoinControlModel : public QAbstractItemModel
{
Q_OBJECT
private:
   enum Column
   {
	  ColumnName,
	  ColumnComment,
	  ColumnBalance,
	  ColumnEmpty,
	  ColumnsCount
   };

public:
   CoinControlModel(const std::shared_ptr<SelectedTransactionInputs>& selectedInputs, QObject* parent = nullptr);
   ~CoinControlModel() override = default;

   int columnCount(const QModelIndex & parent = QModelIndex()) const override;
   int rowCount(const QModelIndex & parent = QModelIndex()) const override;

   Qt::ItemFlags flags(const QModelIndex & index) const override;
   QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

   QVariant data(const QModelIndex& index, int role) const override;
   bool setData(const QModelIndex & index, const QVariant & value, int role) override;

   QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const override;

   QModelIndex parent(const QModelIndex& child) const override;
   bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;

   size_t GetSelectedTransactionsCount() const;
   QString GetSelectedBalance() const;
   QString GetTotalBalance() const;

   void ApplySelection(const std::shared_ptr<SelectedTransactionInputs>& selectedInputs);

   void clearSelection();

   void sort(int column, Qt::SortOrder order  = Qt::AscendingOrder ) override;

signals:
   void selectionChanged();

public slots:
   void selectAll(int sel);
	CoinControlNode* getNodeByIndex(const QModelIndex& index) const;
private:


   void loadInputs(const std::shared_ptr<SelectedTransactionInputs> &selectedInputs);

private:
   std::shared_ptr<CoinControlNode> root_, cpfp_;
   WalletsManager::wallet_gen_type  wallet_;
   std::unordered_map<std::string, CoinControlNode*> addressNodes_, cpfpNodes_;
};




#endif // __COIN_CONTROL_MODEL_H__
