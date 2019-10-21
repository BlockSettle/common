#include "SubscriberConnection.h"

#include "MessageHolder.h"
#include "ZmqHelperFunctions.h"

#include <zmq.h>
#include <spdlog/spdlog.h>

const std::string INPROC_EP = std::string("inproc://");
const std::string TCP_EP = std::string("tcp://");
const std::string INPROC_MON_EP = std::string("inproc://mon-");

SubscriberConnectionListenerCB::SubscriberConnectionListenerCB(const dataReceivedCB& onDataReceived
   , const connectedCB& onConnected
   , const disconnectedCB& onDisconnected)
 : onDataReceived_{onDataReceived}
 , onConnected_{onConnected}
 , onDisconnected_{onDisconnected}
{}

void SubscriberConnectionListenerCB::OnDataReceived(std::string data)
{
   if (onDataReceived_) {
      onDataReceived_(data);
   }
}
void SubscriberConnectionListenerCB::OnConnected()
{
   if (onConnected_) {
      onConnected_();
   }
}
void SubscriberConnectionListenerCB::OnDisconnected()
{
   if (onDisconnected_) {
      onDisconnected_();
   }
}

SubscriberConnection::SubscriberConnection(const std::shared_ptr<spdlog::logger>& logger, 
   const std::shared_ptr<ZmqContext>& context,
   QObject* parent)
 : logger_{logger}
 , context_(context)
 , isConnected_{false}
 , isListenerActive_{false}
 , dataSocket_{ZmqContext::CreateNullSocket()}
 , masterPairSocket_{ZmqContext::CreateNullSocket()}
 , slavePairSocket_{ZmqContext::CreateNullSocket()}
 , monSocket_{ZmqContext::CreateNullSocket()}
 , QObject(parent)
{}

SubscriberConnection::~SubscriberConnection() noexcept
{
   stopListen();
}

bool SubscriberConnection::isActive() const
{
   return listenThread_.joinable();
}

bool SubscriberConnection::ConnectToPublisher(const std::string& endpointName, SubscriberConnectionListener* listener)
{
   const std::string endpoint = INPROC_EP + endpointName;

   return ConnectToPublisherEndpoint(endpoint, listener);
}

bool SubscriberConnection::ConnectToPublisher(const std::string& host, const std::string& port, SubscriberConnectionListener* listener)
{
   const std::string endpoint = TCP_EP + host + ":" + port;

   return ConnectToPublisherEndpoint(endpoint, listener);
}

bool SubscriberConnection::ConnectToPublisherEndpoint(const std::string& endpoint, SubscriberConnectionListener* listener)
{
   if (listener == nullptr) {
      logger_->error("[SubscriberConnection::ConnectToPublisherEndpoint] empty listener not allowed");
      return false;
   }

   isListenerActive_ = true;

   if (isActive()) {
      logger_->error("[SubscriberConnection::ConnectToPublisherEndpoint] connection active.");
      return false;
   }

   connectionName_ = context_->GenerateConnectionName(endpoint);
   std::string controlEndpoint = INPROC_EP + connectionName_;

   // create master paired socket to control connection and resend data
   masterPairSocket_ = context_->CreateInternalControlSocket();
   if (masterPairSocket_ == nullptr) {
      logger_->error("[SubscriberConnection::ConnectToPublisherEndpoint] failed to create ThreadMasterSocket socket {}: {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   int result = zmq_bind(masterPairSocket_.get(), controlEndpoint.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::ConnectToPublisherEndpoint] failed to bind ThreadMasterSocket socket {}: {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   listener_ = listener;

   connect(this, &SubscriberConnection::dataReceived, listener_, &SubscriberConnectionListener::OnDataReceived, Qt::QueuedConnection);
   connect(this, &SubscriberConnection::connected, listener_, &SubscriberConnectionListener::OnConnected, Qt::QueuedConnection);
   connect(this, &SubscriberConnection::disconnected, listener_, &SubscriberConnectionListener::OnDisconnected, Qt::QueuedConnection);

   // and start thread
   listenThread_ = std::thread(&SubscriberConnection::listenFunction, this, endpoint, controlEndpoint);

   return true;
}

void SubscriberConnection::stopListen()
{
   if (!isListenerActive_) {
      return;
   }

   isListenerActive_ = false;

   if (!isActive()) {
      return;
   }

   int command = SubscriberConnection::CommandStop;
   int result = zmq_send(masterPairSocket_.get(), static_cast<void*>(&command), sizeof(command), 0);
   if (result == -1) {
      logger_->error("[SubscriberConnection::stopListen] failed to send stop comamnd for {} : {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      emit stopped();
      return;
   }

   // clean master
   std::string controlEndpoint = INPROC_EP + connectionName_;
   result = zmq_disconnect(masterPairSocket_.get(), controlEndpoint.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::stopListen] Master socket disconnect failed"
         , connectionName_, zmq_strerror(zmq_errno()));
   }

   if (std::this_thread::get_id() == listenThread_.get_id()) {
      listenThread_.detach();
   }
   else {
      try {
         listenThread_.join();
      }
      catch (const std::exception & e) {
         logger_->error("[SubscriberConnection::stopListen] failed to join thread: {}", e.what());
      }
   }

   emit stopped();
}

void SubscriberConnection::initZmqSockets(const std::string& endpoint, const std::string& controlEndpoint)
{
   dataSocket_ = context_->CreateSubscribeSocket();
   if (dataSocket_ == nullptr) {
      logger_->error("[SubscriberConnection::initZmqSockets] failed to create socket {} : {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   // create slave paired socket to control connection and resend data
   slavePairSocket_ = context_->CreateInternalControlSocket();
   if (slavePairSocket_ == nullptr) {
      logger_->error("[SubscriberConnection::initZmqSockets] failed to create ThreadSlaveSocket socket {} : {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   int result = zmq_connect(slavePairSocket_.get(), controlEndpoint.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::initZmqSockets] failed to connect ThreadSlaveSocket socket {}. {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   std::string monitorConnectionName = INPROC_MON_EP + connectionName_;

   result = zmq_socket_monitor(dataSocket_.get(), monitorConnectionName.c_str(), ZMQ_EVENT_ALL);
   if (result != 0) {
      logger_->error("[SubscriberConnection::initZmqSockets] Failed to create monitor socket: {}"
         , zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   monSocket_ = context_->CreateMonitorSocket();
   result = zmq_connect(monSocket_.get(), monitorConnectionName.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::initZmqSockets] Failed to connect monitor socket: {}"
         , zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   // connect subscriber socket
   result = zmq_connect(dataSocket_.get(), endpoint.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::initZmqSockets] failed to connect socket to {}. {}"
         , endpoint, zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   // subscribe to data (all messages, no filtering)
   result = zmq_setsockopt(dataSocket_.get(), ZMQ_SUBSCRIBE, nullptr, 0);
   if (result != 0) {
      logger_->error("[SubscriberConnection::initZmqSockets] failed to subscribe: {}"
         , zmq_strerror(zmq_errno()));
      emit disconnected();
      return;
   }

   // ok, move temp data to members
   isConnected_ = false;
}

void SubscriberConnection::listenFunction(const std::string& endpoint, const std::string& controlEndpoint)
{
   initZmqSockets(endpoint, controlEndpoint);

   zmq_pollitem_t  poll_items[3];
   memset(&poll_items, 0, sizeof(poll_items));

   poll_items[SubscriberConnection::ControlSocketIndex].socket = slavePairSocket_.get();
   poll_items[SubscriberConnection::ControlSocketIndex].events = ZMQ_POLLIN;

   poll_items[SubscriberConnection::StreamSocketIndex].socket = dataSocket_.get();
   poll_items[SubscriberConnection::StreamSocketIndex].events = ZMQ_POLLIN;

   poll_items[SubscriberConnection::MonitorSocketIndex].socket = monSocket_.get();
   poll_items[SubscriberConnection::MonitorSocketIndex].events = ZMQ_POLLIN;

   int result;

   while(true) {
      result = zmq_poll(poll_items, 3, -1);
      if (result == -1) {
         logger_->error("[SubscriberConnection::listenFunction] poll failed for {} : {}"
            , connectionName_, zmq_strerror(zmq_errno()));
         break;
      }

      if (poll_items[SubscriberConnection::ControlSocketIndex].revents & ZMQ_POLLIN) {
         MessageHolder   command;

         int recv_result = zmq_msg_recv(&command, poll_items[SubscriberConnection::ControlSocketIndex].socket, ZMQ_DONTWAIT);
         if (recv_result == -1) {
            logger_->error("[SubscriberConnection::listenFunction] failed to recv command on {} : {}"
               , connectionName_, zmq_strerror(zmq_errno()));
            break;
         }

         auto command_code = command.ToInt();
         if (command_code == SubscriberConnection::CommandStop) {
            break;
         } else {
            logger_->error("[SubscriberConnection::listenFunction] unexpected command code {} for {}"
               , command_code, connectionName_);
            break;
         }
      }

      if (poll_items[SubscriberConnection::StreamSocketIndex].revents & ZMQ_POLLIN) {
         if (!recvData(dataSocket_)) {
            break;
         }
      }

      if (monSocket_ && (poll_items[SubscriberConnection::MonitorSocketIndex].revents & ZMQ_POLLIN)) {
         switch (bs::network::get_monitor_event(monSocket_.get())) {
         case ZMQ_EVENT_CONNECTED:
            if (!isConnected_) {
               if (isListenerActive_) {
                  emit connected();
               }
               isConnected_ = true;
            }
            break;

         case ZMQ_EVENT_DISCONNECTED:
            if (isConnected_) {
               if (isListenerActive_) {
                  emit disconnected();
               }
               isConnected_ = false;
            }
            break;
         }
      }
   }

   disconnectZmqSockets(endpoint, controlEndpoint);
}

void SubscriberConnection::disconnectZmqSockets(const std::string& endpoint, const std::string& controlEndpoint)
{
   int result = zmq_disconnect(dataSocket_.get(), endpoint.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::stopListen] Dat socket disconnect failed"
         , connectionName_, zmq_strerror(zmq_errno()));
   }

   std::string monitorConnectionName = INPROC_MON_EP + connectionName_;

   result = zmq_disconnect(monSocket_.get(), monitorConnectionName.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::stopListen] Monitor socket disconnect failed"
         , connectionName_, zmq_strerror(zmq_errno()));
   }
   
   result = zmq_disconnect(slavePairSocket_.get(), controlEndpoint.c_str());
   if (result != 0) {
      logger_->error("[SubscriberConnection::stopListen] Slave socket disconnect failed"
         , connectionName_, zmq_strerror(zmq_errno()));
   }
}

bool SubscriberConnection::recvData(const ZmqContext::sock_ptr& dataSocket)
{
   MessageHolder data;

   int result = zmq_msg_recv(&data, dataSocket.get(), ZMQ_DONTWAIT);
   if (result == -1) {
      logger_->error("[SubscriberConnection::recvData] {} failed to recv data frame from stream: {}", connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   if (isListenerActive_) {
      emit dataReceived(data.ToString());
   }

   return true;
}
