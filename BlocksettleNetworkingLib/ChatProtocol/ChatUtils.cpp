#include "ChatUtils.h"

#include "ChatCommonTypes.h"

QString ChatUtils::toString(Chat::OtcRangeType value)
{
   return QString::fromStdString(bs::network::OTCRangeID::toString(bs::network::OTCRangeID::Type(value)));
}

QString ChatUtils::toString(Chat::OtcSide value)
{
   return QString::fromStdString(bs::network::ChatOTCSide::toString(bs::network::ChatOTCSide::Type(value)));
}

bool ChatUtils::messageFlagRead(Chat::Data_Message *msg, Chat::Data_Message_State flag)
{
   return msg->state() & uint32_t(flag);
}

void ChatUtils::messageFlagSet(Chat::Data_Message *msg, Chat::Data_Message_State state)
{
   msg->set_state(uint32_t(state));
}
