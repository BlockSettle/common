#ifndef CLIENTPARTYMODEL_H
#define CLIENTPARTYMODEL_H

#include <QMetaType>
#include <QObject>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ChatProtocol/PartyModel.h"
#include "ChatProtocol/ClientParty.h"
#include "ChatProtocol/Message.h"

namespace spdlog
{
   class logger;
}

namespace Chat
{
   enum class ClientPartyModelError
   {
      DynamicPointerCast,
      UserNameNotFound,
      QObjectCast,
      PartyNotFound
   };

   enum class PrivatePartyState
   {
      Unknown,
      Uninitialized,
      RequestedOutgoing,
      RequestedIncoming,
      Rejected,
      Initialized,
      QObjectCast
   };
   
   using LoggerPtr = std::shared_ptr<spdlog::logger>;

   class ClientPartyModel : public PartyModel
   {
      Q_OBJECT
   public:
      ClientPartyModel(const LoggerPtr& loggerPtr, QObject* parent = nullptr);
      IdPartyList getIdPartyList() const;
      ClientPartyPtr getPartyByUserName(const std::string& userName);
      ClientPartyPtr getClientPartyById(const std::string& party_id);

      const std::string& ownUserName() const { return ownUserName_; }
      void setOwnUserName(std::string val) { ownUserName_ = val; }
      PrivatePartyState deducePrivatePartyStateForUser(const std::string& userName);

   signals:
      void error(const ClientPartyModelError& errorCode, const std::string& what = "");
      void clientPartyStatusChanged(const ClientPartyPtr& clientPartyPtr);
      void messageArrived(const Chat::MessagePtrList& messagePtr);
      void messageStateChanged(const std::string& partyId, const std::string& message_id, const int party_message_state);
      void partyStateChanged(const std::string& partyId);
      void clientPartyDisplayNameChanged(const std::string& partyId);

   private slots:
      void handleLocalErrors(const ClientPartyModelError& errorCode, const std::string& what);
      void handlePartyInserted(const PartyPtr& partyPtr);
      void handlePartyRemoved(const PartyPtr& partyPtr);
      void handlePartyStatusChanged(const ClientStatus& clientStatus);
      void handlePartyStateChanged(const std::string& partyId);
      void handleDisplayNameChanged();
      
   private:
      ClientPartyPtr castToClientPartyPtr(const PartyPtr& partyPtr);
      std::string ownUserName_;
   };

   using ClientPartyModelPtr = std::shared_ptr<ClientPartyModel>;

}

Q_DECLARE_METATYPE(Chat::PrivatePartyState)
Q_DECLARE_METATYPE(Chat::ClientPartyModelError)

#endif // CLIENTPARTYMODEL_H
