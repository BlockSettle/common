#ifndef __ZMQ_SECURED_SERVER_CONNECTION_H__
#define __ZMQ_SECURED_SERVER_CONNECTION_H__

#include <QString>
#include "ZmqServerConnection.h"
#include "EncryptionUtils.h"

class ZmqSecuredServerConnection : public ZmqServerConnection
{
public:
   ZmqSecuredServerConnection(const std::shared_ptr<spdlog::logger>& logger
      , const std::shared_ptr<ZmqContext>& context);
   ~ZmqSecuredServerConnection() noexcept override = default;

   ZmqSecuredServerConnection(const ZmqSecuredServerConnection&) = delete;
   ZmqSecuredServerConnection& operator = (const ZmqSecuredServerConnection&) = delete;

   ZmqSecuredServerConnection(ZmqSecuredServerConnection&&) = delete;
   ZmqSecuredServerConnection& operator = (ZmqSecuredServerConnection&&) = delete;

   bool SetKeyPair(const SecureBinaryData& zmqPubKey
      , const SecureBinaryData& zmqPrvKey);

   bool SendDataToClient(const std::string& clientId
      , const std::string& data) override;

protected:
   ZmqContext::sock_ptr CreateDataSocket() override;
   bool ConfigDataSocket(const ZmqContext::sock_ptr& dataSocket) override;

   bool ReadFromDataSocket() override;

private:
   SecureBinaryData publicKey_;
   SecureBinaryData privateKey_;
};

#endif // __ZMQ_SECURED_SERVER_CONNECTION_H__
