#ifndef __OTC_NEGOTIATION_REQUEST_WIDGET_H__
#define __OTC_NEGOTIATION_REQUEST_WIDGET_H__

#include <QWidget>
#include <memory>

namespace Ui {
   class OTCNegotiationCommonWidget;
};

class OTCNegotiationRequestWidget : public QWidget
{
Q_OBJECT
Q_DISABLE_COPY(OTCNegotiationRequestWidget)

public:
   OTCNegotiationRequestWidget(QWidget* parent = nullptr);
   ~OTCNegotiationRequestWidget() override;

private:
   std::unique_ptr<Ui::OTCNegotiationCommonWidget> ui_;
};

#endif // __OTC_NEGOTIATION_REQUEST_WIDGET_H__
