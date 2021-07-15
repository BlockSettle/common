/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef MESSAGE_ENVELOPE_H
#define MESSAGE_ENVELOPE_H

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace bs {
   namespace message {
      using UserValue = int;
      using bus_clock = std::chrono::steady_clock;

      static_assert(bus_clock::is_steady, "Should be steady clock");
      static_assert(bus_clock::period::den >= 10000, "Should be at least ms");

      using TimeStamp = std::chrono::time_point<bus_clock>;

      class User
      {
      public:
         virtual ~User() = default;
         User(UserValue value) : value_(value) {}
         UserValue value() const { return value_; }
         template<typename T> T value() const { return static_cast<T>(value_); }

         virtual std::string name() const { return std::to_string(value_); }
         virtual bool isSystem() const { return false; }
         virtual bool isSupervisor() const { return false; }
         virtual bool isBroadcast() const { return false; }
         virtual bool isFallback() const { return false; }

      private:
         UserValue   value_;
      };


      class UserSystem : public User
      {
      public:
         UserSystem() : User(0) {}

         std::string name() const override { return "System"; }
         bool isSystem() const override { return true; }
      };

      class UserSupervisor : public User
      {
      public:
         UserSupervisor() : User(0) {}

         std::string name() const override { return "Supervisor"; }
         bool isSupervisor() const override { return true; }
      };

      class UserFallback : public User
      {
      public:
         UserFallback() : User(0) {}

         std::string name() const override { return "Fallback"; }
         bool isFallback() const override { return true; }
      };


      using SeqId = uint64_t;

      enum class EnvelopeType : SeqId
      {
         GlobalBroadcast = UINT64_MAX,
         Publish = UINT64_MAX - 1,           // response to subscription request
         Update = UINT64_MAX - 2,            // message from one adapter to another that does not require subscriptions and is not a request
         Processed = UINT64_MAX - 3,         // mark message as processed to prevent infinite broadcast loop
         MinValue = UINT64_MAX - 15          // all values above should be treated as envelope type values only
      };

      struct Envelope
      {
         Envelope() = default;
         ~Envelope() noexcept = default;

         static Envelope makeRequest(const std::shared_ptr<User>& s, const std::shared_ptr<User>& r
            , const std::string& msg, const TimeStamp& execAt = {})
         {
            return Envelope{ s, r, execAt, msg };
         }

         static Envelope makeResponse(const std::shared_ptr<User>& s, const std::shared_ptr<User>& r
            , const std::string& msg, SeqId respId)
         {
            return Envelope{ s, r, msg, respId };
         }

         static Envelope makeBroadcast(const std::shared_ptr<User>& s, const std::string& msg, bool global = false)
         {
            return Envelope{ s, nullptr, msg, global ? (SeqId)EnvelopeType::GlobalBroadcast : 0 };
         }

         Envelope& operator=(Envelope other)
         {
            sender = other.sender;
            receiver = other.receiver;
            posted = other.posted;
            executeAt = other.executeAt;
            message = other.message;
            id_ = 0;
            foreignId_ = other.foreignId_;
            responseId_ = other.responseId_;
            return *this;
         }

         SeqId id() const { return id_; }
         void setId(SeqId id)
         {
            id_ = id;
            if (!foreignId_) {
               foreignId_ = id;
            }
         }

         SeqId foreignId() const { return foreignId_; }
         void setForeignId(SeqId id) { foreignId_ = id; }

         SeqId responseId() const
         {
            if (responseId_ >= static_cast<SeqId>(EnvelopeType::MinValue)) {
               return 0;
            }
            return responseId_;
         }

         void resetEnvelopeType() { responseId_ = 0; }

         void setEnvelopeType(const EnvelopeType f) { responseId_ = (SeqId)f; }


         EnvelopeType envelopeType() const
         {
            if (responseId_ < static_cast<SeqId>(EnvelopeType::MinValue)) {
               return EnvelopeType::MinValue;
            }
            return static_cast<EnvelopeType>(responseId_);
         }

         bool isRequest() const { return (responseId_ == 0); }

         std::shared_ptr<User>   sender;
         std::shared_ptr<User>   receiver;
         TimeStamp   posted;
         TimeStamp   executeAt;
         std::string message;

      private:
         Envelope(const std::shared_ptr<User>& s, const std::shared_ptr<User>& r
            , const std::string& msg, SeqId respId = 0)
            : sender(s), receiver(r), message(msg), responseId_(respId)
         {}
         Envelope(const std::shared_ptr<User>& s, const std::shared_ptr<User>& r
            , const TimeStamp& execAt, const std::string& msg, SeqId respId = 0)
            : sender(s), receiver(r), executeAt(execAt), message(msg)
            , responseId_(respId)
         {}

         SeqId id_{ 0 };         // always unique and growing (no 2 envelopes can have the same id)
         SeqId foreignId_{ 0 };  // used at gatewaying from external bus
         SeqId responseId_{ 0 }; // should be set in reply and for special values of EnvelopeType
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_ENVELOPE_H
