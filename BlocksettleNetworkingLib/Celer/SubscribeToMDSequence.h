#ifndef __CELER_SUBSCRIBE_TO_MD_H__
#define __CELER_SUBSCRIBE_TO_MD_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <string>
#include <functional>
#include <memory>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class SubscribeToMDSequence : public CommandSequence<SubscribeToMDSequence>
      {
      public:
         SubscribeToMDSequence(const std::string& currencyPair
            , bs::network::Asset::Type at, const std::shared_ptr<spdlog::logger>& logger);
         ~SubscribeToMDSequence() noexcept = default;

         SubscribeToMDSequence(const SubscribeToMDSequence&) = delete;
         SubscribeToMDSequence& operator = (const SubscribeToMDSequence&) = delete;

         SubscribeToMDSequence(SubscribeToMDSequence&&) = delete;
         SubscribeToMDSequence& operator = (SubscribeToMDSequence&&) = delete;

         bool FinishSequence() override;
         const std::string getReqId() const { return reqId_; }

      private:
         CelerMessage subscribeToMD();

         std::string currencyPair_;
         bs::network::Asset::Type assetType_;
         std::string reqId_;
         std::shared_ptr<spdlog::logger> logger_;
      };
   }  //namespace celer
}  //namespace bs

#endif // __CELER_SUBSCRIBE_TO_MD_H__
