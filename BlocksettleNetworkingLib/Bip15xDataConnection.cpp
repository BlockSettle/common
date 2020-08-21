/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Bip15xDataConnection.h"

#include <spdlog/spdlog.h>
#include "Transport.h"

class Bip15xDataListener : public DataConnectionListener
{
public:
   Bip15xDataConnection *owner_{};

   void OnDataReceived(const std::string& data) override
   {
      owner_->transport_->onRawDataReceived(data);
   }

   void OnConnected() override
   {
      owner_->transport_->startHandshake();
   }

   void OnDisconnected() override
   {
      if (owner_->connected_) {
         owner_->connected_ = false;
         owner_->listener_->OnDisconnected();
      }
   }

   void OnError(DataConnectionError errorCode) override
   {
      OnDisconnected();
      owner_->listener_->OnError(errorCode);
   }
};


Bip15xDataConnection::Bip15xDataConnection(const std::shared_ptr<spdlog::logger> &logger
   , std::unique_ptr<DataConnection> conn
   , const std::shared_ptr<bs::network::TransportClient> &tr)
   : logger_(logger)
   , transport_(tr)
   , conn_(std::move(conn))
{
   assert(conn_);
   transport_->setSendCb([this](const std::string &d) {
      return conn_->send(d);
   });

   transport_->setNotifyDataCb([this](const std::string &d) {
      listener_->OnDataReceived(d);
   });

   transport_->setSocketErrorCb([this](DataConnectionListener::DataConnectionError e) {
      if (e == DataConnectionListener::NoError) {
         {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!sendQueue_.empty()) {
               transport_->sendData(sendQueue_.front());
               sendQueue_.pop();
            }
            connected_ = true;
         }
         listener_->OnConnected();
         return;
      }
      listener_->OnError(e);
   });
}

Bip15xDataConnection::~Bip15xDataConnection()
{
   closeConnection();
}

bool Bip15xDataConnection::openConnection(const std::string &host, const std::string &port
   , DataConnectionListener *listener)
{
   listener_ = listener;
   ownListener_ = std::make_unique<Bip15xDataListener>();
   ownListener_->owner_ = this;
   transport_->openConnection(host, port);
   return conn_->openConnection(host, port, ownListener_.get());
}

bool Bip15xDataConnection::closeConnection()
{
   bool result = conn_->closeConnection();
   transport_->closeConnection();
   return result;
}

bool Bip15xDataConnection::send(const std::string &data)
{
   {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_) {
         sendQueue_.push(data);
         return true;
      }
   }
   return transport_->sendData(data);
}
