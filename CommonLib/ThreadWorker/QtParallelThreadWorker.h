/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __QT_PARALLEL_THREAD_WORKER__
#define __QT_PARALLEL_THREAD_WORKER__

#include <QThread>
#include <QVariant>
#include "ThreadWorkerBase.h"

class WorkerObject : public QObject {
   Q_OBJECT
public:
   WorkerObject(std::function<QVariant()> &&workerFunction, QObject *parent = nullptr);

public slots:
   void onProcess();

signals:
   void done(QVariant res);

private:
   std::function<QVariant()> workerFunction_;
};

class QtParallelThreadWorker : public QObject, public ThreadWorkerBase<QVariant>
{
   Q_OBJECT
public:
   QtParallelThreadWorker(QObject *parent = nullptr);
   ~QtParallelThreadWorker() override;

   void run() override;
   void onInitDone() override;

private slots:
   void onResultReady(QVariant result);

signals:
   void executeThreadFunction();

private:
   QThread workerThread_;
};

#endif // __QT_PARALLEL_THREAD_WORKER__
