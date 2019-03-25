#ifndef CHATUSERLISTLOGIC_H
#define CHATUSERLISTLOGIC_H

#include <QObject>

#include "ChatUserModel.h"

namespace spdlog {
   class logger;
}

class ChatClient;

class ChatUserListLogic : public QObject
{
   Q_OBJECT
public:
   using UserIdList = std::vector<std::string>;

   explicit ChatUserListLogic(QObject *parent = nullptr);

   void init(const std::shared_ptr<ChatClient> &client,
           const std::shared_ptr<spdlog::logger>& logger);

   void readUsersFromDB();

   ChatUserModelPtr chatUserModelPtr() const;

signals:

public slots:
   void onAddChatUsers(const UserIdList &userIdList);
   void onRemoveChatUsers(const UserIdList &userIdList);
   void onReplaceChatUsers(const UserIdList &userIdList);
   void onIncomingFriendRequest(const UserIdList &userIdList);
   void onAcceptFriendRequest(const UserIdList &userIdList);
   void onDeclineFriendRequest(const UserIdList &userIdList);
   void onUserHaveNewMessageChanged(const QString &userId, const bool &userHaveNewMessage, const bool &isInCurrentChat);
   
   void onAddChatRooms(const std::vector<std::shared_ptr<Chat::ChatRoomData> >& roomList);

private:
   ChatUserModelPtr chatUserModelPtr_;
   std::shared_ptr<ChatClient>      client_;
   std::shared_ptr<spdlog::logger>  logger_;
};

using ChatUserListLogicPtr = std::shared_ptr<ChatUserListLogic>;

#endif // CHATUSERLISTLOGIC_H
