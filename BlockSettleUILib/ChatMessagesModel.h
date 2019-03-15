#ifndef CHATMESSAGESMODEL_H
#define CHATMESSAGESMODEL_H

#include <QAbstractItemModel>
#include <QMap>
#include "ChatClient.h"

using MessagesHistory = std::vector<std::shared_ptr<Chat::MessageData>>;
class ChatMessagesModel : public QAbstractItemModel
{
   Q_OBJECT
public:
   enum class Column {
      Time,
      Status,
      User,
      Message,
      last
   };
   explicit ChatMessagesModel(QObject *parent = nullptr);
   explicit ChatMessagesModel(std::shared_ptr<ChatClient> client, QObject *parent = nullptr);
   
signals:
   
public slots:
   void switchChat(const QString& chatId);
   void onMessagesUpdate(const std::vector<std::shared_ptr<Chat::MessageData>> & messages);
   void onNewMessage(const std::shared_ptr<Chat::MessageData> & message);
   void onChatChanged(const QString & chatId);
   void onLoginChanged(const QString & userId);
   void onMessageIdUpdate(const QString & messageId,const QString & newMessageId, const QString & chatId);
   void onMessageStatusUpdate(const QString & messageId,const QString & chatId, int newStatus);
private:
    std::shared_ptr<ChatClient> client_;
    
    // QAbstractItemModel interface
public:
    QModelIndex index(int row, int column, const QModelIndex& parent) const;
    QModelIndex parent(const QModelIndex& child) const;
    int rowCount(const QModelIndex& parent) const;
    int columnCount(const QModelIndex& parent) const;
    QVariant data(const QModelIndex& index, int role) const;
private:
    QString currentUser_;
    QString currentChat_;
    QMap<QString, MessagesHistory> messages_;
private:
    QString displayUser(std::shared_ptr<Chat::MessageData> message) const;
};

#endif // CHATMESSAGESMODEL_H
