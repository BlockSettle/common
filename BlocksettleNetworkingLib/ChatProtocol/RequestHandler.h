#pragma once

namespace Chat {
   
   class HeartbeatPingRequest;
   class LoginRequest;
   class LogoutRequest;
   class SendMessageRequest;
   class AskForPublicKeyRequest;
   class SendOwnPublicKeyRequest;
   class OnlineUsersRequest;
   class MessagesRequest;
   class MessageChangeStatusRequest;
   class ContactActionRequest;
   class ChatroomsListRequest;
   class PendingMessagesResponse;
   class SendRoomMessageRequest;
   
   class RequestHandler
   {
   public:
      virtual ~RequestHandler() = default;
      virtual void OnHeartbeatPing(const HeartbeatPingRequest& request) = 0;
      virtual void OnLogin(const LoginRequest &) = 0;
      virtual void OnLogout(const LogoutRequest &) = 0;
      virtual void OnSendMessage(const SendMessageRequest &) = 0;
   
      // Asking peer to send us their public key.
      virtual void OnAskForPublicKey(const AskForPublicKeyRequest &) = 0;
   
      // Sending our public key to the peer who asked for it.
      virtual void OnSendOwnPublicKey(const SendOwnPublicKeyRequest &) = 0;
   
      virtual void OnOnlineUsers(const OnlineUsersRequest &) = 0;
      virtual void OnRequestMessages(const MessagesRequest &) = 0;
      
      virtual void OnRequestChangeMessageStatus(const MessageChangeStatusRequest &) = 0;
      
      virtual void OnRequestContactsAction(const ContactActionRequest &) = 0;
      
      virtual void OnRequestChatroomsList(const ChatroomsListRequest &) = 0;
      
      virtual void OnSendRoomMessage(const SendRoomMessageRequest& ) = 0;
   };
}
