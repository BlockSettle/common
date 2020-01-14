/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __SAME_THREAD_WORKER__
#define __SAME_THREAD_WORKER__

#include "ThreadWorkerBase.h"

template <typename ResultType>
class SameThreadWorker : public ThreadWorkerBase<ResultType>
{
public:
   SameThreadWorker(): ThreadWorkerBase<ResultType>() {}
   ~SameThreadWorker() override = default;

   void run() override {
      assert(threadFunction_ && resultCallback_);

      ResultType result = threadFunction_();
      resultCallback_(result);
   }
   void onInitDone() override {};
};

#include "SameThreadWorker.cpp"

#endif // __SAME_THREAD_WORKER__
