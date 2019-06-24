#ifndef CHAT_UTILS_H
#define CHAT_UTILS_H

#include <spdlog/spdlog.h>
#include <QString>
#include <QMetaType>
#include "chat.pb.h"
#include "ChatCommonTypes.h"
#include "BinaryData.h"
#include "SecureBinaryData.h"

class BinaryData;

class ChatUtils
{
public:
   static const char *GlobalRoomKey;
   static const char *OtcRoomKey;
   static const char *SupportRoomKey;

   static QString toString(Chat::OtcRangeType value);
   static QString toString(Chat::OtcSide value);

   static bool messageFlagRead(const Chat::Data_Message &msg, Chat::Data_Message_State flag);
   static void messageFlagSet(Chat::Data_Message *msg, Chat::Data_Message_State state);

   static void registerTypes();

   static size_t defaultNonceSize();

   static std::shared_ptr<Chat::Data> encryptMessageIes(const std::shared_ptr<spdlog::logger> &logger
      , const Chat::Data_Message &msg, const BinaryData &pubKey);
   static std::shared_ptr<Chat::Data> encryptMessageAead(const std::shared_ptr<spdlog::logger> &logger
      , const Chat::Data_Message &msg, const BinaryData &remotePubKey, const SecureBinaryData &privKey
      , const BinaryData &nonce);

   static std::shared_ptr<Chat::Data> decryptMessageIes(const std::shared_ptr<spdlog::logger> &logger
      , const Chat::Data_Message &msg, const SecureBinaryData &privKey);
   static std::shared_ptr<Chat::Data> decryptMessageAead(const std::shared_ptr<spdlog::logger> &logger
      , const Chat::Data_Message &msg, const BinaryData &remotePubKey, const SecureBinaryData &privKey);
};

Q_DECLARE_METATYPE(std::shared_ptr<Chat::Data>)
Q_DECLARE_METATYPE(std::vector<std::shared_ptr<Chat::Data>>)
Q_DECLARE_METATYPE(Chat::ContactStatus)
Q_DECLARE_METATYPE(Chat::UserStatus)

#endif
