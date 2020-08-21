/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#include <vector>

namespace bs {

   std::string toHex(const std::string &str, bool uppercase = false);

   // Works for ASCII encoding only
   std::string toLower(std::string str);
   std::string toUpper(std::string str);

   // Very basic email address verification check (checks that there is one '@' symbol and at least one '.' after that)
   bool isValidEmail(const std::string &str);

   int convertToInt(const std::string& str, bool& converted);
   bool convertToBool(const std::string& str, bool& converted);

   std::vector<std::string> split(const std::string& str, char separator);

   std::string trim(std::string str);

} // namespace bs

#endif
