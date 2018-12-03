#include <chrono>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include "CacheFile.h"

// We need at least 100 MiB on the drive in order for LMDB to work.
#define DBMAPSIZE 100000000

CacheFile::CacheFile(const std::string &filename, size_t nbElemLimit)
   : QObject(nullptr)
   , nbMaxElems_(nbElemLimit)
   , stopped_(false)
   , threadPool_(this)
   , saveTimer_(this)
{
   dbEnv_ = std::make_shared<LMDBEnv>();
   dbEnv_->open(filename);
   dbEnv_->setMapSize(DBMAPSIZE);
   db_ = new LMDB(dbEnv_.get(), "cache");

   threadPool_.setMaxThreadCount(1);
   read();
   QtConcurrent::run(&threadPool_, this, &CacheFile::saver);

   saveTimer_.setInterval(123 * 1000);
   connect(&saveTimer_, &QTimer::timeout, [this] {
      wcModified_.wakeOne();
   });
   saveTimer_.start();
}

CacheFile::~CacheFile()
{
   stop();

   if (db_) {
      db_->close();
      dbEnv_->close();
      delete db_;
      db_ = nullptr;
   }
}

void CacheFile::stop()
{
   stopped_ = true;
   {
      QMutexLocker lock(&mtxModified_);
      wcModified_.wakeAll();
   }
   threadPool_.clear();
   threadPool_.waitForDone();
}

#define DB_PREFIX    0xDC

void CacheFile::read()
{
   LMDBEnv::Transaction tx(dbEnv_.get(), LMDB::ReadOnly);
   auto dbIter = db_->begin();

   BinaryWriter bwKey;
   bwKey.put_uint8_t(DB_PREFIX);
   CharacterArrayRef keyRef(bwKey.getSize(), bwKey.getData().getPtr());

   dbIter.seek(keyRef, LMDB::Iterator::Seek_GE);
   QWriteLocker lock(&rwLock_);

   while (dbIter.isValid()) {
      auto iterkey = dbIter.key();
      auto itervalue = dbIter.value();

      BinaryDataRef keyBDR((uint8_t*)iterkey.mv_data, iterkey.mv_size);
      BinaryDataRef valueBDR((uint8_t*)itervalue.mv_data, itervalue.mv_size);

      if (keyBDR.getSize() < 2) {
         break;
      }
      BinaryRefReader brrKey(keyBDR);
      auto prefix = brrKey.get_uint8_t();
      if (prefix != DB_PREFIX) {
         break;
      }
      const BinaryData key(brrKey.getCurrPtr(), brrKey.getSizeRemaining());

      BinaryRefReader brrVal(valueBDR);
      const BinaryData value(brrVal.getCurrPtr(), brrVal.getSizeRemaining());
      if (!key.isNull()) {
         map_.emplace(key, value);
      }

      dbIter.advance();
   }
}

void CacheFile::write()
{
   QReadLocker lockMap(&rwLock_);
   LMDBEnv::Transaction tx(dbEnv_.get(), LMDB::ReadWrite);
   QMutexLocker lockMapModif(&mtxModified_);
   for (const auto &entry : mapModified_) {
      BinaryWriter bwKey;
      bwKey.put_uint8_t(DB_PREFIX);
      bwKey.put_BinaryData(entry.first);

      CharacterArrayRef keyRef(bwKey.getData().getSize(), bwKey.getData().getPtr());
      CharacterArrayRef dataRef(entry.second.getSize(), entry.second.getPtr());
      db_->insert(keyRef, dataRef);
      map_[entry.first] = entry.second;
   }
   mapModified_.clear();
}

void CacheFile::saver()
{
   const std::chrono::duration<double> minSaveDuration(23.0);
   size_t nbElemsThreshold = 100;
   auto start = std::chrono::system_clock::now();

   while (!stopped_) {
      purge();

      if (!stopped_) {
         QMutexLocker lock(&mtxModified_);
         wcModified_.wait(&mtxModified_);
      }

      if (stopped_ || mapModified_.empty()) {
         continue;
      }
      auto curTime = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = curTime - start;
      if ((diff < minSaveDuration) && (mapModified_.size() < nbElemsThreshold)) {
         continue;
      }

      write();
      start = curTime;
   }
   write();    // final flush
}

void CacheFile::purge()
{  // simple purge - without LRU/MRU counters
   {
      QReadLocker lock(&rwLock_);
      if (map_.size() < nbMaxElems_) {
         return;
      }
   }
   QWriteLocker lock(&rwLock_);
   LMDBEnv::Transaction tx(dbEnv_.get(), LMDB::ReadWrite);
   while (!stopped_ && (map_.size() >= nbMaxElems_)) {
      const auto entry = map_.begin();
      BinaryWriter bwKey;
      bwKey.put_uint8_t(DB_PREFIX);
      bwKey.put_BinaryData(entry->first);

      CharacterArrayRef keyRef(bwKey.getData().getSize(), bwKey.getData().getPtr());
      db_->erase(keyRef);
      map_.erase(entry->first);
   }
}

BinaryData CacheFile::get(const BinaryData &key) const
{
   QReadLocker lockMap(&rwLock_);
   auto it = map_.find(key);
   if (it == map_.end()) {
      QMutexLocker lockMapModif(&mtxModified_);
      it = mapModified_.find(key);
      if (it == mapModified_.end()) {
         return {};
      }
      return it->second;
   }
   return it->second;
}

void CacheFile::put(const BinaryData &key, const BinaryData &val)
{
   QMutexLocker lock(&mtxModified_);
   mapModified_[key] = val;
   wcModified_.wakeOne();
}
