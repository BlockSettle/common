/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
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

         virtual Users supportedReceivers() const = 0;
         virtual std::string name() const = 0;

         virtual void setQueue(const std::shared_ptr<QueueInterface> &queue) {
            queue_ = queue;
         }

      protected:
         virtual bool push(const Envelope &);
         virtual bool pushFill(Envelope &);

      protected:
         std::shared_ptr<QueueInterface>  queue_;
      };

      class PipeAdapter : public Adapter
      {
      public:
         PipeAdapter() {}
         PipeAdapter(const std::shared_ptr<PipeAdapter> &endpoint)
            : endpoint_(endpoint) {}

         void setEndpoint(const std::shared_ptr<PipeAdapter> &endpoint) {
            endpoint_ = endpoint;
         }

      protected:
         bool process(const Envelope &) override;

      private:
         std::shared_ptr<PipeAdapter>  endpoint_;
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_ADAPTER_H
