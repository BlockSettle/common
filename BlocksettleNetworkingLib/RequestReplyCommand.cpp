#include "RequestReplyCommand.h"

#include <spdlog/spdlog.h>

#include "DataConnection.h"
#include "ManualResetEvent.h"

RequestReplyCommand::RequestReplyCommand(const std::string& name
      , const std::shared_ptr<DataConnection>& connection
      , const std::shared_ptr<spdlog::logger>& logger)
 : name_(name)
 , connection_(connection)
 , logger_(logger)
 , dropResult_(false)
 , replyReceived_(false)
 , result_(false)
 , executeOnConnect_(false)
 , requestCompleted_(nullptr)
{
}

RequestReplyCommand::~RequestReplyCommand() noexcept
{
   if (connection_) {
      connection_->closeConnection();
   }
}

void RequestReplyCommand::SetReplyCallback(const data_callback_type& callback)
{
   replyCallback_ = callback;
}

void RequestReplyCommand::SetErrorCallback(const error_callback_type& callback)
{
   errorCallback_ = callback;
}

void RequestReplyCommand::CleanupCallbacks()
{
   data_callback_type   oldReplyCallback;
   error_callback_type  oldErrorCallback;

   std::swap(oldReplyCallback, replyCallback_);
   std::swap(oldErrorCallback, errorCallback_);
}

bool RequestReplyCommand::ExecuteRequest(const std::string& host
   , const std::string& port, const std::string& data, bool executeOnConnect)
{
   if (requestCompleted_ != nullptr) {
      logger_->error("[RequestReplyCommand::ExecuteRequest] create new object for each request");
      return false;
   }

   requestCompleted_ = std::make_shared<ManualResetEvent>();
   requestCompleted_->ResetEvent();

   if (!replyCallback_ || !errorCallback_) {
      logger_->error("[RequestReplyCommand] {}: not all callbacks are set", name_);
      return false;
   }

   requestData_ = data;

   bool connectionOpened = connection_->openConnection(host, port, this);
   if (!connectionOpened) {
      logger_->error("[RequestReplyCommand] {}: failed to open connection to {}:{}"
         ,  name_, host, port);
      return false;
   }

   executeOnConnect_ = executeOnConnect;

   if (!executeOnConnect) {
      if (!connection_->send(requestData_)) {
         std::string errorMessage = name_ + ": failed to send request";
         logger_->error("[RequestReplyCommand::ExecuteRequest]: {}", errorMessage);
         requestCompleted_->SetEvent();
         return false;
      }
   }

   return true;
}

void RequestReplyCommand::OnDataReceived(const std::string& data)
{
   if (!replyReceived_) {
      replyReceived_ = true;
      requestCompleted_->SetEvent();
      if (dropResult_) {
         result_ = true;
      }
      else {
         result_ = replyCallback_(data);
      }
   } else {
      logger_->error("[RequestReplyCommand::OnDataReceived] reply already received. Ignore data for {}."
         , name_);
   }
}

void RequestReplyCommand::OnConnected()
{
   if (executeOnConnect_) {
      if (!connection_->send(requestData_)) {
         std::string errorMessage = name_ + ": failed to send request";
         logger_->error("[RequestReplyCommand::OnConnected] {}", errorMessage);
         if (!dropResult_ && errorCallback_) {
            errorCallback_(errorMessage);
         }
         requestCompleted_->SetEvent();
      }
   }
}

void RequestReplyCommand::OnDisconnected()
{
   if (!replyReceived_) {
      std::string errorMessage = name_ + ": disconnected from server without reply";
      logger_->error("[RequestReplyCommand::OnDisconnected]: {}", errorMessage);
      if (!dropResult_ && errorCallback_) {
         errorCallback_(errorMessage);
      }
      requestCompleted_->SetEvent();
   }
}

void RequestReplyCommand::OnError(DataConnectionError errorCode)
{
   std::string errorMessage = name_ + ": get error from data connection " + std::to_string(errorCode);
   logger_->error("{}", errorMessage);
   if (!dropResult_ && errorCallback_) {
      errorCallback_(errorMessage);
   }
   requestCompleted_->SetEvent();
}

bool RequestReplyCommand::WaitForRequestedProcessed(uint64_t milliseconds)
{
   return requestCompleted_->WaitForEvent(milliseconds);
}
