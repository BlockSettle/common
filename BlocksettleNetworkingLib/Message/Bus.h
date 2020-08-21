/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef MESSAGE_BUS_H
#define MESSAGE_BUS_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include "Message/Envelope.h"

namespace spdlog {
   class logger;
}

namespace bs {
   namespace message {
      class Adapter;

      class RouterInterface
      {
      public:
         virtual ~RouterInterface() = default;
         virtual void bindAdapter(const std::shared_ptr<Adapter> &) = 0;
         virtual std::vector<std::shared_ptr<bs::message::Adapter>> process(const Envelope &) const = 0;
         virtual void reset() = 0;
         virtual std::set<UserValue> supportedReceivers() const = 0;
      };

      class Router : public RouterInterface
      {
      public:
         Router(const std::shared_ptr<spdlog::logger> &);

         void bindAdapter(const std::shared_ptr<Adapter> &) override;
         std::set<UserValue> supportedReceivers() const override;
         std::vector<std::shared_ptr<bs::message::Adapter>> process(const Envelope &) const override;
         void reset() override;

      private:
         std::shared_ptr<spdlog::logger>           logger_;
         std::map<UserValue, std::shared_ptr<Adapter>>   adapters_;
         std::shared_ptr<Adapter>   supervisor_;
         std::shared_ptr<Adapter>   defaultRoute_;
      };

      class QueueInterface
      {
      public:
         QueueInterface(const std::shared_ptr<RouterInterface> &router)
            : router_(router) {}
         virtual ~QueueInterface() = default;

         virtual void terminate() = 0;
         virtual void bindAdapter(const std::shared_ptr<Adapter> &) = 0;
         virtual std::set<UserValue> supportedReceivers() const = 0;

         virtual bool pushFill(Envelope &) = 0;
         virtual bool push(const Envelope &) = 0;
         uint64_t nextId() { return seqNo_++; }

      protected:
         std::shared_ptr<RouterInterface> router_;
         std::atomic<uint64_t>   seqNo_{ 1 };
      };

      class Queue_Locking : public QueueInterface
      {
      public:
         Queue_Locking(const std::shared_ptr<RouterInterface> &, const std::shared_ptr<spdlog::logger> &
            , const std::map<int, std::string> & = {}, bool accounting = true);
         ~Queue_Locking() override;

         void terminate() override;
         void bindAdapter(const std::shared_ptr<Adapter> &) override;
         std::set<UserValue> supportedReceivers() const override;

         bool pushFill(Envelope &) override;
         bool push(const Envelope &) override;

      private:
         void stop();
         void process();

      private:
         std::shared_ptr<spdlog::logger>  logger_;
         const std::map<int, std::string> accMap_;
         const bool accounting_;
         const std::chrono::seconds deferredQueueInterval_{ 30 };
         const std::chrono::seconds accountingInterval_{ 600 };
         std::deque<Envelope>    queue_;
         std::condition_variable cvQueue_;
         std::mutex              cvMutex_;
         std::atomic_bool        running_{ true };
         std::thread             thread_;
      };
      using Queue = Queue_Locking;    // temporary hack to avoid name clashing with ThreadSafeClasses.h


      class Bus
      {
      public:
         virtual ~Bus() = default;

         virtual void addAdapter(const std::shared_ptr<Adapter> &) = 0;

         virtual void SetCommunicationDumpEnabled(bool) {};
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_BUS_H
