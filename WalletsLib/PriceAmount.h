/*

***********************************************************************************
* Copyright (C) 2021 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef PRICE_AMOUNT_H
#define PRICE_AMOUNT_H

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace bs {

   template<uint32_t precision>
   class PriceAmount
   {
   public:
      using Self = PriceAmount<precision>;
      using underlying_type = int64_t;

      PriceAmount()
         : value_{0}
      {
      }

      // Amount is truncated toward zero, so PriceAmount<2>(1.1299) is equal to PriceAmount<2>(1.12)
      PriceAmount(double amount)
      {
         value_ = static_cast<underlying_type>(std::trunc(amount * scale()));
      }

      PriceAmount(const Self&) = default;
      Self& operator = (const Self&) = default;

      PriceAmount(Self&&) = default;
      Self& operator = (Self&&) = default;

      std::string to_string() const
      {
         std::ostringstream ss;
         underlying_type base = std::abs(value_) / scale();
         underlying_type rem = std::abs(value_) % scale();
         if (value_ < 0) {
            ss << '-';
         }
         ss << base << "." << std::setw(precision) << std::setfill('0') << rem;
         return ss.str();
      }

      bool isZero() const {
         return value_ == 0;
      }

      double to_double() const
      {
         return value_ / static_cast<double>(scale());
      }

      bool operator == (const Self &other) const
      {
         return (value_ == other.value_);
      }
      bool operator != (const Self &other) const
      {
         return (value_ != other.value_);
      }
      bool operator > (const Self &other) const
      {
         return (value_ > other.value_);
      }
      bool operator < (const Self &other) const
      {
         return (value_ < other.value_);
      }

      Self operator + (const Self &other) const
      {
         return PriceAmount(value_ + other.value_);
      }
      Self operator - (const Self &other) const
      {
         return PriceAmount(value_ - other.value_);
      }

      underlying_type serialize() const
      {
         return value_;
      }

      static Self deserialize(underlying_type data)
      {
         return Self{data};
      }

   private:
      static constexpr underlying_type scaleHelper(underlying_type value)
      {
          return value == 0 ? 1 : 10 * scaleHelper(value - 1);
      }

      static constexpr underlying_type scale()
      {
         return scaleHelper(precision);
      }

      explicit PriceAmount(underlying_type amount)
      {
         value_ = amount;
      }

      underlying_type value_;
   };

   using CentAmount = PriceAmount<2>;

}

#endif // PRICE_AMOUNT_H
