/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Message/ThreadedAdapter.h"
#include "FastLock.h"

using namespace bs::message;

ThreadedAdapter::ThreadedAdapter()
{
   // start thread
   processingThread_ = std::thread(&ThreadedAdapter::processingRoutine, this);
}

ThreadedAdapter::~ThreadedAdapter() noexcept
{
   stop();
}

bool ThreadedAdapter::process(const Envelope &envelope)
{
   SendEnvelopeToThread(envelope);
   return true;
}

void ThreadedAdapter::stop()
{
   continueExecution_ = false;
   decltype(pendingEnvelopes_) cleanQueue;
   pendingEnvelopes_.swap(cleanQueue);
   pendingEnvelopesEvent_.SetEvent();
   if (processingThread_.joinable()) {
      processingThread_.join();
   }
}

void ThreadedAdapter::processingRoutine()
{
   while (continueExecution_) {
      pendingEnvelopesEvent_.WaitForEvent();

      if (!continueExecution_) {
         break;
      }

      std::shared_ptr<Envelope> envelope;

      {
         FastLock locker{pendingEnvelopesLock_};

         if (!pendingEnvelopes_.empty()) {
            envelope = pendingEnvelopes_.front();
            pendingEnvelopes_.pop();
         }

         if (pendingEnvelopes_.empty()) {
            pendingEnvelopesEvent_.ResetEvent();
         }
      }

      if (envelope == nullptr) {
         continue;
      }
      if (!processEnvelope(*envelope)) {
         FastLock locker{ pendingEnvelopesLock_ };
         pendingEnvelopes_.emplace(envelope);
      }
   }
}

void ThreadedAdapter::SendEnvelopeToThread(const Envelope &envelope)
{
   FastLock locker{pendingEnvelopesLock_};
   pendingEnvelopes_.emplace(std::make_shared<Envelope>(envelope));
   pendingEnvelopesEvent_.SetEvent();
}
