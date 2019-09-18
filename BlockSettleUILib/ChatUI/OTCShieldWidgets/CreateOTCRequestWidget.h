#ifndef __CREATE_OTC_REQUEST_WIDGET_H__
#define __CREATE_OTC_REQUEST_WIDGET_H__

#include <QWidget>
#include <memory>

#include "OtcTypes.h"
#include "OTCWindowsAdapterBase.h"

namespace Ui {
   class CreateOTCRequestWidget;
};

class CreateOTCRequestWidget : public OTCWindowsAdapterBase
{
   Q_OBJECT

public:
   CreateOTCRequestWidget(QWidget* parent = nullptr);
   ~CreateOTCRequestWidget() override;

   void init(bs::network::otc::Env env);

   bs::network::otc::QuoteRequest request() const;

signals:
   void requestCreated();

private slots:
   void onSellClicked();
   void onBuyClicked();

private:
   std::unique_ptr<Ui::CreateOTCRequestWidget> ui_;
};

#endif // __CREATE_OTC_REQUEST_WIDGET_H__
