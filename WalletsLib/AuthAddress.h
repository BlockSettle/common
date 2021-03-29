/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __AUTH_ADDRESS_H__
#define __AUTH_ADDRESS_H__

#include <string>
#include <memory>
#include "Address.h"
#include "EncryptionUtils.h"

enum class AddressVerificationState
{
   // VerificationFailed - there were errors/issues while getting address verification state
   VerificationFailed = 0,
   // Virgin - address does not have history
   Virgin,
   // Tainted - address has no validation outputs but has history
   Tainted,
   // Submitted - address has a validation output without enough confirmations
   Verifying,
   // Verified - address is verified
   Verified,
   // Revoked - address is revoked (by user)
   Revoked,
   // Invalidated - address was invalidated by a validation address (explicit) or 
   // validation address for this user address was revoked (implicit)
   Invalidated_Explicit,
   Invalidated_Implicit,
   Whitelisted
};

std::string to_string(AddressVerificationState state);

class AuthAddress
{
public:
   AuthAddress(const bs::Address &chainedAddress, AddressVerificationState state = AddressVerificationState::VerificationFailed);
   ~AuthAddress() noexcept = default;

   AuthAddress(const AuthAddress&) = default;
   AuthAddress& operator = (const AuthAddress&) = default;

   AuthAddress(AuthAddress&&) = default;
   AuthAddress& operator = (AuthAddress&&) = default;

   const bs::Address& GetChainedAddress() const { return chainedAddress_; }

   AddressVerificationState   GetState() const { return state_; }
   void                       SetState(AddressVerificationState newState) { state_ = newState; }

   BinaryData GetInitialTransactionTxHash() const { return initialBsTxHash_; }
   void SetInitialTransactionTxHash(const BinaryData& hash) { initialBsTxHash_ = hash; }

   BinaryData GetVerificationChangeTxHash() const { return verificationChangeTxHash_; }
   void SetVerificationChangeTxHash(const BinaryData& hash) { verificationChangeTxHash_ = hash; }

   const bs::Address &GetBSFundingAddress() const { return bsFundingAddress160_; }
   void SetBSFundingAddress(const bs::Address &address) { bsFundingAddress160_ = address; }

private:
   bs::Address                   chainedAddress_;

   bs::Address                   bsFundingAddress160_;
   BinaryData                    initialBsTxHash_;
   BinaryData                    verificationChangeTxHash_;
   AddressVerificationState      state_;
};

#endif // __AUTH_ADDRESS_H__
