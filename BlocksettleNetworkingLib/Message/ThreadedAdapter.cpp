/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
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

bool bs::message::ThreadedAdapter::processBroadcast(const Envelope& env)
{
   SendEnvelopeToThread(env);
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
   decltype(pendingEnvelopes_) defferedEnvelopes;
   bool lastEnvelope = false;
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
            pendingEnvelopes_.pop_front();
         }

         if (pendingEnvelopes_.empty()) {
            pendingEnvelopesEvent_.ResetEvent();
            lastEnvelope = true;
         }
      }

      if (envelope == nullptr) {
         continue;
      }

      if (!processEnvelope(*envelope)) {
         defferedEnvelopes.emplace_back(envelope);
      }
      if (lastEnvelope) {
         if (!defferedEnvelopes.empty()) {
            decltype(defferedEnvelopes) tempQ;
            tempQ.swap(defferedEnvelopes);

            for (const auto& env : tempQ) {
               if (!processEnvelope(*env)) {
                  defferedEnvelopes.emplace_back(env);
               }
            }
         }
         lastEnvelope = false;
      }
   }
}

void ThreadedAdapter::SendEnvelopeToThread(const Envelope &envelope)
{
   FastLock locker{pendingEnvelopesLock_};
   pendingEnvelopes_.emplace_back(std::make_shared<Envelope>(envelope));
   pendingEnvelopesEvent_.SetEvent();
}
