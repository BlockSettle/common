#ifndef __MANUAL_RESET_EVENT_H__
#define __MANUAL_RESET_EVENT_H__

#include <atomic>
#include <condition_variable>
#include <mutex>

class ManualResetEvent
{
public:
   ManualResetEvent();
   ~ManualResetEvent() noexcept = default;

   ManualResetEvent(const ManualResetEvent&) = delete;
   ManualResetEvent& operator = (const ManualResetEvent&) = delete;

   ManualResetEvent(ManualResetEvent&&) = delete;
   ManualResetEvent& operator = (ManualResetEvent&&) = delete;

   bool WaitForEvent(std::chrono::milliseconds period);
   void WaitForEvent();

   void SetEvent();
   void ResetEvent();

private:
   std::condition_variable event_;
   mutable std::mutex      flagMutex_;
   std::atomic<bool>       eventFlag_;
};

#endif // __MANUAL_RESET_EVENT_H__
