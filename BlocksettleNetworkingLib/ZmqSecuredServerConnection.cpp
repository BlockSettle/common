#include "ZmqSecuredServerConnection.h"
#include "ZMQHelperFunctions.h"
#include "FastLock.h"
#include "MessageHolder.h"

#include <zmq.h>
#include <spdlog/spdlog.h>


ZmqSecuredServerConnection::ZmqSecuredServerConnection(const std::shared_ptr<spdlog::logger>& logger
      , const std::shared_ptr<ZmqContext>& context)
 : ZmqServerConnection(logger, context)
{}

ZmqContext::sock_ptr ZmqSecuredServerConnection::CreateDataSocket()
{
   return context_->CreateServerSocket();
}

bool ZmqSecuredServerConnection::ConfigDataSocket(const ZmqContext::sock_ptr& dataSocket)
{
   return ZmqServerConnection::ConfigDataSocket(dataSocket);
}

bool ZmqSecuredServerConnection::ReadFromDataSocket()
{
   MessageHolder clientId;
   MessageHolder data;

   int result = zmq_msg_recv(&clientId, dataSocket_.get(), ZMQ_DONTWAIT);
   if (result == -1) {
      logger_->error("[ZmqSecuredServerConnection::ReadFromDataSocket] {} failed to recv header: {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   const auto &clientIdStr = clientId.ToString();
   if (clientInfo_.find(clientIdStr) == clientInfo_.end()) {
#ifdef WIN32
      SOCKET socket = 0;
#else
      int socket = 0;
#endif
      size_t sockSize = sizeof(socket);
      if (zmq_getsockopt(dataSocket_.get(), ZMQ_FD, &socket, &sockSize) == 0) {
         clientInfo_[clientIdStr] = bs::network::peerAddressString(static_cast<int>(socket));
      }
   }

   result = zmq_msg_recv(&data, dataSocket_.get(), ZMQ_DONTWAIT);
   if (result == -1) {
      logger_->error("[ZmqSecuredServerConnection::ReadFromDataSocket] {} failed to recv message data: {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   if (!data.IsLast()) {
      logger_->error("[ZmqSecuredServerConnection::ReadFromDataSocket] {} broken protocol"
         , connectionName_);
      return false;
   }

   notifyListenerOnData(clientIdStr, data.ToString());
   return true;
}

bool ZmqSecuredServerConnection::SendDataToClient(const std::string& clientId, const std::string& data, const SendResultCb &cb)
{
   return QueueDataToSend(clientId, data, cb, false);
}
