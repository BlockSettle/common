/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __MESSAGE_THREADED_ADAPTER_H__
#define __MESSAGE_THREADED_ADAPTER_H__

#include "Message/Adapter.h"
#include "ManualResetEvent.h"

#include <atomic>
#include <queue>
#include <thread>

namespace bs {
   namespace message {
      class ThreadedAdapter : public Adapter
      {
      public:
         ThreadedAdapter();
         ~ThreadedAdapter() noexcept override;

         ThreadedAdapter(const ThreadedAdapter&) = delete;
         ThreadedAdapter& operator = (const ThreadedAdapter&) = delete;

         ThreadedAdapter(ThreadedAdapter&&) = delete;
         ThreadedAdapter& operator = (ThreadedAdapter&&) = delete;

         bool process(const Envelope &) final;

      protected:
         virtual bool processEnvelope(const Envelope &) = 0;

      private:
         void processingRoutine();
         void SendEnvelopeToThread(const Envelope &envelope);

      private:
         std::thread processingThread_;

         std::atomic_bool                       continueExecution_ { true };
         mutable std::atomic_flag               pendingEnvelopesLock_ = ATOMIC_FLAG_INIT;
         ManualResetEvent                       pendingEnvelopesEvent_;
         std::queue<std::shared_ptr<Envelope>>  pendingEnvelopes_;
      };
   }
}

#endif // __MESSAGE_THREADED_ADAPTER_H__
