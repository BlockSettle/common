#ifndef __SUBSCRIBER_CONNECTION_H__
#define __SUBSCRIBER_CONNECTION_H__

#include <QObject>

#include "ZmqContext.h"

#include <atomic>
#include <thread>
#include <functional>

class SubscriberConnectionListener : public QObject
{
   Q_OBJECT
public:
   SubscriberConnectionListener() = default;
   virtual ~SubscriberConnectionListener() noexcept = default;

   SubscriberConnectionListener(const SubscriberConnectionListener&) = delete;
   SubscriberConnectionListener& operator = (const SubscriberConnectionListener&) = delete;

   SubscriberConnectionListener(SubscriberConnectionListener&&) = delete;
   SubscriberConnectionListener& operator = (SubscriberConnectionListener&&) = delete;

public slots:
   virtual void OnDataReceived(std::string data) = 0;
   virtual void OnConnected() = 0;
   virtual void OnDisconnected() = 0;
};

class SubscriberConnectionListenerCB : public SubscriberConnectionListener
{
public:
   using connectedCB = std::function<void()>;
   using disconnectedCB = std::function<void()>;
   using dataReceivedCB = std::function<void(const std::string& data)>;

public:
   SubscriberConnectionListenerCB(const dataReceivedCB& onDataReceived
      , const connectedCB& onConnected
      , const disconnectedCB& onDisconnected);
   ~SubscriberConnectionListenerCB() noexcept override = default;

   SubscriberConnectionListenerCB(const SubscriberConnectionListenerCB&) = delete;
   SubscriberConnectionListenerCB& operator = (const SubscriberConnectionListenerCB&) = delete;

   SubscriberConnectionListenerCB(SubscriberConnectionListenerCB&&) = delete;
   SubscriberConnectionListenerCB& operator = (SubscriberConnectionListenerCB&&) = delete;

   void OnDataReceived(std::string data) override;
   void OnConnected() override;
   void OnDisconnected() override;

private:
   dataReceivedCB onDataReceived_;
   connectedCB    onConnected_;
   disconnectedCB onDisconnected_;
};

class SubscriberConnection : public QObject
{
   Q_OBJECT
public:
   SubscriberConnection(
      const std::shared_ptr<spdlog::logger>& logger, 
      const std::shared_ptr<ZmqContext>& context,
      QObject *parent = nullptr);

   ~SubscriberConnection() noexcept;

   SubscriberConnection(const SubscriberConnection&) = delete;
   SubscriberConnection& operator = (const SubscriberConnection&) = delete;

   SubscriberConnection(SubscriberConnection&&) = delete;
   SubscriberConnection& operator = (SubscriberConnection&&) = delete;

   bool ConnectToPublisher(const std::string& host, const std::string& port, SubscriberConnectionListener* listener);
   bool ConnectToPublisher(const std::string& endpoint, SubscriberConnectionListener* listener);

   void stopListen();

signals:
   void dataReceived(std::string data);
   void connected();
   void disconnected();
   void stopped();

private:
   void listenFunction(const std::string& endpoint, const std::string& controlEndpoint);
   void initZmqSockets(const std::string& endpoint, const std::string& controlEndpoint);
   void disconnectZmqSockets(const std::string& endpoint, const std::string& controlEndpoint);

   enum SocketIndex {
      ControlSocketIndex = 0,
      StreamSocketIndex,
      MonitorSocketIndex
   };

   enum InternalCommandCode {
      CommandStop = 0
   };

   bool isActive() const;

   bool recvData(const ZmqContext::sock_ptr& dataSocket);

   bool ConnectToPublisherEndpoint(const std::string& endpoint, SubscriberConnectionListener* listener);

private:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<ZmqContext>      context_;

   std::string                      connectionName_;
   std::atomic_bool                 isConnected_;

   ZmqContext::sock_ptr             dataSocket_;
   ZmqContext::sock_ptr             masterPairSocket_;
   ZmqContext::sock_ptr             slavePairSocket_;
   ZmqContext::sock_ptr             monSocket_;

   std::thread                      listenThread_;
   SubscriberConnectionListener*    listener_ = nullptr;
   std::atomic_bool                 isListenerActive_;
};

#endif // __SUBSCRIBER_CONNECTION_H__
