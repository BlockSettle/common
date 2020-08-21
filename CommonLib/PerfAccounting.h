#ifndef PERF_ACCOUNTING_H
#define PERF_ACCOUNTING_H

#include <chrono>
#include <map>
#include <memory>

namespace spdlog {
   class logger;
}

namespace bs {
   namespace message {
      class PerfAccounting
      {
      public:
         void add(int key, const std::chrono::microseconds &interval);
         void addQueueTime(const std::chrono::microseconds &interval);
         void reset();

         void report(const std::shared_ptr<spdlog::logger> &
            , const std::map<int, std::string> &keyMapping);

      private:
         class Entry
         {
         public:
            void add(const std::chrono::microseconds &interval);
            void reset();

            double min() const { return min_.count() / 1000.0; }
            double max() const { return max_.count() / 1000.0; }
            double avg() const { return total_.count() / 1000.0 / count_; }
            size_t count() const { return count_; }

         private:
            size_t   count_{ 0 };
            std::chrono::microseconds  total_;
            std::chrono::microseconds  min_;
            std::chrono::microseconds  max_;
         };

         std::map<int, Entry> entries_;
      };
   } // namespace message
} // namespace bs

#endif	// PERF_ACCOUNTING_H
