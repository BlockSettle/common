/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Message/Bus.h"
#include <spdlog/spdlog.h>
#include "Message/Adapter.h"
#include "PerfAccounting.h"

using namespace bs::message;

static const std::string kQuitMessage("QUIT");
static const std::string kAccResetMessage("ACC_RESET");


Router::Router(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{}

void Router::bindAdapter(const std::shared_ptr<Adapter> &adapter)
{
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

std::vector<std::shared_ptr<bs::message::Adapter>> Router::process(const bs::message::Envelope &env) const
{
   std::vector<std::shared_ptr<bs::message::Adapter>> result;
   if (supervisor_ && !supervisor_->process(env)) {
      logger_->debug("[Router::process] msg {} skipped by supervisor", env.id);
      return {};
   }
   if (!env.receiver || env.receiver->isBroadcast()) {
      for (const auto &adapter : adapters_) {
         if (adapter.first == env.sender->value()) {
            continue;
         }
         result.push_back(adapter.second);
      }
      auto last = std::unique(result.begin(), result.end());
      result.erase(last, result.end());
   } else {
      auto itAdapter = adapters_.find(env.receiver->value());
      if (itAdapter == adapters_.end()) {
         if (env.sender->isFallback()) {
            logger_->warn("[Router::process] failed to find route for {} "
               "(from {}) - dropping message", env.receiver->name(), env.sender->name());
            return {};
         }
         if (defaultRoute_) {
            return { defaultRoute_ };
         }
         else {
            logger_->error("[Router::process] failed to find route for {} - dropping message"
               , env.receiver->name());
            return {};
         }
      }
      else {
         return { itAdapter->second };
      }
   }
   return result;
}

void Router::reset()
{
   supervisor_.reset();
   adapters_.clear();
}


Queue_Locking::Queue_Locking(const std::shared_ptr<RouterInterface> &router
   , const std::shared_ptr<spdlog::logger> &logger
   , const std::map<int, std::string> &accMap, bool accounting)
   : QueueInterface(router), logger_(logger), accMap_(accMap)
   , accounting_(accounting)
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
   Envelope envQuit{ 0, std::make_shared<UserSystem>(), std::make_shared<UserSystem>()
      , {}, {}, kQuitMessage };
   pushFill(envQuit);
}

bool Queue_Locking::pushFill(Envelope &env)
{
   if (env.id == 0) {
      env.id = nextId();
   }
   if (env.posted.time_since_epoch().count() == 0) {
      env.posted = std::chrono::system_clock::now();
   }
   return push(env);
}

bool Queue_Locking::push(const Envelope &env)
{
   std::unique_lock<std::mutex> lock(cvMutex_);
   queue_.push_back(env);
   cvQueue_.notify_one();
   return true;
}

void Queue_Locking::process()
{
   logger_->debug("[Queue::process] started");
   decltype(queue_) deferredQueue;
   auto dqTime = std::chrono::system_clock::now();
   auto accTime = std::chrono::system_clock::now();
   PerfAccounting acc;

   const auto &processPortion = [this, &deferredQueue, &dqTime, &accTime, &acc]
      (const decltype(queue_) &tempQueue, const std::chrono::system_clock::time_point &timeNow)
   {
      std::chrono::time_point<std::chrono::system_clock> procStart;
      for (const auto &env : tempQueue) {
         if (env.executeAt.time_since_epoch().count() != 0) {
            if (env.executeAt > timeNow) {
               deferredQueue.emplace_back(std::move(env));
               continue;
            }
         } else if (accounting_) {
            acc.addQueueTime(std::chrono::duration_cast<std::chrono::microseconds>(timeNow - env.posted));
         }

         if (env.sender->isSystem() && env.receiver->isSystem()) {
            if (env.message == kQuitMessage) {
               logger_->info("[Queue::process] detected quit system message");
               running_ = false;
            } else if (env.message == kAccResetMessage) {
               acc.reset();
               continue;
            } else {
               logger_->warn("[Queue::process] unknown system message {} - skipping"
                  , env.message);
            }
         } else {
            const auto &adapters = router_->process(env);
            if (adapters.empty()) {
               continue;
            } else if (adapters.size() == 1) {
               if (accounting_) {
                  procStart = std::chrono::system_clock::now();
               }
               if (!adapters[0]->process(env)) {
                  deferredQueue.emplace_back(std::move(env));
               }
               if (accounting_) {
                  acc.add(static_cast<int>(env.receiver->value())
                     , std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - procStart));
               }
            } else {
               if (accounting_) {
                  procStart = std::chrono::system_clock::now();
               }
               // Multiple adapters to process a message typically means broadcast. We don't requeue such messages
               // on processing failed, as it's hard to signify the failure: if all adapters return false, or only one.
               // Also semantically broadcasts are not supposed to be re-queued
               for (const auto &adapter : adapters) {
                  adapter->process(env);
               }
               if (accounting_) {
                  acc.add(static_cast<int>(env.receiver->value())
                     , std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - procStart));
               }
            }
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
      const auto &timeNow = std::chrono::system_clock::now();
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
         dqTime = std::chrono::system_clock::now();
         logger_->warn("[Queue::process] deferred queue has grown to {} elements", deferredQueue.size());
      }
      if (accounting_ && ((timeNow - accTime) >= accountingInterval_)) {
         accTime = std::chrono::system_clock::now();
         acc.report(logger_, accMap_);
      }
   }
   if (accounting_) {
      acc.report(logger_, accMap_);
   }
   logger_->debug("[Queue::process] finished");
}

void Queue_Locking::bindAdapter(const std::shared_ptr<Adapter> &adapter)
{
   router_->bindAdapter(adapter);
}

std::set<UserValue> Queue_Locking::supportedReceivers() const
{
   return router_->supportedReceivers();
}
