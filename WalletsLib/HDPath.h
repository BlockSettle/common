/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_CORE_HD_PATH_H
#define BS_CORE_HD_PATH_H

#include <string>
#include <set>
#include <vector>
#include "Addresses.h"


namespace bs {
   namespace hd {

      class PathException : public std::runtime_error
      {
      public:
         PathException(const std::string &s) : std::runtime_error(s) {}
      };

      class Path
      {
      public:
         using Elem = uint32_t;

         Path() {}
         Path(const std::vector<Elem> &elems);

         bool operator==(const Path &other) const {
            return (path_ == other.path_);
         }
         bool operator!=(const Path &other) const {
            return (path_ != other.path_);
         }
         bool operator < (const Path &other) const;

         void append(Elem elem);
         void append(const std::string &key);
         void append(const Path& childPath);
         void pop();

         size_t length() const { return path_.size(); }
         std::vector<Elem> toVector() const { return path_; }
         Elem get(int index) const;   // negative index is an offset from end
         void clear();
         bool isAbsolute() const { return isAbsolute_; }
         int priority() const;

         std::string toString() const;

         bool isHardened(size_t index) const;
         void setHardened(size_t index, bool value = true);

         std::vector<Elem>::const_iterator begin() const;
         std::vector<Elem>::const_iterator end() const;

         static Path fromString(const std::string &);
         static Elem keyToElem(const std::string &key);
         static std::string elemToKey(Elem);

      private:
         std::vector<Elem> path_;
         bool isAbsolute_ = false;
      };


      enum Purpose : Path::Elem {
         Native = 84,   // BIP84
         Nested = 49,   // BIP49
         NonSegWit = 44 // BIP44
      };

      Purpose purpose(AddressEntryType);
      AddressEntryType addressType(Path::Elem);

      static const Path::Elem hardFlag = 0x80000000;

      enum CoinType : Path::Elem {
         Bitcoin_main = hardFlag,
         Bitcoin_test = hardFlag + 1,
         BlockSettle_CC = hardFlag + 0x4253, // 0x80000000 | "BS" in hex
         BlockSettle_Auth = hardFlag + 0x41757468,  // 0x80000000 | "Auth" in hex
         Blocksettle_Sign = hardFlag + 0x5369676e, // Sign in hex

         //this is a place holder for the Group ctor, settlement accounts
         //are not deterministic
         BlockSettle_Settlement = 0xdeadbeef
      };

   }  //namespace hd
}  //namespace bs

#endif //BS_CORE_HD_PATH_H
