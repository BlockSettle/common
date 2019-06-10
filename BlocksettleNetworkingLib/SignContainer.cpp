#include "SignContainer.h"

#include "ApplicationSettings.h"
#include "ConnectionManager.h"

#include <QTcpSocket>
#include <spdlog/spdlog.h>

Q_DECLARE_METATYPE(std::shared_ptr<bs::sync::hd::Wallet>)


SignContainer::SignContainer(const std::shared_ptr<spdlog::logger> &logger, OpMode opMode)
   : logger_(logger), mode_(opMode)
{
   qRegisterMetaType<std::shared_ptr<bs::sync::hd::Wallet>>();
}


bool SignerConnectionExists(const QString &host, const QString &port)
{
   QTcpSocket sock;
   sock.connectToHost(host, port.toUInt());
   return sock.waitForConnected(30);
}

Blocksettle::Communication::Internal::SettlementInfo bs::sync::SettlementInfo::toProtobufMessage()
{
   Blocksettle::Communication::Internal::SettlementInfo info;
   info.set_productgroup(productGroup.toUtf8());
   info.set_security(security.toUtf8());
   info.set_product(product.toUtf8());
   info.set_side(side.toUtf8());
   info.set_quantity(quantity.toUtf8());
   info.set_price(price.toUtf8());
   info.set_totalvalue(totalValue.toUtf8());

   return info;
}
