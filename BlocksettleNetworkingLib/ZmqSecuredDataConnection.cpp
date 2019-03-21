#include "ZmqSecuredDataConnection.h"

#include "FastLock.h"
#include "MessageHolder.h"

#include <zmq.h>
#include <spdlog/spdlog.h>

ZmqSecuredDataConnection::ZmqSecuredDataConnection(const std::shared_ptr<spdlog::logger>& logger
                                                   , bool monitored)
 : ZmqDataConnection(logger, monitored)
{
   // CLASS WILL BE REMOVED EVENTUALLY
}

bool ZmqSecuredDataConnection::SetServerPublicKey(const BinaryData& key)
{
   // CLASS WILL BE REMOVED EVENTUALLY
}

bool ZmqSecuredDataConnection::ConfigureDataSocket(const ZmqContext::sock_ptr& s)
{
   int lingerPeriod = 0;
   int result = zmq_setsockopt(s.get(), ZMQ_LINGER, &lingerPeriod
                           , sizeof(lingerPeriod));
   if (result != 0) {
      if (logger_) {
         logger_->error("[ZmqSecuredDataConnection::{}] {} failed to set "
            "linger interval: {}", __func__ , connectionName_
            , zmq_strerror(zmq_errno()));
      }
      return false;
   }

   return true;
}

ZmqContext::sock_ptr ZmqSecuredDataConnection::CreateDataSocket()
{
   return context_->CreateClientSocket();
}

bool ZmqSecuredDataConnection::recvData()
{
   MessageHolder data;

   int result = zmq_msg_recv(&data, dataSocket_.get(), ZMQ_DONTWAIT);
   if (result == -1) {
      if (logger_) {
         logger_->error("[ZmqSecuredDataConnection::{}] {} failed to recv data "
            "frame from stream: {}" , __func__, connectionName_
            , zmq_strerror(zmq_errno()));
      }
      return false;
   }

   onRawDataReceived(data.ToString());

   return true;
}

bool ZmqSecuredDataConnection::send(const std::string& data)
{
   int result = -1;
   {
      FastLock locker(lockSocket_);
      result = zmq_send(dataSocket_.get(), data.c_str(), data.size(), 0);
   }
   if (result != (int)data.size()) {
      if (logger_) {
         logger_->error("[ZmqSecuredDataConnection::{}] {} failed to send "
            "data: {}", __func__, connectionName_, zmq_strerror(zmq_errno()));
      }
      return false;
   }

   return true;
}

void ZmqSecuredDataConnection::onRawDataReceived(const std::string& rawData)
{
   notifyOnData(rawData);
}
