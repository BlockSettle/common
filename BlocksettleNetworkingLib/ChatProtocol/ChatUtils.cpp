#include "ChatUtils.h"

#include "ChatCommonTypes.h"

const char *ChatUtils::GlobalRoomKey = "global_chat";
const char *ChatUtils::OtcRoomKey = "otc_chat";

QString ChatUtils::toString(Chat::OtcRangeType value)
{
   return QString::fromStdString(bs::network::OTCRangeID::toString(bs::network::OTCRangeID::Type(value)));
}

QString ChatUtils::toString(Chat::OtcSide value)
{
   return QString::fromStdString(bs::network::ChatOTCSide::toString(bs::network::ChatOTCSide::Type(value)));
}

bool ChatUtils::messageFlagRead(const Chat::Data_Message &msg, Chat::Data_Message_State flag)
{
   return msg.state() & uint32_t(flag);
}

void ChatUtils::messageFlagSet(Chat::Data_Message *msg, Chat::Data_Message_State state)
{
   msg->set_state(msg->state() | uint32_t(state));
}

void ChatUtils::registerTypes()
{
   qRegisterMetaType<std::shared_ptr<Chat::Data>>();
   qRegisterMetaType<std::vector<std::shared_ptr<Chat::Data>>>();
   qRegisterMetaType<Chat::ContactStatus>();
   qRegisterMetaType<Chat::UserStatus>();
}

size_t ChatUtils::defaultNonceSize()
{
   return 24;
}
