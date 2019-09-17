#ifndef OTC_UTILS_H
#define OTC_UTILS_H

#include <string>
#include <QString>
#include "BinaryData.h"

class OtcUtils
{
public:
   static std::string serializeMessage(const BinaryData &data);
   static BinaryData deserializeMessage(const std::string &data);

   static std::string serializePublicMessage(const BinaryData &data);
   static BinaryData deserializePublicMessage(const std::string &data);

   // Parse incoming message and convert it into readable string (that will be visible in the UI).
   // If not OTC return empty string.
   static QString toReadableString(const QString &text);

};

#endif
