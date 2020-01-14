/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __THREAD_WORKER_BASE__
#define __THREAD_WORKER_BASE__

#include <QVariant>

template <typename CarrierType, typename ResultType>
class ThreadWorkerData {
public:

   template <typename = std::enable_if<std::is_same<CarrierType, ResultType>::value>::type>
   ThreadWorkerData(std::function<ResultType()> &&threadFunction, std::function<void(ResultType&)> &&resultCallback)
   {
      threadFunction_ = std::move(threadFunction);
      resultCallback_ = std::move(resultCallback);
   }

   template <typename = std::enable_if<std::is_same<CarrierType, QVariant>::value>::type,
      typename = std::enable_if<!std::is_same<CarrierType, ResultType>::value>::type>
   ThreadWorkerData(std::function<ResultType()> &&threadFunction, std::function<void(ResultType&)> &&resultCallback)
   {
      threadFunction_ = [originFunc = std::move(threadFunction)]()->CarrierType {
         QVariant carrier;
         carrier.setValue(originFunc());
         return carrier;
      };
      resultCallback_ = [originFunc = std::move(resultCallback)](const CarrierType& carrier) {
         assert(carrier.canConvert<ResultType>());
         return originFunc(qvariant_cast<ResultType>(carrier));
      };
   }

   std::function<CarrierType()> threadFunction_;
   std::function<void(CarrierType&)> resultCallback_;

};

template <typename ResultType>
class ThreadWorkerBase
{
public:
   ThreadWorkerBase() {}
   virtual ~ThreadWorkerBase() = default;

   virtual void run() = 0;
   virtual void onInitDone() = 0;

   template <typename DataType>
   void setFunctions(ThreadWorkerData<ResultType, DataType>&& workerData) {
      threadFunction_ = std::move(workerData.threadFunction_);
      resultCallback_ = std::move(workerData.resultCallback_);
      onInitDone();
   }

protected:
   std::function<ResultType()> threadFunction_;
   std::function<void(ResultType&)> resultCallback_;
};

#endif // __THREAD_WORKER_BASE__
