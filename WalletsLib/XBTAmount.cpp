/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "XBTAmount.h"

#include <cmath>

using namespace bs;
XBTAmount::XBTAmount()
 : value_{0}
{}

XBTAmount::XBTAmount(const BTCNumericTypes::balance_type amount)
 : value_{convertFromBitcoinToSatoshi(amount)}
{}

XBTAmount::XBTAmount(const BTCNumericTypes::satoshi_type value)
 : value_{value}
{}

BTCNumericTypes::satoshi_type XBTAmount::GetValue() const
{
   return value_;
}

BTCNumericTypes::balance_type XBTAmount::GetValueBitcoin() const
{
   return convertFromSatoshiToBitcoin(value_);
}

void XBTAmount::SetValueBitcoin(const BTCNumericTypes::balance_type amount)
{
   SetValue(convertFromBitcoinToSatoshi(amount));
}

void XBTAmount::SetValue(const BTCNumericTypes::satoshi_type value)
{
   value_ = value;
}

BTCNumericTypes::satoshi_type XBTAmount::convertFromBitcoinToSatoshi(BTCNumericTypes::balance_type amount)
{
   return std::llround(amount * BTCNumericTypes::BalanceDivider);
}

BTCNumericTypes::balance_type XBTAmount::convertFromSatoshiToBitcoin(BTCNumericTypes::satoshi_type value)
{
   return static_cast<BTCNumericTypes::balance_type>(value) / BTCNumericTypes::BalanceDivider;
}


int64_t operator-(const bs::XBTAmount &a, const int64_t b)
{
   return (int64_t)a.GetValue() - b;
}

int64_t operator-(const int64_t a, const bs::XBTAmount &b)
{
   return a - (int64_t)b.GetValue();
}
