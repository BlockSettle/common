#ifndef ServiceThread_h__
#define ServiceThread_h__

#include <QThread>

namespace Chat
{

   template <typename TServiceWorker>
   class ServiceThread : public QThread
   {
   public:
      explicit ServiceThread(std::unique_ptr<TServiceWorker> worker, QObject* parent = nullptr)
         : QThread(parent), worker_(std::move(worker))
      {
         worker_->moveToThread(this);
         start();
      }

      ~ServiceThread()
      {
         quit();
         wait();
      }

      TServiceWorker* worker() const
      {
         return worker_.get();
      }

   private:
      std::unique_ptr<TServiceWorker> worker_;
   };

}
#endif // ServiceThread_h__
