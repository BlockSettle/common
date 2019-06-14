#ifndef PROTOBUF_UTILS_H
#define PROTOBUF_UTILS_H

#include <google/protobuf/timestamp.pb.h>
#include <QDateTime>

class ProtobufUtils
{
public:
   static QDateTime convert(const google::protobuf::Timestamp &value);
   static google::protobuf::Timestamp convert(const QDateTime &value);

   static bool less(const google::protobuf::Timestamp &a, const google::protobuf::Timestamp &b);
};

#endif
