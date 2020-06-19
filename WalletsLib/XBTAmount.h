/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __XBT_AMOUNT_H__
#define __XBT_AMOUNT_H__

#include "BTCNumericTypes.h"

namespace bs {

// class XBTAmount should be used to unify getting satochi amount from double BTC
// across all codebase
// basically it is strongly typed wrapper for uint64_t
   class XBTAmount
   {
   public:
      XBTAmount();
      explicit XBTAmount(const BTCNumericTypes::balance_type amount);
      explicit XBTAmount(const BTCNumericTypes::satoshi_type value);
      ~XBTAmount() noexcept = default;

      XBTAmount(const XBTAmount&) = default;
      XBTAmount& operator = (const XBTAmount&) = default;

      XBTAmount(XBTAmount&&) = default;
      XBTAmount& operator = (XBTAmount&&) = default;

      void SetValueBitcoin(const BTCNumericTypes::balance_type amount);
      void SetValue(const BTCNumericTypes::satoshi_type value);

      BTCNumericTypes::satoshi_type GetValue() const;
      BTCNumericTypes::balance_type GetValueBitcoin() const;

      bool isZero() const
      {
         return (value_ == 0);
      }
      bool isValid() const
      {
         return (value_ != UINT64_MAX);
      }

      bool operator == (const XBTAmount &other) const
      {
         return (value_ == other.value_);
      }
      bool operator != (const XBTAmount &other) const
      {
         return (value_ != other.value_);
      }
      bool operator > (const BTCNumericTypes::satoshi_type other) const
      {
         return (value_ > other);
      }
      bool operator > (const XBTAmount& other) const
      {
         return (value_ > other.value_);
      }

      XBTAmount operator + (const XBTAmount &other) const
      {
         return XBTAmount(value_ + other.value_);
      }
      int64_t operator - (const XBTAmount &other) const
      {
         return (int64_t)value_ - (int64_t)other.value_;
      }

   private:
      static BTCNumericTypes::satoshi_type convertFromBitcoinToSatoshi(BTCNumericTypes::balance_type amount);
      static BTCNumericTypes::balance_type convertFromSatoshiToBitcoin(BTCNumericTypes::satoshi_type value);

   private:
      BTCNumericTypes::satoshi_type value_;
   };

}

int64_t operator-(const bs::XBTAmount &a, const int64_t b);
int64_t operator-(const int64_t a, const bs::XBTAmount &b);


#endif // __XBT_AMOUNT_H__
