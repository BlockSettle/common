
#ifndef COINCONTROLVIEW_H_INCLUDED
#define COINCONTROLVIEW_H_INCLUDED

#include <QTreeView>

#include <map>


class CoinControlModel;


//
// CoinControlView
//

//! View for coin control widget.
class CoinControlView : public QTreeView
{
   Q_OBJECT

public:
   explicit CoinControlView(QWidget *parent);
   ~CoinControlView() noexcept override = default;

   void setCoinsModel(CoinControlModel *model);

protected:
   void resizeEvent(QResizeEvent *e) override;
   void drawRow(QPainter *painter, const QStyleOptionViewItem &option,
      const QModelIndex &index) const override;
   void paintEvent(QPaintEvent *e) override;

private:
   int visibleRow(const QPoint &p) const;

private slots:
   void calcCountOfVisibleRows();
   void calcCountOfVisibleRows(const QModelIndex &parent, int &row);
   void onRowsInserted(const QModelIndex &parent, int first, int last);
   void onRowsRemoved(const QModelIndex &parent, int first, int last);
   void onCollapsed(const QModelIndex &index);
   void onExpanded(const QModelIndex &index);

private:
   std::map<QPersistentModelIndex, int> visible_;
   CoinControlModel *model_;
   mutable int currentPainted_;
}; // class CoinControlView

#endif // COINCONTROLVIEW_H_INCLUDED
