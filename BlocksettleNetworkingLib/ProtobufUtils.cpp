#include "ProtobufUtils.h"


QDateTime ProtobufUtils::convert(const google::protobuf::Timestamp &value)
{
   return QDateTime::fromMSecsSinceEpoch(value.seconds() * 1000 + value.nanos() / (1000 * 1000));
}

google::protobuf::Timestamp ProtobufUtils::convert(const QDateTime &value)
{
   int64_t valueMs = value.toMSecsSinceEpoch();
   google::protobuf::Timestamp result;
   result.set_seconds(valueMs / 1000);
   result.set_nanos(valueMs % 1000 * (1000 * 1000));
   return result;
}

bool ProtobufUtils::less(const google::protobuf::Timestamp &a, const google::protobuf::Timestamp &b)
{
   if (a.seconds() < b.seconds()) {
      return true;
   }
   if (a.seconds() > b.seconds()) {
      return false;
   }
   return a.nanos() < b.nanos();
}
