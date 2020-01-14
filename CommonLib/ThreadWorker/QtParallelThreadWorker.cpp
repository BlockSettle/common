/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#include "QtParallelThreadWorker.h"

WorkerObject::WorkerObject(std::function<QVariant()> &&workerFunction, QObject *parent /*= nullptr*/)
   : QObject(parent)
   , workerFunction_(std::move(workerFunction))
{
}

void WorkerObject::onProcess()
{
   QVariant res = workerFunction_();
   emit done(res);
}

QtParallelThreadWorker::QtParallelThreadWorker(QObject *parent /*= nullptr*/)
   : QObject(parent)
   , ThreadWorkerBase<QVariant>()
{
}

QtParallelThreadWorker::~QtParallelThreadWorker()
{
   workerThread_.quit();
   workerThread_.wait();
}

void QtParallelThreadWorker::run()
{
   workerThread_.start();
   emit executeThreadFunction();
}

void QtParallelThreadWorker::onInitDone()
{
   auto *workerObject = new WorkerObject(std::move(threadFunction_), this);
   workerObject->moveToThread(&workerThread_);

   connect(workerObject, &WorkerObject::done,
      this, &QtParallelThreadWorker::onResultReady, Qt::QueuedConnection);
   connect(this, &QtParallelThreadWorker::executeThreadFunction,
      workerObject, &WorkerObject::onProcess, Qt::QueuedConnection);
   connect(&workerThread_, &QThread::finished,
      workerObject, &QObject::deleteLater, Qt::QueuedConnection);
}

void QtParallelThreadWorker::onResultReady(QVariant result)
{
   resultCallback_(result);
}
