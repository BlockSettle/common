#ifndef CHAT_UTILS_H
#define CHAT_UTILS_H

#include <QString>
#include "chat.pb.h"

class ChatUtils
{
public:
   static QString toString(Chat::OtcRangeType value);
   static QString toString(Chat::OtcSide value);

   static bool messageFlagRead(Chat::Data_Message *msg, Chat::Data_Message_State flag);
   static void messageFlagSet(Chat::Data_Message *msg, Chat::Data_Message_State state);
};

#endif
