#include "PerfAccounting.h"
#include <spdlog/spdlog.h>

using namespace bs::message;

static const int kQueueTime{ -1 };
static const std::string kQTname{ "Queue time" };

void PerfAccounting::Entry::add(const std::chrono::microseconds &interval)
{
   if (!count_ || (interval < min_)) {
      min_ = interval;
   }
   if (!count_ || (interval > max_)) {
      max_ = interval;
   }
   total_ += interval;
   count_++;
}

void PerfAccounting::Entry::reset()
{
   count_ = 0;
   min_.zero();
   max_.zero();
   total_.zero();
}

void PerfAccounting::add(int key, const std::chrono::microseconds &interval)
{
   entries_[key].add(interval);
}

void PerfAccounting::addQueueTime(const std::chrono::microseconds &interval)
{
   add(kQueueTime, interval);
}

void PerfAccounting::reset()
{
   for (auto &entry : entries_) {
      entry.second.reset();
   }
}

void PerfAccounting::report(const std::shared_ptr<spdlog::logger> &logger
   , const std::map<int, std::string> &keyMapping)
{
   std::string output;
   std::string name;
   for (const auto &entry : entries_) {
      if (entry.first == kQueueTime) {
         name = kQTname;
      }
      else {
         const auto itMapping = keyMapping.find(entry.first);
         name = (itMapping == keyMapping.end())
            ? std::to_string(entry.first) : itMapping->second;
      }
      output += fmt::format("\n\t{}:\t{:.3f} / {:.3f} / {:.3f}\t{}", name
         , entry.second.min(), entry.second.avg(), entry.second.max()
         , entry.second.count());
   }
   logger->info("Performance accounting info 'min/avg/max count' expressed in "
      "milliseconds:{}", output);
}
