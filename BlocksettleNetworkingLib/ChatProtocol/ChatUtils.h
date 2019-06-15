#ifndef CHAT_UTILS_H
#define CHAT_UTILS_H

#include <QString>
#include <QMetaType>
#include "chat.pb.h"
#include "ChatCommonTypes.h"

class ChatUtils
{
public:
   static QString toString(Chat::OtcRangeType value);
   static QString toString(Chat::OtcSide value);

   static bool messageFlagRead(const Chat::Data_Message &msg, Chat::Data_Message_State flag);
   static void messageFlagSet(Chat::Data_Message *msg, Chat::Data_Message_State state);

   static void registerTypes();

   static size_t defaultNonceSize();

   static const char*GlobalRoomKey;
   static const char* OtcRoomKey;

   static Chat::ResponseType responseType(const Chat::Response *response);
};

Q_DECLARE_METATYPE(std::shared_ptr<Chat::Data>)
Q_DECLARE_METATYPE(std::vector<std::shared_ptr<Chat::Data>>)
Q_DECLARE_METATYPE(Chat::ContactStatus)
Q_DECLARE_METATYPE(Chat::UserStatus)

#endif
