/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "JsonTools.h"

#include <QJsonValue>
#include <QString>

namespace JsonTools
{
   double GetDouble(const QJsonValue& value, bool& converted)
   {
      if (value.isDouble()) {
         converted = true;
         return value.toDouble();
      } else {
         return value.toString().toDouble(&converted);
      }
   }

   double GetDouble(const QJsonValue& value)
   {
      bool converted;
      return GetDouble(value, converted);
   }

   int64_t GetInt64(const QJsonValue &value)
   {
      return static_cast<int64_t>(GetDouble(value));
   }

   QString GetStringProperty(const QVariantMap& settingsMap, const QString& propertyName)
   {
      if (settingsMap.contains(propertyName)) {
         const auto value = settingsMap.value(propertyName);

         if (value.isValid()) {
            return value.toString();
         }
      }

      return QString{};
   }

   double GetDoubleProperty(const QVariantMap& settingsMap, const QString& propertyName, bool *converted)
   {
      auto it = settingsMap.constFind(propertyName);
      if (it == settingsMap.constEnd()) {
         if (converted != nullptr) {
            *converted = false;
         }
         return 0;
      }

      return it->toDouble(converted);
   }

   uint64_t GetUIntProperty(const QVariantMap& settingsMap, const QString& propertyName, bool *converted)
   {
      auto it = settingsMap.constFind(propertyName);
      if (it == settingsMap.constEnd()) {
         if (converted != nullptr) {
            *converted = false;
         }
         return 0;
      }

      return it->toULongLong(converted);
   }

   bool LoadStringFields(const QVariantMap& data, std::vector<std::pair<QString, std::string*>>& fields
                         , std::string &errorMessage, const FieldsLoadingRule loadingRule)
   {
      for (auto& fieldInfo : fields) {
         const auto it = data.constFind(fieldInfo.first);
         if ((it == data.constEnd()) || (!it->isValid())) {
            if (loadingRule == FieldsLoadingRule::NonEmptyOnly) {
               errorMessage = "Field not found: " + fieldInfo.first.toStdString();
               return false;
            }

            // set empty string
            *fieldInfo.second = std::string();
         } else {
            *fieldInfo.second = it->toString().toStdString();
            if (fieldInfo.second->empty() && (loadingRule == FieldsLoadingRule::NonEmptyOnly)) {
               errorMessage = "Field empty: " + fieldInfo.first.toStdString();
               return false;
            }
         }
      }

      return true;
   }

   bool LoadIntFields(const QVariantMap& data, std::vector<std::pair<QString, int64_t*>>& fields
                      , std::string &errorMessage, const FieldsLoadingRule loadingRule)
   {
      for (auto& fieldInfo : fields) {
         const auto it = data.constFind(fieldInfo.first);
         if ((it == data.constEnd()) || (!it->isValid())) {
            if (loadingRule == FieldsLoadingRule::NonEmptyOnly) {
               errorMessage = "Field not found: " + fieldInfo.first.toStdString();
               return false;
            }

            // set empty string
            *fieldInfo.second = 0;
         } else {
            bool converted = false;
            *fieldInfo.second = it->toInt(&converted);

            if (!converted) {
               errorMessage = "Invalid value for field: " + fieldInfo.first.toStdString();
               return false;
            }
         }
      }

      return true;
   }

   bool LoadDoubleFields(const QVariantMap& data, std::vector<std::pair<QString, double*>>& fields
                         , std::string &errorMessage, const FieldsLoadingRule loadingRule)
   {
      for (auto& fieldInfo : fields) {
         const auto it = data.constFind(fieldInfo.first);
         if ((it == data.constEnd()) || (!it->isValid())) {
            if (loadingRule == FieldsLoadingRule::NonEmptyOnly) {
               errorMessage = "Field not found: " + fieldInfo.first.toStdString();
               return false;
            }

            // set empty string
            *fieldInfo.second = 0;
         } else {
            bool converted = false;
            *fieldInfo.second = it->toDouble(&converted);

            if (!converted) {
               errorMessage = "Invalid value for field: " + fieldInfo.first.toStdString();
               return false;
            }
         }
      }

      return true;
   }

#ifdef NLOHMANN_JSON
   double GetDouble(const nlohmann::json& jsonObject, const std::string& propertyName, bool *converted)
   {
      return GetDoubleProperty(jsonObject, propertyName, converted);
   }

   double GetDoubleProperty(const nlohmann::json& jsonObject, const std::string& propertyName, bool *converted)
   {
      double result = 0;
      bool convertResult = false;
      auto it = jsonObject.find(propertyName);
      if (it != jsonObject.end()) {
         if (it->is_number()) {
            result = it->get<double>();
            convertResult = true;
         } else if (it->is_string()) {
            try {
               result = std::stod(it->get<std::string>());
               convertResult = true;
            } catch (...) {
            }
         }
      }

      if (converted != nullptr) {
         *converted = convertResult;
      }

      return result;
   }
#endif   //NLOHMANN_JSON
}
