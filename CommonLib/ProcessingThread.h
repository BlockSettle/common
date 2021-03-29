/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#ifndef __PROCESSING_THREAD_H__
#define __PROCESSING_THREAD_H__

#include <atomic>
#include <memory>
#include <queue>
#include <thread>
#include <type_traits>

#include "FastLock.h"
#include "ManualResetEvent.h"

template <typename T>
class ProcessingThread
{
   static_assert(std::is_copy_constructible<T>::value, "ProcessingThread packet type should be copy constructable");
public:
   ProcessingThread()
   {
      processingThread_ = std::thread(&ProcessingThread<T>::processingLoop, this);

   }

   virtual ~ProcessingThread() noexcept
   {
      haltProcessing();
      continueExecution_ = false;
      pendingPacketsEvent_.SetEvent();
      if (processingThread_.joinable()) {
         processingThread_.join();
      }
   }

   ProcessingThread(const ProcessingThread&) = delete;
   ProcessingThread& operator = (const ProcessingThread&) = delete;

   ProcessingThread(ProcessingThread&&) = delete;
   ProcessingThread& operator = (ProcessingThread&&) = delete;

   void SchedulePacketProcessing(const T& packet)
   {
      if (!processingHalted_) {
         FastLock locker{pendingPacketsLock_};
         pendingPackets_.emplace(std::make_shared<T>(packet));
         pendingPacketsEvent_.SetEvent();
      }
   }

   virtual void processPacket(const T& packet) = 0;

   void haltProcessing()
   {
      processingHalted_ = true;
      cleanQueue();
   }

   void continueProcessing()
   {
      processingHalted_ = false;
   }

private:
   void cleanQueue()
   {
      FastLock locker{pendingPacketsLock_};
      decltype(pendingPackets_) cleanQueue;
      pendingPackets_.swap(cleanQueue);
   }

private:
   void processingLoop()
   {
      while (continueExecution_) {
         pendingPacketsEvent_.WaitForEvent();

         if (!continueExecution_) {
            break;
         }

         std::shared_ptr<T> packet;

         {
            FastLock locker{pendingPacketsLock_};

            if (!pendingPackets_.empty()) {
               packet = pendingPackets_.front();
               pendingPackets_.pop();
            }

            if (pendingPackets_.empty()) {
               pendingPacketsEvent_.ResetEvent();
            }
         }

         if (packet == nullptr) {
            continue;
         }
         processPacket(*packet);
      }
   }

private:
   std::thread processingThread_;
   std::atomic_bool                       continueExecution_{ true };
   std::atomic_bool                       processingHalted_{ false };
   mutable std::atomic_flag               pendingPacketsLock_ = ATOMIC_FLAG_INIT;
   ManualResetEvent                       pendingPacketsEvent_;
   std::queue<std::shared_ptr<T>>         pendingPackets_;
};

#endif // __PROCESSING_THREAD_H__
