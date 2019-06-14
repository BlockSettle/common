#ifndef ResponseHandler_h__
#define ResponseHandler_h__

namespace Chat {
   class Response_AskForPublicKey;
   class Response_ChatroomsList;
   class Response_ContactsActionDirect;
   class Response_ContactsActionServer;
   class Response_ContactsList;
   class Response_Login;
   class Response_Logout;
   class Response_MessageChangeStatus;
   class Response_Messages;
   class Response_ReplySessionPublicKey;
   class Response_RoomMessages;
   class Response_SearchUsers;
   class Response_SendMessage;
   class Response_SendOwnPublicKey;
   class Response_SendRoomMessage;
   class Response_SessionPublicKey;
   class Response_UsersList;

   class ResponseHandler
   {
   public:
      virtual ~ResponseHandler() = default;
      virtual void OnUsersList(const Response_UsersList &) = 0;
      virtual void OnMessages(const Response_Messages &) = 0;

      // Received a call from a peer to send our public key.
      virtual void OnAskForPublicKey(const Response_AskForPublicKey &) = 0;

      // Received public key of one of our peers.
      virtual void OnSendOwnPublicKey(const Response_SendOwnPublicKey &) = 0;

      virtual void OnLoginReturned(const Response_Login &) = 0;
      virtual void OnLogoutResponse(const Response_Logout &) = 0;

      virtual void OnSendMessageResponse(const Response_SendMessage&) = 0;
      virtual void OnMessageChangeStatusResponse(const Response_MessageChangeStatus&) = 0;
      virtual void OnContactsActionResponseDirect(const Response_ContactsActionDirect&) = 0;
      virtual void OnContactsActionResponseServer(const Response_ContactsActionServer&) = 0;
      virtual void OnContactsListResponse(const Response_ContactsList&) = 0;

      virtual void OnChatroomsList(const Response_ChatroomsList&) = 0;
      virtual void OnRoomMessages(const Response_RoomMessages&) = 0;

      virtual void OnSearchUsersResponse(const Response_SearchUsers&) = 0;

      virtual void OnSessionPublicKeyResponse(const Response_SessionPublicKey&) = 0;
      virtual void OnReplySessionPublicKeyResponse(const Response_ReplySessionPublicKey&) = 0;
   };
}

#endif // ResponseHandler_h__
