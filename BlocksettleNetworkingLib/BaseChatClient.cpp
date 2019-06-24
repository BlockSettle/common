#include "BaseChatClient.h"

#include "ConnectionManager.h"
#include "UserHasher.h"
#include "Encryption/AEAD_Encryption.h"
#include "Encryption/AEAD_Decryption.h"
#include "Encryption/IES_Encryption.h"
#include "Encryption/IES_Decryption.h"
#include "Encryption/ChatSessionKeyData.h"

#include "ProtobufUtils.h"
#include <disable_warnings.h>
#include <botan/bigint.h>
#include <botan/base64.h>
#include <botan/auto_rng.h>
#include <enable_warnings.h>

#include "ChatProtocol/ChatUtils.h"

BaseChatClient::BaseChatClient(const std::shared_ptr<ConnectionManager>& connectionManager
                               , const std::shared_ptr<spdlog::logger>& logger
                               , const QString& dbFile)
   : logger_{logger}
   , connectionManager_{connectionManager}
{
   chatSessionKeyPtr_ = std::make_shared<Chat::ChatSessionKey>(logger);
   hasher_ = std::make_shared<UserHasher>();

   chatDb_ = make_unique<ChatDB>(logger, dbFile);

   bool loaded = false;
   setSavedKeys(chatDb_->loadKeys(&loaded));

   if (!loaded) {
      logger_->error("[BaseChatClient::BaseChatClient] failed to load saved keys");
   }
}

BaseChatClient::~BaseChatClient() noexcept
{}

void BaseChatClient::OnDataReceived(const std::string& data)
{
   auto response = std::make_shared<Chat::Response>();
   bool result = response->ParseFromString(data);
   if (!result) {
      logger_->error("[BaseChatClient::OnDataReceived] failed to parse message:\n{}", data);
      return;
   }

   logger_->debug("[BaseChatClient::{}] recv: \n{}", __func__, ProtobufUtils::toJson(*response));

   // Process on main thread because otherwise ChatDB could crash
   QMetaObject::invokeMethod(this, [this, response] {
      switch (response->data_case()) {
         case Chat::Response::kUsersList:
            OnUsersList(response->users_list());
            break;
         case Chat::Response::kMessages:
            OnMessages(response->messages());
            break;
         case Chat::Response::kAskForPublicKey:
            OnAskForPublicKey(response->ask_for_public_key());
            break;
         case Chat::Response::kSendOwnPublicKey:
            OnSendOwnPublicKey(response->send_own_public_key());
            break;
         case Chat::Response::kLogin:
            OnLoginReturned(response->login());
            break;
         case Chat::Response::kLogout:
            OnLogoutResponse(response->logout());
            break;
         case Chat::Response::kSendMessage:
            OnSendMessageResponse(response->send_message());
            break;
         case Chat::Response::kMessageChangeStatus:
            OnMessageChangeStatusResponse(response->message_change_status());
            break;
         case Chat::Response::kModifyContactsDirect:
            OnModifyContactsDirectResponse(response->modify_contacts_direct());
            break;
         case Chat::Response::kModifyContactsServer:
            OnModifyContactsServerResponse(response->modify_contacts_server());
            break;
         case Chat::Response::kContactsList:
            OnContactsListResponse(response->contacts_list());
            break;
         case Chat::Response::kChatroomsList:
            OnChatroomsList(response->chatrooms_list());
            break;
         case Chat::Response::kRoomMessages:
            OnRoomMessages(response->room_messages());
            break;
         case Chat::Response::kSearchUsers:
            OnSearchUsersResponse(response->search_users());
            break;
         case Chat::Response::kSessionPublicKey:
            OnSessionPublicKeyResponse(response->session_public_key());
            break;
         case Chat::Response::kReplySessionPublicKey:
            OnReplySessionPublicKeyResponse(response->reply_session_public_key());
            break;
         case Chat::Response::DATA_NOT_SET:
            logger_->error("Invalid empty or unknown response detected");
            break;
      }
   });
}

void BaseChatClient::OnConnected()
{
   logger_->debug("[BaseChatClient::OnConnected]");

   Chat::Request request;
   auto d = request.mutable_login();
   d->set_auth_id(currentUserId_);
   d->set_jwt(currentJwt_);
   d->set_pub_key(getOwnAuthPublicKey().toBinStr());
   sendRequest(request);
}

void BaseChatClient::OnError(DataConnectionError errorCode)
{
   logger_->debug("[BaseChatClient::OnError] {}", errorCode);
}

std::string BaseChatClient::LoginToServer(const std::string& email, const std::string& jwt
                                          , const ZmqBIP15XDataConnection::cbNewKey &cb)
{
   if (connection_) {
      logger_->error("[BaseChatClient::LoginToServer] connecting with not purged connection");
      return {};
   }

   currentUserId_ = hasher_->deriveKey(email);
   currentJwt_ = jwt;

   connection_ = connectionManager_->CreateZMQBIP15XDataConnection();
   connection_->setCBs(cb);

   if (!connection_->openConnection( getChatServerHost(), getChatServerPort(), this))
   {
      logger_->error("[BaseChatClient::LoginToServer] failed to open ZMQ data connection");
      connection_.reset();
   }

   return currentUserId_;
}

void BaseChatClient::LogoutFromServer()
{
   if (!connection_) {
      logger_->error("[BaseChatClient::LogoutFromServer] Disconnected already");
      return;
   }

   Chat::Request request;
   auto d = request.mutable_logout();
   d->set_auth_id(currentUserId_);
   sendRequest(request);

   cleanupConnection();
}

void BaseChatClient::OnDisconnected()
{
   logger_->debug("[BaseChatClient::OnDisconnected]");
   QMetaObject::invokeMethod(this, [this] {
      cleanupConnection();
   });
}

void BaseChatClient::OnLoginReturned(const Chat::Response_Login &response)
{
   if (response.success()) {
      OnLoginCompleted();
   }
   else {
      OnLogingFailed();
   }
}

void BaseChatClient::OnLogoutResponse(const Chat::Response_Logout & response)
{
   logger_->debug("[BaseChatClient::OnLogoutResponse]");
   QMetaObject::invokeMethod(this, [this] {
      cleanupConnection();
   });
}

void BaseChatClient::setSavedKeys(std::map<std::string, BinaryData>&& loadedKeys)
{
   std::swap(contactPublicKeys_, loadedKeys);
}

bool BaseChatClient::sendRequest(const Chat::Request& request)
{
   logger_->debug("[BaseChatClient::{}] send: \n{}", __func__, ProtobufUtils::toJson(request));

   if (!connection_->isActive()) {
      logger_->error("[BaseChatClient::sendRequest] Connection is not alive!");
      return false;
   }
   return connection_->send(request.SerializeAsString());
}


bool BaseChatClient::sendFriendRequestToServer(const std::string &friendUserId)
{
   Chat::Request request;
   auto d = request.mutable_modify_contacts_direct();
   d->set_sender_id(currentUserId_);
   d->set_receiver_id(friendUserId);
   d->set_action(Chat::CONTACTS_ACTION_REQUEST);
   d->set_sender_pub_key(getOwnAuthPublicKey().toBinStr());
   return sendRequest(request);
}

bool BaseChatClient::sendAcceptFriendRequestToServer(const std::string &friendUserId)
{
   {
      Chat::Request request;
      auto d = request.mutable_modify_contacts_direct();
      d->set_sender_id(currentUserId_);
      d->set_receiver_id(friendUserId);
      d->set_action(Chat::CONTACTS_ACTION_ACCEPT);
      d->set_sender_pub_key(getOwnAuthPublicKey().toBinStr());
      sendRequest(request);
   }

   {
      const BinaryData &publicKey = contactPublicKeys_[friendUserId];
      Chat::Request request;
      auto d = request.mutable_modify_contacts_server();
      d->set_sender_id(currentUserId_);
      d->set_contact_id(friendUserId);
      d->set_action(Chat::CONTACTS_ACTION_SERVER_ADD);
      d->set_status(Chat::CONTACT_STATUS_ACCEPTED);
      d->set_contact_pub_key(publicKey.toBinStr());
      sendRequest(request);
   }

   return true;
}

bool BaseChatClient::sendDeclientFriendRequestToServer(const std::string &friendUserId)
{
   {
      Chat::Request request;
      auto d = request.mutable_modify_contacts_direct();
      d->set_sender_id(currentUserId_);
      d->set_receiver_id(friendUserId);
      d->set_action(Chat::CONTACTS_ACTION_REJECT);
      d->set_sender_pub_key(getOwnAuthPublicKey().toBinStr());
      sendRequest(request);
   }

   {
      const BinaryData &publicKey = contactPublicKeys_[friendUserId];
      Chat::Request request;
      auto d = request.mutable_modify_contacts_server();
      d->set_sender_id(currentUserId_);
      d->set_contact_id(friendUserId);
      d->set_action(Chat::CONTACTS_ACTION_SERVER_ADD);
      d->set_status(Chat::CONTACT_STATUS_REJECTED);
      d->set_contact_pub_key(publicKey.toBinStr());
      sendRequest(request);
   }

   return true;
}

bool BaseChatClient::sendRemoveFriendToServer(const std::string &contactId)
{
   const BinaryData &publicKey = contactPublicKeys_[contactId];

   Chat::Request request;
   auto d = request.mutable_modify_contacts_server();
   d->set_sender_id(currentUserId_);
   d->set_contact_id(contactId);
   d->set_action(Chat::CONTACTS_ACTION_SERVER_REMOVE);
   d->set_status(Chat::CONTACT_STATUS_REJECTED);
   d->set_contact_pub_key(publicKey.toBinStr());
   return sendRequest(request);
}

bool BaseChatClient::sendUpdateMessageState(const std::shared_ptr<Chat::Data>& message)
{
   assert(message->has_message());

   Chat::Request request;
   auto d = request.mutable_message_change_status();
   d->set_message_id(message->message().id());
   d->set_state(message->message().state());
   return sendRequest(request);
}

bool BaseChatClient::sendSearchUsersRequest(const std::string &userIdPattern)
{
   Chat::Request request;
   auto d = request.mutable_search_users();
   d->set_sender_id(currentUserId_);
   d->set_search_id_pattern(userIdPattern);
   return sendRequest(request);
}

std::string BaseChatClient::deriveKey(const std::string &email) const
{
   return hasher_->deriveKey(email);
}


std::string BaseChatClient::getUserId() const
{
   return currentUserId_;
}

void BaseChatClient::cleanupConnection()
{
   chatSessionKeyPtr_->clearAll();
   currentUserId_.clear();
   currentJwt_.clear();
   connection_.reset();

   OnLogoutCompleted();
}

bool BaseChatClient::decodeAndUpdateIncomingSessionPublicKey(const std::string& senderId, const BinaryData& encodedPublicKey)
{
   BinaryData test(getOwnAuthPublicKey());

   // decrypt by ies received public key
   std::unique_ptr<Encryption::IES_Decryption> dec = Encryption::IES_Decryption::create(logger_);
   dec->setPrivateKey(getOwnAuthPrivateKey());

   dec->setData(encodedPublicKey.toBinStr());

   Botan::SecureVector<uint8_t> decodedData;
   try {
      dec->finish(decodedData);
   }
   catch (const std::exception &e) {
      logger_->error("[BaseChatClient::{}] Failed to decrypt public key by ies: {}", __func__, e.what());
      return false;
   }

   BinaryData remoteSessionPublicKey = BinaryData::CreateFromHex(QString::fromUtf8((char*)decodedData.data(), (int)decodedData.size()).toStdString());

   chatSessionKeyPtr_->generateLocalKeysForUser(senderId);
   chatSessionKeyPtr_->updateRemotePublicKeyForUser(senderId, remoteSessionPublicKey);

   return true;
}

void BaseChatClient::OnSendMessageResponse(const Chat::Response_SendMessage& response)
{
   if (response.accepted()) {
      if (!chatDb_->syncMessageId(response.client_message_id(), response.server_message_id())) {
         logger_->error("[BaseChatClient::OnSendMessageResponse] failed to update message id in DB from {} to {}"
                        , response.client_message_id(), response.server_message_id());
      }

      onMessageSent(response.receiver_id(), response.client_message_id(), response.server_message_id());
   }
}

void BaseChatClient::OnMessageChangeStatusResponse(const Chat::Response_MessageChangeStatus& response)
{
   const std::string &messageId = response.message_id();
   int newStatus = int(response.status());

   if (chatDb_->updateMessageStatus(messageId, newStatus)) {
      std::string chatId = response.sender_id() == currentUserId_
                           ? response.receiver_id()
                           : response.sender_id();

      onMessageStatusChanged(chatId, messageId, newStatus);
   } else {
      logger_->error("[BaseChatClient::OnMessageChangeStatusResponse] failed to update message state in DB: {} {}"
                     , response.message_id(), newStatus);
   }
}

void BaseChatClient::OnModifyContactsDirectResponse(const Chat::Response_ModifyContactsDirect& response)
{
   std::string actionString = "<unknown>";
   switch (response.action()) {
      case Chat::CONTACTS_ACTION_ACCEPT: {
         actionString = "ContactsAction::Accept";
         const std::string &senderId = response.sender_id();
         const auto pubKey = BinaryData(response.sender_public_key());
         onFriendRequestAccepted(senderId, pubKey);
         break;
      }
      case Chat::CONTACTS_ACTION_REJECT: {
         actionString = "ContactsAction::Reject";
         const std::string &senderId = response.sender_id();
         onFriendRequestRejected(senderId);
         break;
      }
      case Chat::CONTACTS_ACTION_REQUEST: {
         actionString = "ContactsAction::Request";
         const std::string &senderId = response.sender_id();
         const std::string &receiverId = response.receiver_id();
         const auto pubKey = BinaryData(response.sender_public_key());
         onFriendRequestReceived(receiverId,
                                 senderId,
                                 pubKey);
         break;
      }
      case Chat::CONTACTS_ACTION_REMOVE: {
         const std::string &senderId = response.sender_id();
         onFriendRequestedRemove(senderId);
         break;
      }
      default:
         break;
   }


   logger_->debug("[BaseChatClient::OnContactsActionResponseDirect]: Incoming contact action from {}: {}"
                  , response.sender_id(), Chat::ContactsAction_Name(response.action()));
}

void BaseChatClient::OnModifyContactsServerResponse(const Chat::Response_ModifyContactsServer & response)
{
   switch (response.requested_action()) {
      case Chat::CONTACTS_ACTION_SERVER_ADD:
         //addOrUpdateContact(QString::fromStdString(response.userId()));
         retrySendQueuedMessages(response.contact_id());
         break;
      case Chat::CONTACTS_ACTION_SERVER_REMOVE:
         if (response.success()) {
            onServerApprovedFriendRemoving(response.contact_id());
         }
         break;
      case Chat::CONTACTS_ACTION_SERVER_UPDATE:
         //addOrUpdateContact(QString::fromStdString(response.userId()), QStringLiteral(""), true);
         break;
      default:
         break;
   }
}

void BaseChatClient::OnContactsListResponse(const Chat::Response_ContactsList & response)
{
   std::vector<std::shared_ptr<Chat::Data>> newList;
   for (const auto& contact : response.contacts()) {
      if (!contact.has_contact_record()) {
         logger_->error("[BaseChatClient::{}] invalid response detected", __func__);
         continue;
      }

      contactPublicKeys_[contact.contact_record().contact_id()] = BinaryData(contact.contact_record().public_key());
      newList.push_back(std::make_shared<Chat::Data>(contact));
   }

   onContactListLoaded(newList);
}

void BaseChatClient::OnChatroomsList(const Chat::Response_ChatroomsList &response)
{
   std::vector<std::shared_ptr<Chat::Data>> newList;
   for (const auto& room : response.rooms()) {
      if (!room.has_room()) {
         logger_->error("[BaseChatClient::{}] invalid response detected", __func__);
         continue;
      }

      chatDb_->removeRoomMessages(room.room().id());
      newList.push_back(std::make_shared<Chat::Data>(room));
   }

   onRoomsLoaded(newList);
}

void BaseChatClient::OnRoomMessages(const Chat::Response_RoomMessages& response)
{
   for (const auto &msg : response.messages()) {
      if (!msg.has_message()) {
         logger_->error("[BaseChatClient::{}] invalid response detected", __func__);
         continue;
      }

      auto msgCopy = std::make_shared<Chat::Data>(msg);
      ChatUtils::messageFlagSet(msgCopy->mutable_message(), Chat::Data_Message_State_ACKNOWLEDGED);

      /*chatDb_->add(*msg);

      if (msg->encryptionType() == Chat::MessageData::EncryptionType::IES) {
         if (!msg->decrypt(ownPrivKey_)) {
            logger_->error("Failed to decrypt msg {}", msg->getId().toStdString());
            msg->setFlag(Chat::MessageData::State::Invalid);
         }
         else {
            msg->setEncryptionType(Chat::MessageData::EncryptionType::Unencrypted);
         }
      }*/


      onRoomMessageReceived(msgCopy);
   }
}

void BaseChatClient::OnSearchUsersResponse(const Chat::Response_SearchUsers & response)
{
   std::vector<std::shared_ptr<Chat::Data>> newList;
   for (const auto& user : response.users()) {
      if (!user.has_user()) {
         logger_->error("[BaseChatClient::{}] invalid response detected", __func__);
         continue;
      }
      newList.push_back(std::make_shared<Chat::Data>(user));
   }

   onSearchResult(newList);
}

void BaseChatClient::OnUsersList(const Chat::Response_UsersList& response)
{
   std::vector<std::string> usersList;
   for (auto& user : response.users()) {
      usersList.push_back(user);

      // if status changed clear session keys for contact
      chatSessionKeyPtr_->clearSessionForUser(user);
   }

   onUserListChanged(response.command(), usersList);
}

void BaseChatClient::OnMessages(const Chat::Response_Messages &response)
{
   std::vector<std::shared_ptr<Chat::Data>> messages;
   for (const auto &msg : response.messages()) {
      auto msgCopy = std::make_shared<Chat::Data>(msg);

      msgCopy->set_direction(Chat::Data_Direction_RECEIVED);

      if (!chatDb_->isContactExist(msg.message().sender_id())) {
         continue;
      }

      msgCopy->set_direction(Chat::Data_Direction_RECEIVED);
      ChatUtils::messageFlagSet(msgCopy->mutable_message(), Chat::Data_Message_State_ACKNOWLEDGED);

      switch (msgCopy->message().encryption()) {
         case Chat::Data_Message_Encryption_AEAD:
         {
            const std::string &senderId = msgCopy->message().sender_id();
            const auto& chatSessionKeyDataPtr = chatSessionKeyPtr_->findSessionForUser(senderId);

            if (!chatSessionKeyPtr_ || !chatSessionKeyPtr_->isExchangeForUserSucceeded(senderId)) {
               logger_->error("[BaseChatClient::OnMessages] Can't find public key for sender {}"
                              , senderId);
               ChatUtils::messageFlagSet(msgCopy->mutable_message(), Chat::Data_Message_State_INVALID);
            }
            else {
               BinaryData remotePublicKey(chatSessionKeyDataPtr->remotePublicKey());
               SecureBinaryData localPrivateKey(chatSessionKeyDataPtr->localPrivateKey());

               msgCopy = ChatUtils::decryptMessageAead(logger_, msgCopy->message(), remotePublicKey, localPrivateKey);
               if (!msgCopy) {
                  logger_->error("decrypt message failed");
                  continue;
               }
            }

            onDMMessageReceived(msgCopy);

            encryptByIESAndSaveMessageInDb(msgCopy);
         }
            break;

         case Chat::Data_Message_Encryption_IES:
         {
            logger_->error("[BaseChatClient::OnMessages] This could not happen! Failed to decrypt msg.");
            chatDb_->add(msgCopy);
            auto decMsg = decryptIESMessage(msgCopy);
            onDMMessageReceived(decMsg);
            break;
         }

         default:
            break;
      }
      sendUpdateMessageState(msgCopy);
   }
}

void BaseChatClient::OnAskForPublicKey(const Chat::Response_AskForPublicKey &response)
{
   logger_->debug("Received request to send own public key from server");

   // Make sure we are the node for which a public key was expected, if not, ignore this call.
   if (currentUserId_ != response.peer_id()) {
      return;
   }

   // Send our key to the peer.
   Chat::Request request;
   auto d = request.mutable_send_own_public_key();
   d->set_receiving_node_id(response.asking_node_id());
   d->set_sending_node_id(response.peer_id());
   d->set_sending_node_pub_key(getOwnAuthPublicKey().toBinStr());
   sendRequest(request);
}

void BaseChatClient::OnSendOwnPublicKey(const Chat::Response_SendOwnPublicKey &response)
{
   // Make sure we are the node for which a public key was expected, if not, ignore this call.
   if (currentUserId_ != response.receiving_node_id()) {
      return;
   }

   // Save received public key of peer.
   const auto &peerId = response.sending_node_id();
   contactPublicKeys_[peerId] = BinaryData(response.sending_node_pub_key());
   chatDb_->addKey(peerId, BinaryData(response.sending_node_pub_key()));

   retrySendQueuedMessages(response.sending_node_id());
}

bool BaseChatClient::getContacts(ContactRecordDataList &contactList)
{
   return chatDb_->getContacts(contactList);
}

bool BaseChatClient::addOrUpdateContact(const std::string &userId, Chat::ContactStatus status, const std::string &userName)
{
   Chat::Data contact;
   auto d = contact.mutable_contact_record();
   d->set_user_id(userId);
   d->set_contact_id(userId);
   d->set_status(status);
   d->set_display_name(userName);

   if (chatDb_->isContactExist(userId))
   {
      return chatDb_->updateContact(contact);
   }

   return chatDb_->addContact(contact);
}


bool BaseChatClient::removeContactFromDB(const std::string &userId)
{
   return chatDb_->removeContact(userId);
}

void BaseChatClient::OnSessionPublicKeyResponse(const Chat::Response_SessionPublicKey& response)
{
   // Do not use base64 after protobuf switch and send binary data as-is
   if (!decodeAndUpdateIncomingSessionPublicKey(response.sender_id(), response.sender_session_pub_key())) {
      logger_->error("[BaseChatClient::OnSessionPublicKeyResponse] Failed updating remote public key!");
      return;
   }

   // encode own session public key by ies and send as reply
   const auto& contactPublicKeyIterator = contactPublicKeys_.find(response.sender_id());
   if (contactPublicKeyIterator == contactPublicKeys_.end()) {
      // this should not happen
      logger_->error("[BaseChatClient::OnSessionPublicKeyResponse] Cannot find remote public key!");
      return;
   }

   BinaryData remotePublicKey(contactPublicKeyIterator->second);

   try {
      BinaryData encryptedLocalPublicKey = chatSessionKeyPtr_->iesEncryptLocalPublicKey(response.sender_id(), remotePublicKey);

      Chat::Request request;
      auto d = request.mutable_reply_session_public_key();
      d->set_sender_id(currentUserId_);
      d->set_receiver_id(response.sender_id());
      d->set_sender_session_pub_key(encryptedLocalPublicKey.toBinStr());
      sendRequest(request);
   }
   catch (std::exception& e) {
      logger_->error("[BaseChatClient::OnSessionPublicKeyResponse] Failed to encrypt msg by ies {}", e.what());
      return;
   }
}

void BaseChatClient::OnReplySessionPublicKeyResponse(const Chat::Response_ReplySessionPublicKey& response)
{
   if (!decodeAndUpdateIncomingSessionPublicKey(response.sender_id(), BinaryData(response.sender_session_pub_key()))) {
      logger_->error("[BaseChatClient::OnReplySessionPublicKeyResponse] Failed updating remote public key!");
      return;
   }

   retrySendQueuedMessages(response.sender_id());
}

std::shared_ptr<Chat::Data> BaseChatClient::sendMessageDataRequest(const std::shared_ptr<Chat::Data>& messageData
                                                                   , const std::string &receiver, bool isFromQueue)
{
   messageData->set_direction(Chat::Data_Direction_SENT);

   if (!isFromQueue) {
      if (!encryptByIESAndSaveMessageInDb(messageData))
      {
         logger_->error("[BaseChatClient::sendMessageDataRequest] failed to encrypt. discarding message");
         ChatUtils::messageFlagSet(messageData->mutable_message(), Chat::Data_Message_State_INVALID);
         return messageData;
      }

      onDMMessageReceived(messageData);
   }

   if (!chatDb_->isContactExist(receiver)) {
      //make friend request before sending direct message.
      //Enqueue the message to be sent, once our friend request accepted.
      enqueued_messages_[receiver].push(messageData);
      // we should not send friend request from here. this is user action
      // sendFriendRequest(receiver);
      return messageData;
   } else {
      // is contact rejected?
      Chat::Data_ContactRecord contact;
      chatDb_->getContact(messageData->message().receiver_id(), &contact);

      if (contact.status() == Chat::CONTACT_STATUS_REJECTED) {
         logger_->error("[BaseChatClient::sendMessageDataRequest] {}",
                        "Receiver has rejected state. Discarding message."
                        , receiver);
         ChatUtils::messageFlagSet(messageData->mutable_message(), Chat::Data_Message_State_INVALID);
         return messageData;
      }
   }

   const auto &contactPublicKeyIterator = contactPublicKeys_.find(receiver);
   if (contactPublicKeyIterator == contactPublicKeys_.end()) {
      // Ask for public key from peer. Enqueue the message to be sent, once we receive the
      // necessary public key.
      enqueued_messages_[receiver].push(messageData);

      // Send our key to the peer.
      Chat::Request request;
      auto d = request.mutable_ask_for_public_key();
      d->set_asking_node_id(currentUserId_);
      d->set_peer_id(receiver);
      sendRequest(request);

      return messageData;
   }

   switch (resolveMessageEncryption(messageData)) {
      case Chat::Data_Message_Encryption_AEAD: {
         auto msgEncrypted = encryptMessageToSendAEAD(receiver, contactPublicKeyIterator->second, messageData);
         if (msgEncrypted) {
            Chat::Request request;
            auto d = request.mutable_send_message();
            *d->mutable_message() = std::move(*msgEncrypted);
            sendRequest(request);
         } else {
            return messageData;
         }
         break;
      }
      case Chat::Data_Message_Encryption_IES:{
         auto msgEncrypted = encryptMessageToSendIES(contactPublicKeyIterator->second, messageData);
         if (msgEncrypted) {
            Chat::Request request;
            auto d = request.mutable_send_message();
            *d->mutable_message() = std::move(*msgEncrypted);
            sendRequest(request);
         } else {
            return messageData;
         }
         break;
      }
      default:{
         Chat::Request request;
         auto d = request.mutable_send_message();
         *d->mutable_message() = *messageData;
         sendRequest(request);
      }
   }

   return messageData;
}

void BaseChatClient::retrySendQueuedMessages(const std::string userId)
{
   // Run over enqueued messages if any, and try to send them all now.
   messages_queue& messages = enqueued_messages_[userId];

   while (!messages.empty()) {
      sendMessageDataRequest(messages.front(), userId, true);
      messages.pop();
   }
}

void BaseChatClient::eraseQueuedMessages(const std::string userId)
{
   enqueued_messages_.erase(userId);
}

bool BaseChatClient::encryptByIESAndSaveMessageInDb(const std::shared_ptr<Chat::Data>& message)
{
   auto msgEncrypted = ChatUtils::encryptMessageIes(logger_, message->message(), getOwnAuthPublicKey());

   if (!msgEncrypted) {
      logger_->error("[BaseChatClient::{}] failed to encrypt msg by ies", __func__);
      return false;
   }

   bool result = chatDb_->add(msgEncrypted);
   if (!result) {
      logger_->error("[BaseChatClient::{}] message store failed", __func__);
      return false;
   }

   return true;
}

std::shared_ptr<Chat::Data> BaseChatClient::encryptMessageToSendAEAD(const std::string &receiver, BinaryData &rpk, std::shared_ptr<Chat::Data> messageData)
{
   const auto& chatSessionKeyDataPtr = chatSessionKeyPtr_->findSessionForUser(receiver);
   if (chatSessionKeyDataPtr == nullptr || !chatSessionKeyPtr_->isExchangeForUserSucceeded(receiver)) {
      enqueued_messages_[receiver].push(messageData);

      chatSessionKeyPtr_->generateLocalKeysForUser(receiver);

      BinaryData remotePublicKey(rpk);
      logger_->debug("[BaseChatClient::encryptMessageToSendAEAD] USING PUBLIC KEY: {}", remotePublicKey.toHexStr());

      try {
         BinaryData encryptedLocalPublicKey = chatSessionKeyPtr_->iesEncryptLocalPublicKey(receiver, remotePublicKey);

//         std::string encryptedString = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(encryptedLocalPublicKey.getPtr()),
//                                                                      int(encryptedLocalPublicKey.getSize())).toBase64()).toStdString();

         Chat::Request request;
         auto d = request.mutable_session_public_key();
         d->set_sender_id(currentUserId_);
         d->set_receiver_id(receiver);
         d->set_sender_session_pub_key(encryptedLocalPublicKey.toBinStr());
         sendRequest(request);

         return nullptr;
      } catch (std::exception& e) {
         logger_->error("[ChatClient::sendMessageDataRequest] Failed to encrypt msg by ies {}", e.what());
         return nullptr;
      }
   }

   // search active message session for given user
   const auto userNoncesIterator = userNonces_.find(receiver);
   Botan::SecureVector<uint8_t> nonce;
   if (userNoncesIterator == userNonces_.end()) {
      // generate random nonce
      Botan::AutoSeeded_RNG rng;
      nonce = rng.random_vec(ChatUtils::defaultNonceSize());
      userNonces_.emplace_hint(userNoncesIterator, receiver, nonce);
   }
   else {
      // read nonce and increment
      Botan::BigInt bigIntNonce;
      bigIntNonce.binary_decode(userNoncesIterator->second);
      bigIntNonce++;
      nonce = Botan::BigInt::encode_locked(bigIntNonce);
      userNoncesIterator->second = nonce;
   }

   auto msgEncrypted = ChatUtils::encryptMessageAead(logger_, messageData->message()
                                                     , chatSessionKeyDataPtr->remotePublicKey(), chatSessionKeyDataPtr->localPrivateKey()
                                                     , BinaryData(nonce.data(), nonce.size()));

   if (!msgEncrypted) {
      logger_->error("[BaseChatClient::{}] can't encode data", __func__);
      ChatUtils::messageFlagSet(messageData->mutable_message(), Chat::Data_Message_State_INVALID);
      return nullptr;
   }

   return msgEncrypted;
}

std::shared_ptr<Chat::Data> BaseChatClient::encryptMessageToSendIES(BinaryData &rpk, std::shared_ptr<Chat::Data> messageData)
{
   auto msgEncrypted = ChatUtils::encryptMessageIes(logger_, messageData->message(), rpk);

   if (!msgEncrypted) {
      logger_->error("[BaseChatClient::{}] failed to encrypt msg by ies", __func__);
      ChatUtils::messageFlagSet(messageData->mutable_message(), Chat::Data_Message_State_INVALID);
      return nullptr;
   }

   return msgEncrypted;
}

std::shared_ptr<Chat::Data> BaseChatClient::decryptIESMessage(const std::shared_ptr<Chat::Data>& message)
{
   auto msgDecrypted = ChatUtils::decryptMessageIes(logger_, message->message(), getOwnAuthPrivateKey());
   if (!msgDecrypted) {
      logger_->error("[BaseChatClient::{}] Failed to decrypt msg from DB {}", __func__, message->message().id());
      ChatUtils::messageFlagSet(message->mutable_message(), Chat::Data_Message_State_INVALID);

      return message;
   }

   return msgDecrypted;
}

void BaseChatClient::onFriendRequestReceived(const std::string &userId, const std::string &contactId, BinaryData publicKey)
{
   contactPublicKeys_[contactId] = publicKey;
   chatDb_->addKey(contactId, publicKey);
   onFriendRequest(userId, contactId, publicKey);
}

void BaseChatClient::onFriendRequestAccepted(const std::string &contactId, BinaryData publicKey)
{
   contactPublicKeys_[contactId] = publicKey;
   chatDb_->addKey(contactId, publicKey);

   onContactAccepted(contactId);

   addOrUpdateContact(contactId, Chat::CONTACT_STATUS_ACCEPTED);

   Chat::Request request;
   auto d = request.mutable_modify_contacts_server();
   d->set_sender_id(currentUserId_);
   d->set_contact_id(contactId);
   d->set_action(Chat::CONTACTS_ACTION_SERVER_ADD);
   d->set_status(Chat::CONTACT_STATUS_ACCEPTED);
   d->set_contact_pub_key(publicKey.toBinStr());
   sendRequest(request);

   // reprocess message again
   retrySendQueuedMessages(contactId);
}

void BaseChatClient::onFriendRequestRejected(const std::string &contactId)
{
   addOrUpdateContact(contactId, Chat::CONTACT_STATUS_REJECTED);

   onContactRejected(contactId);

   Chat::Request request;
   auto d = request.mutable_modify_contacts_server();
   d->set_sender_id(currentUserId_);
   d->set_contact_id(contactId);
   d->set_action(Chat::CONTACTS_ACTION_SERVER_ADD);
   d->set_status(Chat::CONTACT_STATUS_REJECTED);
   //d->set_contact_pub_key(response.sender_public_key());
   sendRequest(request);

   //removeContact(QString::fromStdString(response.senderId()));
   eraseQueuedMessages(contactId);

}

void BaseChatClient::onFriendRequestedRemove(const std::string &contactId)
{
   eraseQueuedMessages(contactId);
   sendRemoveFriendToServer(contactId);
}

void BaseChatClient::onServerApprovedFriendRemoving(const std::string &contactId)
{
   chatDb_->removeContact(contactId);
   //TODO: Remove pub key
   onContactRemove(contactId);
}
