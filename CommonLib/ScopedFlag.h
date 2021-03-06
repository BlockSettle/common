/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __SCOPED_FLAG_H__
#define __SCOPED_FLAG_H__

#include <type_traits>

template<typename BooleanType>
class ScopedFlag
{
public:
   ScopedFlag() = delete;
   explicit ScopedFlag(BooleanType& flag)
    : flag_{flag}
   {
      flag_ = true;
   }

   ~ScopedFlag() noexcept
   {
      flag_ = false;
   }

   ScopedFlag(const ScopedFlag&) = delete;
   ScopedFlag& operator = (const ScopedFlag&) = delete;

   ScopedFlag(ScopedFlag&&) = delete;
   ScopedFlag& operator = (ScopedFlag&&) = delete;
private:
   BooleanType& flag_;
};

#endif // __SCOPED_FLAG_H__
