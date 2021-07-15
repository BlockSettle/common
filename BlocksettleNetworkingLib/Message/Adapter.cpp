/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Message/Adapter.h"
#include "Message/Bus.h"

using namespace bs::message;

bool Adapter::pushFill(Envelope &env)
{
   if (!queue_) {
      return false;
   }
   return queue_->pushFill(env);
}

SeqId Adapter::pushRequest(const std::shared_ptr<User>& sender
   , const std::shared_ptr<User>& receiver
   , const std::string& msg, const TimeStamp& execAt)
{
   auto env = Envelope::makeRequest(sender, receiver, msg, execAt);
   if (pushFill(env)) {
      return env.id();
   }

   return 0;
}

SeqId Adapter::pushResponse(const std::shared_ptr<User>& sender
   , const std::shared_ptr<User>& receiver
   , const std::string& msg, SeqId respId)
{
   auto env = Envelope::makeResponse(sender, receiver, msg, respId);
   if (pushFill(env)) {
      return env.id();
   }
   return 0;
}

SeqId Adapter::pushResponse(const std::shared_ptr<User>& sender
   , const bs::message::Envelope& envReq, const std::string& msg)
{
   auto env = Envelope::makeResponse(sender, envReq.sender, msg, envReq.foreignId());
   if (pushFill(env)) {
      return env.id();
   }
   return 0;
}

SeqId Adapter::pushBroadcast(const std::shared_ptr<User>& sender
   , const std::string& msg, bool global)
{
   auto env = Envelope::makeBroadcast(sender, msg, global);
   if (pushFill(env)) {
      return env.id();
   }
   return 0;
}


bool PipeAdapter::process(const Envelope &env)
{
   if (!endpoint_) {
      return false;
   }
   auto envCopy = env;
   return endpoint_->pushFill(envCopy);
}

bool PipeAdapter::processBroadcast(const Envelope& env)
{
   return process(env);
}


bool RelayAdapter::isInitialized() const
{
   return (fallbackUser_ != nullptr);
}

Adapter::Users RelayAdapter::supportedReceivers() const
{
   if (!isInitialized()) {
      throw std::runtime_error("invalid initialization");
   }
   return { fallbackUser_ };
}

void RelayAdapter::setQueue(const std::shared_ptr<QueueInterface>& queue)
{
   if (!queue_) {
      Adapter::setQueue(queue);
   }
   queues_.insert(queue);
   for (const auto& user : queue->supportedReceivers()) {
      queueByUser_[user] = queue;
   }
}

bool RelayAdapter::process(const Envelope& env)
{
   if (!isInitialized()) {
      throw std::runtime_error("invalid initialization");
   }
   return relay(env);
}

bool RelayAdapter::processBroadcast(const Envelope& env)
{
   if (env.envelopeType() == EnvelopeType::Processed) {
      return false;
   }
   if (!isInitialized()) {
      throw std::runtime_error("invalid initialization");
   }
   if ((env.id() != env.foreignId()) && (env.envelopeType() == EnvelopeType::GlobalBroadcast)) {
      return false;  // global broadcasts are processed elsewhere (external relayer like AMQP)
   }
   for (const auto& queue : queues_) {
      if (!queue->isCurrentlyProcessing(env)) {
         auto envCopy = env;
         envCopy.setId(0);
         envCopy.setEnvelopeType(EnvelopeType::Processed);
         queue->pushFill(envCopy);
      }
   }
   return false;  // don't account processing time
}

bool RelayAdapter::relay(const Envelope& env)
{
   if (!env.receiver || env.receiver->isBroadcast()) {
      return true;   // ignore broadcasts
   }
   const auto& itQueue = queueByUser_.find(env.receiver->value());
   if (itQueue == queueByUser_.end()) {
      return false;
   }
   auto envCopy = env;
   envCopy.setId(0);
   return itQueue->second->pushFill(envCopy);
}
