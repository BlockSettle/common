#ifndef CHATOTCHELPER_H
#define CHATOTCHELPER_H

#include <QObject>
#include <set>
#include "ChatProtocol/Message.h"
#include "ChatProtocol/ClientParty.h"

namespace spdlog {
   class logger;
}

namespace bs {
   namespace sync {
      class WalletsManager;
   }
   namespace network {
      namespace otc {
         enum class Env : int;
         struct Offer;
         struct Peer;
         struct QuoteRequest;
      }
   }
}

class ApplicationSettings;
class ArmoryConnection;
class AuthAddressManager;
class OtcClient;
class SignContainer;

class ChatOTCHelper : public QObject {
   Q_OBJECT
public:
   ChatOTCHelper(QObject* parent = nullptr);
   ~ChatOTCHelper() override = default;

   void init(bs::network::otc::Env env
      , const std::shared_ptr<spdlog::logger>& loggerPtr
      , const std::shared_ptr<bs::sync::WalletsManager>& walletsMgr
      , const std::shared_ptr<ArmoryConnection>& armory
      , const std::shared_ptr<SignContainer>& signContainer
      , const std::shared_ptr<AuthAddressManager> &authAddressManager
      , const std::shared_ptr<ApplicationSettings> &applicationSettings);

   OtcClient* getClient() const;
   void setCurrentUserId(const std::string& ownUserId);
   const bs::network::otc::Peer* getPeer(const std::string& partyId) const;

public slots:
   void onLogout();
   void onProcessOtcPbMessage(const std::string& data);

   void onOtcRequestSubmit(const std::string& partyId, const bs::network::otc::Offer& offer);
   void onOtcRequestPull(const std::string& partyId);
   void onOtcResponseAccept(const std::string& partyId, const bs::network::otc::Offer& offer);
   void onOtcResponseUpdate(const std::string& partyId, const bs::network::otc::Offer& offer);
   void onOtcResponseReject(const std::string& partyId);

   void onOtcQuoteRequestSubmit(const bs::network::otc::QuoteRequest &request);
   void onOtcPullOwnRequest();

   void onMessageArrived(const Chat::MessagePtrList& messagePtr);
   void onPartyStateChanged(const Chat::ClientPartyPtr& clientPartyPtr);

private:
   OtcClient* otcClient_{};
   std::set<std::string> connectedPeers_;
   std::shared_ptr<spdlog::logger> loggerPtr_;
};

#endif // CHATOTCHELPER_H
