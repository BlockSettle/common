/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
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
      using TimeStamp = std::chrono::time_point<std::chrono::system_clock>;

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


      struct Envelope
      {
         uint64_t id;
         std::shared_ptr<User>   sender;
         std::shared_ptr<User>   receiver;
         TimeStamp   posted;
         TimeStamp   executeAt;
         std::string message;
         bool  request{ false };
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_ENVELOPE_H
