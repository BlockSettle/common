/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __BS_ERROR_CODE_H__
#define __BS_ERROR_CODE_H__

namespace bs {
   namespace error {
      enum class ErrorCode
      {
         // General error codes
         NoError = 0,
         FailedToParse,
         WalletNotFound,
         WrongAddress,
         MissingPassword,
         InvalidPassword,
         MissingAuthKeys,
         MissingSettlementWallet,
         MissingAuthWallet,
         InternalError,
         WalletAlreadyPresent,

         // TX signing error codes
         TxInvalidRequest,
         TxCancelled,
         TxSpendLimitExceed,
         TxRequestFileExist,
         TxFailedToOpenRequestFile,
         TxFailedToWriteRequestFile,
         TxSettlementExpired,

         // Change wallet error codes
         WalletFailedRemoveLastEidDevice,

         // Other codes
         AutoSignDisabled
      };

      enum class AuthAddressSubmitResult
      {
         Success = 0,
         SubmitLimitExceeded,
         ServerError,
         AlreadyUsed,
         RequestTimeout,
         AuthRequestSignFailed
      };
   }
}

#endif
