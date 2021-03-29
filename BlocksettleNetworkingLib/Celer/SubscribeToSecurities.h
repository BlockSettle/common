#ifndef __CELER_SUBSCRIBE_TO_SECURITIES_H__
#define __CELER_SUBSCRIBE_TO_SECURITIES_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <functional>
#include <unordered_map>
#include <memory>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {

      class SubscribeToSecurities : public CommandSequence<SubscribeToSecurities>
      {
      public:
         typedef std::unordered_map<std::string, bs::network::SecurityDef>   Securities;
         using onSecuritiesSnapshotReceived = std::function<void(const SubscribeToSecurities::Securities &)>;

      public:
         SubscribeToSecurities(const std::shared_ptr<spdlog::logger>& logger
            , const onSecuritiesSnapshotReceived &);
         ~SubscribeToSecurities() noexcept = default;

         SubscribeToSecurities(const SubscribeToSecurities&) = delete;
         SubscribeToSecurities& operator = (const SubscribeToSecurities&) = delete;

         SubscribeToSecurities(SubscribeToSecurities&&) = delete;
         SubscribeToSecurities& operator = (SubscribeToSecurities&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage subscribeFX();
         bool process(const CelerMessage& message);

      private:
         std::shared_ptr<spdlog::logger>     logger_;
         const onSecuritiesSnapshotReceived  onSnapshotReceived_;
         Securities                          dictionary_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_SUBSCRIBE_TO_SECURITIES_H__
