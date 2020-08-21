/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __SERVER_CONNECTION_LISTENER_H__
#define __SERVER_CONNECTION_LISTENER_H__

#include <map>
#include <string>

class ServerConnectionListener
{
public:
   enum ClientError
   {
      // Reported when client do not have valid credentials (unknown public key)
      HandshakeFailed = 1,
      Timeout = 2,
   };

   enum class Detail
   {
      IpAddr = 1,
      PublicKey = 2,
   };
   using Details = std::map<Detail, std::string>;

   ServerConnectionListener() = default;
   virtual ~ServerConnectionListener() noexcept = default;

   ServerConnectionListener(const ServerConnectionListener&) = delete;
   ServerConnectionListener& operator = (const ServerConnectionListener&) = delete;

   ServerConnectionListener(ServerConnectionListener&&) = delete;
   ServerConnectionListener& operator = (ServerConnectionListener&&) = delete;

public:
   virtual void OnDataFromClient(const std::string& clientId, const std::string& data) = 0;

   virtual void OnClientConnected(const std::string &clientId, const Details &details) = 0;
   virtual void OnClientDisconnected(const std::string& clientId) = 0;

   virtual void onClientError(const std::string &clientId, ClientError error, const Details &details) {}
};

#endif // __SERVER_CONNECTION_LISTENER_H__
