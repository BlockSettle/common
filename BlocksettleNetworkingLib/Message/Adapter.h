/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef MESSAGE_ADAPTER_H
#define MESSAGE_ADAPTER_H

#include <memory>
#include <set>
#include "Message/Envelope.h"


namespace bs {
   namespace message {
      class QueueInterface;

      class Adapter
      {
      public:
         using Users = std::set<std::shared_ptr<User>>;

         virtual ~Adapter() = default;

         // if false is returned, the message is automatically pushed back
         virtual bool process(const Envelope &) = 0;

         // if false is returned, it's not counted in processing stats (broadcast will never return back anyway)
         virtual bool processBroadcast(const Envelope&) = 0;

         virtual Users supportedReceivers() const = 0;
         virtual std::string name() const = 0;

         virtual void setQueue(const std::shared_ptr<QueueInterface> &queue)
         {
            queue_ = queue;
         }

      protected:
         virtual bool pushFill(Envelope &);

         SeqId pushRequest(const std::shared_ptr<User>& sender
            , const std::shared_ptr<User>& receiver
            , const std::string& msg, const TimeStamp& execAt = {});
         SeqId pushResponse(const std::shared_ptr<User>& sender
            , const std::shared_ptr<User>& receiver
            , const std::string& msg, SeqId respId =
            (bs::message::SeqId)bs::message::EnvelopeType::Update);
         virtual SeqId pushResponse(const std::shared_ptr<User>& sender
            , const bs::message::Envelope& envReq, const std::string& msg);
         SeqId pushBroadcast(const std::shared_ptr<User>& sender
            , const std::string& msg, bool global = false);

      protected:
         std::shared_ptr<QueueInterface>  queue_;
      };

      class RelayAdapter : public Adapter
      {
      public:
         RelayAdapter() {}
         RelayAdapter(const std::shared_ptr<User> &user)
            : fallbackUser_(user) {}

         Users supportedReceivers() const override;
         std::string name() const override { return "Relay"; }

         void setQueue(const std::shared_ptr<QueueInterface>&) override;

      protected:
         bool process(const Envelope &) override;
         bool processBroadcast(const Envelope&) override;
         bool relay(const Envelope&);
         bool isInitialized() const;

      protected:
         std::shared_ptr<User>   fallbackUser_;

      private:
         std::map<UserValue, std::shared_ptr<QueueInterface>>  queueByUser_;
         std::set<std::shared_ptr<QueueInterface>>             queues_;
      };

      class PipeAdapter : public Adapter
      {
      public:
         PipeAdapter() {}
         PipeAdapter(const std::shared_ptr<PipeAdapter>& endpoint)
            : endpoint_(endpoint) {}

         void setEndpoint(const std::shared_ptr<PipeAdapter>& endpoint) {
            endpoint_ = endpoint;
         }

      protected:
         bool process(const Envelope&) override;
         bool processBroadcast(const Envelope&) override;

      private:
         std::shared_ptr<PipeAdapter>  endpoint_;
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_ADAPTER_H
