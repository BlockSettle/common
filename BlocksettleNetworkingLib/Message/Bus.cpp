/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Message/Bus.h"
#include <spdlog/spdlog.h>
#include "Message/Adapter.h"
#include "PerfAccounting.h"
#include "StringUtils.h"

//#define MSG_DEBUGGING   // comment this out to disable queue message logging
using namespace bs::message;

static const std::string kQuitMessage("QUIT");
static const std::string kAccResetMessage("ACC_RESET");

Router::Router(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{}

void Router::bindAdapter(const std::shared_ptr<Adapter> &adapter)
{
   if (!adapter) {
      throw std::runtime_error("invalid null adapter");
   }
   const auto &supportedReceivers = adapter->supportedReceivers();
   if (supportedReceivers.empty()) {
      logger_->error("[Router::bindAdapter] {} has no supported receivers", adapter->name());
      return;
   }
   if (supportedReceivers.size() == 1) {
      const auto receiver = *supportedReceivers.begin();
      if (receiver && receiver->isSupervisor()) {
         supervisor_ = adapter;
         return;
      }
   }

   for (const auto &receiver : supportedReceivers) {
      if (!receiver) {
         logger_->error("[Router::bindAdapter] {} has null receiver", adapter->name());
         continue;
      }
      if (receiver->isFallback()) {
         defaultRoute_ = adapter;
         continue;
      }
      const auto itReceiver = adapters_.find(receiver->value());
      if (itReceiver != adapters_.end()) {
         logger_->critical("[Router::bindAdapter] adapter {} for {} already exists "
            "- overriding with {}", itReceiver->second->name(), receiver->name(), adapter->name());
      }
      std::lock_guard<std::mutex> lock(mutex_);
      adapters_[receiver->value()] = adapter;
   }
}

std::set<UserValue> Router::supportedReceivers() const
{
   std::set<UserValue> result;
   for (const auto &adapter : adapters_) {
      result.insert(adapter.first);
   }
   return result;
}

bool Router::isDefaultRouted(const bs::message::Envelope &env) const
{
   if (env.receiver->isFallback()) {
      return true;
   }
   auto itAdapter = adapters_.find(env.receiver->value());
   if (itAdapter == adapters_.end()) {
      if (env.sender->isFallback()) {
         logger_->warn("[Router::process] failed to find route for {} "
            "(from {}) - dropping message", env.receiver->name(), env.sender->name());
         return false;
      }
   } else {
      return false;
   }
   return true;
}

std::vector<std::shared_ptr<bs::message::Adapter>> Router::process(const bs::message::Envelope &env) const
{
   std::set<std::shared_ptr<bs::message::Adapter>> result;
   if (supervisor_ && !supervisor_->process(env)) {
      logger_->info("[Router::process] msg #{} seized by supervisor", env.id());
      return {};
   }
   if (!env.receiver || env.receiver->isBroadcast()) {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto &adapter : adapters_) {
         if (!env.sender->isSystem() && (adapter.first == env.sender->value())) {
            continue;
         }
         result.insert(adapter.second);
      }
      if (defaultRoute_ && !env.sender->isFallback()) {
         result.insert(defaultRoute_);
      }
   } else {
      if (isDefaultRouted(env)) {
         if (defaultRoute_) {
            return { defaultRoute_ };
         }
         else {
            throw std::runtime_error("no route");
         }
      }
      else {
         try {
            return { adapters_.at(env.receiver->value()) };
         }
         catch (const std::exception &) {
            throw std::runtime_error("receiver not found");
         }
      }
   }
   if (result.empty()) {
      throw std::runtime_error("no destination found");
   }
   return { result.cbegin(), result.cend() };
}

void Router::reset()
{
   supervisor_.reset();
   adapters_.clear();
}


SeqId QueueInterface::resetId(SeqId newId)
{
   if (seqNo_ < newId) {
      seqNo_ = newId;
   }
   return seqNo_;
}

bool bs::message::QueueInterface::accept(const Envelope& env)
{
   if (!env.sender) {
      return false;
   }
   const auto& itDeferred = deferredIds_.find(env.id());
   if (itDeferred != deferredIds_.end()) {
      deferredIds_.erase(itDeferred);
      return true;
   }
   return (env.id() > lastProcessedSeqNo_);
}

Queue_Locking::Queue_Locking(const std::shared_ptr<RouterInterface> &router
   , const std::shared_ptr<spdlog::logger> &logger, const std::string &name
   , const std::map<int, std::string> &accMap, bool accounting)
   : QueueInterface(router, name)
   , logger_(logger), accMap_(accMap), accounting_(accounting)
{
   thread_ = std::thread(&Queue::process, this);
}

Queue_Locking::~Queue_Locking()
{
   terminate();
}

void Queue_Locking::terminate()
{
   stop();
   if (thread_.joinable()) {
      thread_.join();
   }
   router_->reset();
}


void Queue_Locking::stop()
{
   auto envQuit = Envelope::makeRequest(std::make_shared<UserSystem>(), std::make_shared<UserSystem>()
      , kQuitMessage);
   pushFill(envQuit);
}

bool Queue_Locking::pushFill(Envelope &env)
{
   if (env.posted.time_since_epoch().count() == 0) {
      env.posted = bus_clock::now();
   }
   std::unique_lock<std::mutex> lock(cvMutex_);
   if (env.id() == 0) {
      env.setId(nextId());
   }

#ifdef MSG_DEBUGGING
   std::string msgBody;
   for (const char c : env.message) {
      if ((c < 32) || (c > 126)) {
         break;
      }
      msgBody += c;
      if (msgBody.length() >= 8) {
         break;
      }
   }
   if (msgBody.empty() && !env.message.empty()) {
      msgBody = bs::toHex(env.message.substr(0, 8));
      if (env.message.size() > 8) {
         msgBody += "...";
      }
   }
   logger_->debug("[Queue::push] {}: #{}/{} {}({}) -> {}({}) r#{} f:{} [{}] {}"
      , name_, env.id(), env.foreignId()
      , env.sender->name(), env.sender->value()
      , env.receiver ? env.receiver->name() : "null"
      , env.receiver ? env.receiver->value() : 0, env.responseId()
      , (bs::message::SeqId)env.envelopeType(), env.message.size()
      , msgBody.empty() ? msgBody : "'" + msgBody + "'");
#endif   //MSG_DEBUGGING

   queue_.push_back(env);
   cvQueue_.notify_one();
   return true;
}

void Queue_Locking::process()
{
   srand(std::time(nullptr));    // requred for per-thread randomness
   logger_->debug("[Queue::process] {} started", name_);
   decltype(queue_) deferredQueue;
   auto dqTime = bus_clock::now();
   auto accTime = bus_clock::now();
   PerfAccounting acc;

   const auto &processPortion = [this, &deferredQueue, &dqTime, &accTime, &acc]
      (const decltype(queue_) &tempQueue, const bus_clock::time_point &timeNow)
   {
      for (const auto &env : tempQueue) {
         if (env.executeAt.time_since_epoch().count() != 0) {
            if (env.executeAt > timeNow) {
               deferredIds_.insert(env.id());
               deferredQueue.emplace_back(env);
               continue;
            }
         } else if (accounting_) {
            acc.addQueueTime(std::chrono::duration_cast<std::chrono::microseconds>(timeNow - env.posted));
         }

         if (!accept(env)) {
            logger_->info("[Queue::process] {}: envelope #{} failed to pass "
               "validity checks (<= {}) - skipping", name_, env.id(), lastProcessedSeqNo_);
            continue;
         }

         if (env.receiver && env.sender->isSystem() && env.receiver->isSystem()) {
            if (env.message == kQuitMessage) {
               logger_->info("[Queue::process] {} detected quit system message", name_);
               running_ = false;
               break;
            } else if (env.message == kAccResetMessage) {
               acc.reset();
               continue;
            } else {
               logger_->warn("[Queue::process] {} unknown system message {} - skipping"
                  , name_, env.message);
            }
         } else {
            TimeStamp procStart;
            const bool isBroadcast = (!env.receiver || env.receiver->isBroadcast());
            const auto& process = [this, env, isBroadcast, timeNow, &procStart, &acc, &deferredQueue]
               (const std::shared_ptr<bs::message::Adapter>&adapter)
            {
               currentEnvId_ = env.id();
               if (isBroadcast) {
                  const bool processed = adapter->processBroadcast(env);
                  if (accounting_ && processed) {
                     const auto& timeNow = bus_clock::now();
                     acc.add(static_cast<int>((*adapter->supportedReceivers().cbegin())->value() + 0x1000)
                        , std::chrono::duration_cast<std::chrono::microseconds>(timeNow - procStart));
                     procStart = timeNow;
                  }
               }
               else {
                  if (!adapter->process(env)) {
                     const auto& result = deferredIds_.insert(env.id());
                     if (result.second) { // avoid duplicates
                        deferredQueue.emplace_back(env);
                     }
                  }
                  if (accounting_) {
                     acc.add(static_cast<int>(env.receiver ? env.receiver->value() : 0)
                        , std::chrono::duration_cast<std::chrono::microseconds>(bus_clock::now() - procStart));
                  }
               }
               currentEnvId_ = 0;
            };

            if (accounting_) {
               procStart = bus_clock::now();
            }
            try {
               const auto& adapters = router_->process(env);
               if (adapters.empty()) {
                  continue;   // empty result is intended for skipping a message silently (e.g. by supervisor)
               }
               for (const auto& adapter : adapters) {
#ifdef MSG_DEBUGGING
                  logger_->debug("[Queue::process] {}: #{}/{} r#{} f:{} by {}"
                     , name_, env.id(), env.foreignId(), env.responseId()
                     , (bs::message::SeqId)env.envelopeType(), adapter->name());
#endif
                  process(adapter);
               }
            }
            catch (const std::exception& e) {
               logger_->error("[Queue::process] {}: {} for #{} "
                  "from {} ({}) to {} ({}) - skipping", name_, e.what(), env.id()
                  , env.sender->value(), env.sender->name()
                  , env.receiver ? env.receiver->value() : 0
                  , env.receiver ? env.receiver->name() : "null");
               continue;
            }
         }
         if (env.id() > lastProcessedSeqNo_) {
            lastProcessedSeqNo_ = env.id();
         }
      }
   };

   while (running_) {
      {
         std::unique_lock<std::mutex> lock(cvMutex_);
         if (queue_.empty()) {
            cvQueue_.wait_for(lock, std::chrono::milliseconds{ 10 });
         }
      }
      if (!running_) {
         break;
      }
      const auto &timeNow = bus_clock::now();
      if (!deferredQueue.empty()) {
         decltype(queue_) tempQueue;
         deferredQueue.swap(tempQueue);
         processPortion(tempQueue, timeNow);
      }
      decltype(queue_) tempQueue;
      {
         std::unique_lock<std::mutex> lock(cvMutex_);
         tempQueue.swap(queue_);
      }
      if (!tempQueue.empty()) {
         processPortion(tempQueue, timeNow);
      }

      if ((deferredQueue.size() > 100) && ((timeNow - dqTime) > deferredQueueInterval_)) {
         dqTime = bus_clock::now();
         logger_->warn("[Queue::process] {} deferred queue has grown to {}/{} elements"
            , name_, deferredQueue.size(), deferredIds_.size());
      }
      if (accounting_ && ((timeNow - accTime) >= accountingInterval_)) {
         accTime = bus_clock::now();
         acc.report(logger_, name_, accMap_);
      }
   }
   if (accounting_) {
      acc.report(logger_, name_, accMap_);
   }
   logger_->debug("[Queue::process] {} finished", name_);
}

void Queue_Locking::bindAdapter(const std::shared_ptr<Adapter> &adapter)
{
   router_->bindAdapter(adapter);
}

std::set<UserValue> Queue_Locking::supportedReceivers() const
{
   return router_->supportedReceivers();
}
