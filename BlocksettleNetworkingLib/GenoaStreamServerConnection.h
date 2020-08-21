/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __GENOA_STREAM_SERVER_CONNECTION_H__
#define __GENOA_STREAM_SERVER_CONNECTION_H__

#include "ZmqStreamServerConnection.h"

class GenoaStreamServerConnection : public ZmqStreamServerConnection
{
public:
   GenoaStreamServerConnection(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<ZmqContext> &);
   ~GenoaStreamServerConnection() noexcept = default;

   GenoaStreamServerConnection(const GenoaStreamServerConnection&) = delete;
   GenoaStreamServerConnection& operator = (const GenoaStreamServerConnection&) = delete;

   GenoaStreamServerConnection(GenoaStreamServerConnection&&) = delete;
   GenoaStreamServerConnection& operator = (GenoaStreamServerConnection&&) = delete;

protected:
   server_connection_ptr CreateActiveConnection() override;
};

#endif // __GENOA_STREAM_SERVER_CONNECTION_H__