#ifndef __SETTINGS_PARSER_H__
#define __SETTINGS_PARSER_H__

#include <memory>
#include <vector>
#include <QString>
#include <QVariant>

namespace spdlog
{
class logger;
}

// Base class for command line arguments parsing.
//
// If --settings_file argument is set then settings loads from it (ini-format).
// It will be checked for unknown parameters.
// --help is generated and processed as exptected.
// See MobileAppServerSettings for usage example.
class SettingsParser
{
public:
   class BaseSettingsParam {
   public:
      const QString &name() { return name_; }
      const QString &desc() { return desc_; }
      const QString &defValue() { return desc_; }

   protected:
      friend class SettingsParser;

      virtual bool setValue(const QVariant &value) = 0;

      QString name_;
      QString desc_;
      QVariant defValue_;
   };

   template<class T>
   class TemplSettingsParam : public BaseSettingsParam
   {
   public:
      const T &operator()() const { return value_; }

   protected:
      bool setValue(const QVariant &value) override
      {
         if (!value.canConvert<T>()) {
            return false;
         }
         value_ = value.value<T>();
         return true;
      }

      T value_;
   };

   using SettingsParam = TemplSettingsParam<QString>;
   using IntSettingsNoCheckParam = TemplSettingsParam<int>;
   using BoolSettingsParam = TemplSettingsParam<bool>;

   class IntSettingsParam : public IntSettingsNoCheckParam {
   protected:
      bool setValue(const QVariant &value) override
      {
         // QVariant::canConvert<int> returns true even if original string was invalid.
         // Let's use more strict version.
         bool ok = false;
         value_ = value.toInt(&ok);
         return ok;
      }
   };

   SettingsParam SettingsFile;

   SettingsParser(const std::shared_ptr<spdlog::logger>& logger);
   ~SettingsParser() noexcept = default;

   bool LoadSettings(const QStringList& argList);

protected:
   template<class T>
   void addParam(BaseSettingsParam &param, const char* name, const T &defValue, const char* descr)
   {
      addParamVariant(param, name, QVariant::fromValue(defValue), descr);
   }

   void addParam(SettingsParam &param, const char* name, const char *defValue, const char* descr)
   {
      addParamVariant(param, name, QLatin1String(defValue), descr);
   }

   void addParamVariant(BaseSettingsParam &param, const char* name, const QVariant &defValue, const char* descr);

   std::shared_ptr<spdlog::logger> logger_;
   std::vector<BaseSettingsParam*> params_;
};

#endif // __SETTINGS_PARSER_H__
