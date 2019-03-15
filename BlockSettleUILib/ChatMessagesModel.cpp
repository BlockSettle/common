#include "ChatMessagesModel.h"

ChatMessagesModel::ChatMessagesModel(QObject *parent) : ChatMessagesModel(nullptr, parent)
{
   
}

ChatMessagesModel::ChatMessagesModel(std::shared_ptr<ChatClient> client, QObject* parent)
:QAbstractItemModel (parent), client_(client)
{
    connect(client_.get(), &ChatClient::MessagesUpdate, this, &ChatMessagesModel::onMessagesUpdate);
    connect(client_.get(), &ChatClient::NewMessage, this, &ChatMessagesModel::onNewMessage);
    connect(client_.get(), &ChatClient::ChatChanged, this, &ChatMessagesModel::onChatChanged);
    connect(client_.get(), &ChatClient::LoginSuccess, this, &ChatMessagesModel::onLoginChanged);
    connect(client_.get(), &ChatClient::MessageIdUpdated, this, &ChatMessagesModel::onMessageIdUpdate);
    connect(client_.get(), &ChatClient::MessageStatusUpdated, this, &ChatMessagesModel::onMessageStatusUpdate);
    
    
}

void ChatMessagesModel::switchChat(const QString& chatId)
{
   onChatChanged(chatId);
}



QModelIndex ChatMessagesModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)){
        return QModelIndex();
    }
    
    if (!parent.isValid()){
        return createIndex(row, column);
    }
    
       return QModelIndex();
        
}

QModelIndex ChatMessagesModel::parent(const QModelIndex& child) const
{
    return QModelIndex();
}

int ChatMessagesModel::rowCount(const QModelIndex& parent) const
{
   if (currentChat_.isNull() || currentChat_.isEmpty())
       return 0;
   return messages_.value(currentChat_).size();
}

int ChatMessagesModel::columnCount(const QModelIndex& parent) const
{
    return static_cast<int>(Column::last);
}

QVariant ChatMessagesModel::data(const QModelIndex& index, int role) const
{
   if (!index.isValid()) {
      return QVariant();
   }
   
   if (role != Qt::DisplayRole) {
      return QVariant();
   }
   std::shared_ptr<Chat::MessageData> message = messages_.value(currentChat_).at(index.row());
   
   switch (static_cast<Column>(index.column())) {
       case Column::Time:
           return message->getDateTime();
       case Column::User:
           return displayUser(message);
       case Column::Status:
           return message->getState();
       case Column::Message:
           return  QObject::tr("%1 | %2 | %3 |%4| %5")
                 .arg(message->getDateTime().toString(QString::fromUtf8("MM/dd/yy hh:mm:ss")))
                 .arg(displayUser(message))
                 .arg((int)message->getState())
                 .arg(message->getId())
                 .arg(message->getMessageData());
   }
   return QVariant();
}

QString ChatMessagesModel::displayUser(std::shared_ptr<Chat::MessageData> message) const
{
   QString own = QLatin1Literal("You         ");
   QString sender = message->getSenderId();
   if (sender == currentUser_){
      sender = own;
   } 
   return sender;
}

void ChatMessagesModel::onMessagesUpdate(const std::vector<std::shared_ptr<Chat::MessageData> >& messages)
{
   emit layoutAboutToBeChanged();
   
   for (const auto& message : messages){
      if (!messages.empty()){
         QString chatId;
         if (message->getReceiverId() == currentUser_){
            chatId = message->getSenderId();
         } else {
            chatId = message->getReceiverId();
         }
         
         auto it = std::find_if(messages_[chatId].begin(), messages_[chatId].end(),
                                [message](const std::shared_ptr<Chat::MessageData> first)->bool
                                 {
                                    return first->getId() == message->getId();
                                 });
         
         if (it == messages_[chatId].end()){
            messages_[chatId].push_back(message);
         }
      }
   }
   
   emit layoutChanged();
}

void ChatMessagesModel::onNewMessage(const std::shared_ptr<Chat::MessageData>& message)
{
   emit layoutAboutToBeChanged();
   if (message->getReceiverId() == currentUser_){
       messages_[message->getSenderId()].push_back(message);
   } else {
       messages_[message->getReceiverId()].push_back(message);
       
   }
   emit layoutChanged();
}

void ChatMessagesModel::onChatChanged(const QString& chatId)
{
   emit layoutAboutToBeChanged();
   currentChat_ = chatId;
   emit layoutChanged();
}

void ChatMessagesModel::onLoginChanged(const QString& userId)
{
   emit layoutAboutToBeChanged();
   currentUser_ = userId;
   emit layoutChanged();
}


void ChatMessagesModel::onMessageIdUpdate(const QString& messageId, const QString& newMessageId, const QString& chatId)
{
   if (messages_.contains(chatId)){
      if (chatId == currentChat_){
         emit layoutAboutToBeChanged();
      }
      
      auto it = std::find_if(messages_[chatId].begin(), messages_[chatId].end(),
                             [messageId](const std::shared_ptr<Chat::MessageData> first)->bool
                              {
                                 return first->getId() == messageId;
                              });
      if (it != messages_[chatId].end()){
         (*it)->setId(newMessageId);
         (*it)->setFlag(Chat::MessageData::State::Sent);
         
      }
      if (chatId == currentChat_){
         emit layoutChanged();
      }
   }
}

void ChatMessagesModel::onMessageStatusUpdate(const QString& messageId, const QString& chatId, int newStatus)
{
   if (messages_.contains(chatId)){
      if (chatId == currentChat_){
         emit layoutAboutToBeChanged();
      }
      
      auto it = std::find_if(messages_[chatId].begin(), messages_[chatId].end(),
                             [messageId](const std::shared_ptr<Chat::MessageData> first)->bool
                              {
                                 return first->getId() == messageId;
                              });
      if (it != messages_[chatId].end()){
         (*it)->updateState(newStatus);
      }
      if (chatId == currentChat_){
         emit layoutChanged();
      }
   }
}
