#ifndef __SERVER_CONNECTION_H__
#define __SERVER_CONNECTION_H__

#include "ServerConnectionListener.h"

#include <functional>
#include <memory>
#include <string>

class ServerConnection
{
public:
   ServerConnection() = default;
   virtual ~ServerConnection() noexcept = default;

   ServerConnection(const ServerConnection&) = delete;
   ServerConnection& operator = (const ServerConnection&) = delete;

   ServerConnection(ServerConnection&&) = delete;
   ServerConnection& operator = (ServerConnection&&) = delete;

public:
   virtual bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) = 0;

   virtual std::string GetClientInfo(const std::string &clientId) const = 0;

   using SendResultCb = std::function<void(const std::string &clientId, const std::string &data, bool)>;
   virtual bool SendDataToClient(const std::string& clientId, const std::string& data
      , const SendResultCb &cb = nullptr) = 0;
   virtual bool SendDataToAllClients(const std::string&, const SendResultCb &cb = nullptr) { return false; }
};

#endif // __SERVER_CONNECTION_H__