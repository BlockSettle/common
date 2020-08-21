/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __AUTH_ADDRESS_MANAGER_H__
#define __AUTH_ADDRESS_MANAGER_H__

#include "AuthAddress.h"

#include <atomic>
#include <memory>
#include <set>
#include <unordered_set>
#include <vector>
#include <QObject>
#include <QThreadPool>

#include "ArmoryConnection.h"
#include "AutheIDClient.h"
#include "BSErrorCode.h"
#include "BSErrorCodeStrings.h"
#include "CommonTypes.h"
#include "WalletEncryption.h"

#include "bs_communication.pb.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      namespace hd {
         class Leaf;
         class SettlementLeaf;
      }
      class Wallet;
      class WalletsManager;
   }
   struct TradeSettings;
}
class AddressVerificator;
class ApplicationSettings;
class ArmoryConnection;
class BsClient;
class BaseCelerClient;
class RequestReplyCommand;
class ResolverFeed_AuthAddress;
class SignContainer;


class AuthAddressManager : public QObject, public ArmoryCallbackTarget
{
   Q_OBJECT

public:
   enum class ReadyError
   {
      NoError,
      MissingAuthAddr,
      MissingAddressList,
      MissingArmoryPtr,
      ArmoryOffline,
   };

   enum class AuthAddressState
   {
      Unknown, // in progress
      NotSubmitted,
      Submitted,
      Tainted,
      Verifying,
      Verified,
      Revoked,
      RevokedByBS,
      Invalid
   };

   AuthAddressManager(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<ArmoryConnection> &);
   ~AuthAddressManager() noexcept override;

   AuthAddressManager(const AuthAddressManager&) = delete;
   AuthAddressManager& operator = (const AuthAddressManager&) = delete;
   AuthAddressManager(AuthAddressManager&&) = delete;
   AuthAddressManager& operator = (AuthAddressManager&&) = delete;

   void init(const std::shared_ptr<ApplicationSettings> &
      , const std::shared_ptr<bs::sync::WalletsManager> &
      , const std::shared_ptr<SignContainer> &);
   void initLogin(const std::shared_ptr<BaseCelerClient> &,
      const std::shared_ptr<bs::TradeSettings> &);

   const std::shared_ptr<bs::TradeSettings>& tradeSettings() const;

   size_t GetAddressCount();
   bs::Address GetAddress(size_t index);

   AuthAddressState GetState(const bs::Address &addr) const;

   void setDefault(const bs::Address &addr);

   bs::Address getDefault() const;
   size_t getDefaultIndex() const;

   bool HaveAuthWallet() const;
   bool HasAuthAddr() const;

   bool CreateNewAuthAddress();

   bool hasSettlementLeaf(const bs::Address &) const;
   void createSettlementLeaf(const bs::Address &, const std::function<void()> &);

   void ConfirmSubmitForVerification(const std::weak_ptr<BsClient> &bsClient, const bs::Address &address);

   bool RevokeAddress(const bs::Address &address);

   ReadyError readyError() const;

   void OnDisconnectedFromCeler();

   std::vector<bs::Address> GetSubmittedAddressList(bool includeVerified = true) const;

   bool isAtLeastOneAwaitingVerification() const;
   bool isAllLoadded() const;
   const std::unordered_set<std::string> &GetBSAddresses() const;

   void setAuthAddressesSigned(const BinaryData &data);

   static std::string readyErrorStr(ReadyError error);

   void setUserType(bs::network::UserType userType);

   bool UserCanSubmitAuthAddress() const;

private slots:
   void tryVerifyWalletAddresses();
   void onAuthWalletChanged();
   void onWalletChanged(const std::string &walletId);
   void onTXSigned(unsigned int id, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason);

   void onWalletCreated();

signals:
   void AddressListUpdated();
   void VerifiedAddressListUpdated();
   void AddrVerifiedOrRevoked(const QString &addr, const QString &state);
   void AddrStateChanged();
   void AuthWalletChanged();
   void AuthWalletCreated(const QString &walletId);
   void Error(const QString &errorText) const;
   void Info(const QString &info);

   void AuthAddressSubmitError(const QString &address, const bs::error::AuthAddressSubmitResult statusCode);
   void AuthAddressSubmitSuccess(const QString &address);
   void AuthAddressSubmitCancelled(const QString &address);
   void AuthRevokeTxSent();
   void gotBsAddressList();

private:
   void SetAuthWallet();
   void ClearAddressList();
   bool setup();
   void OnDataReceived(const std::string& data);

   void ProcessBSAddressListResponse(const std::string& response, bool sigVerified);

   bool HaveBSAddressList() const;

   void VerifyWalletAddressesFunction();
   bool WalletAddressesLoaded();
   void AddAddress(const bs::Address &addr);

   template <typename TVal> TVal lookup(const bs::Address &key, const std::map<bs::Address, TVal> &container) const;

   void SubmitToCeler(const bs::Address &);
   bool BroadcastTransaction(const BinaryData& transactionData);
   void SetBSAddressList(const std::unordered_set<std::string>& bsAddressList);

   // From ArmoryCallbackTarget
   void onStateChanged(ArmoryState) override;

   void markAsSubmitted(const bs::Address &address);

   std::vector<bs::Address> GetVerifiedAddressList() const;

   void SetValidationState(const bs::Address &addr, AddressVerificationState state);

   void SetExplicitState(const bs::Address &addr, AuthAddressState state);

protected:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ArmoryConnection>      armory_;
   std::shared_ptr<ApplicationSettings>   settings_;
   std::shared_ptr<bs::sync::WalletsManager> walletsManager_;
   std::shared_ptr<BaseCelerClient>       celerClient_;
   std::shared_ptr<AddressVerificator>    addressVerificator_;

   mutable std::atomic_flag                  lockList_ = ATOMIC_FLAG_INIT;
   std::vector<bs::Address>                  addresses_;

   std::map<bs::Address, AuthAddressState>   states_;
   mutable std::atomic_flag                  statesLock_ = ATOMIC_FLAG_INIT;

   using HashMap = std::map<bs::Address, BinaryData>;
   mutable bs::Address  defaultAddr_{};

   std::unordered_set<std::string>           bsAddressList_;
   std::shared_ptr<bs::sync::Wallet>         authWallet_;

   std::shared_ptr<SignContainer>      signingContainer_;
   std::unordered_set<unsigned int>    signIdsRevoke_;
   std::shared_ptr<bs::TradeSettings>  tradeSettings_;

   bs::network::UserType userType_ = bs::network::UserType::Undefined;
};

#endif // __AUTH_ADDRESS_MANAGER_H__
