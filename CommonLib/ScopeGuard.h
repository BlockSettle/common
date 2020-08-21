/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#ifndef SCOPED_GUARD_H
#define SCOPED_GUARD_H

#include <functional>

class ScopedGuard
{
public:
   explicit ScopedGuard(std::function<void()> f)
      : f_(std::move(f))
   {
   }

   ~ScopedGuard()
   {
      f_();
   }

   std::function<void()> releaseCb() {
      auto ret = std::move(f_);
      f_ = []{};
      return std::move(ret);
   }

private:
   std::function<void()> f_;

};

#endif
