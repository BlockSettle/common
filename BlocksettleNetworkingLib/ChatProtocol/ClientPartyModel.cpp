#include "ChatProtocol/ClientPartyModel.h"

#include <disable_warnings.h>
#include <spdlog/logger.h>
#include <enable_warnings.h>

using namespace Chat;

ClientPartyModel::ClientPartyModel(const LoggerPtr& loggerPtr, QObject* parent /* = nullptr */)
   : PartyModel(loggerPtr, parent)
{
   qRegisterMetaType<Chat::PrivatePartyState>();
   qRegisterMetaType<Chat::ClientPartyModelError>();

   connect(this, &ClientPartyModel::error, this, &ClientPartyModel::handleLocalErrors);
   connect(this, &ClientPartyModel::partyInserted, this, &ClientPartyModel::handlePartyInserted);
   connect(this, &ClientPartyModel::partyRemoved, this, &ClientPartyModel::handlePartyRemoved);
}

IdPartyList ClientPartyModel::getIdPartyList() const
{
   IdPartyList idPartyList;
   for (const std::pair<std::string, PartyPtr>& element : partyMap_)
   {
      idPartyList.push_back(element.first);
   }

   return idPartyList;
}

ClientPartyPtr ClientPartyModel::getPartyByUserName(const std::string& userName)
{
   const IdPartyList idPartyList = getIdPartyList();

   for (const auto& partyId : idPartyList)
   {
      const ClientPartyPtr clientPartyPtr = getClientPartyById(partyId);

      if (nullptr == clientPartyPtr)
      {
         continue;
      }

      if (clientPartyPtr->isUserBelongsToParty(userName))
      {
         return clientPartyPtr;
      }
   }

   emit error(ClientPartyModelError::UserNameNotFound, userName);

   return nullptr;
}

void ClientPartyModel::handleLocalErrors(const Chat::ClientPartyModelError& errorCode, const std::string& what, bool displayAsWarning)
{
   const std::string displayAs = displayAsWarning ? WarningDescription : ErrorDescription;

   loggerPtr_->debug("[ClientPartyModel::handleLocalErrors] {}: {}, what: {}", displayAs, (int)errorCode, what);
}

void ClientPartyModel::handlePartyInserted(const PartyPtr& partyPtr)
{
   const ClientPartyPtr clientPartyPtr = getClientPartyById(partyPtr->id());

   if (nullptr == clientPartyPtr)
   {
      return;
   }

   connect(clientPartyPtr.get(), &ClientParty::clientStatusChanged, this, &ClientPartyModel::handlePartyStatusChanged);
   connect(clientPartyPtr.get(), &ClientParty::partyStateChanged, this, &ClientPartyModel::handlePartyStateChanged);
   connect(clientPartyPtr.get(), &ClientParty::displayNameChanged, this, &ClientPartyModel::handleDisplayNameChanged);
}

void ClientPartyModel::handlePartyRemoved(const PartyPtr& partyPtr)
{
   const ClientPartyPtr clientPartyPtr = getClientPartyById(partyPtr->id());

   if (nullptr == clientPartyPtr)
   {
      return;
   }

   disconnect(clientPartyPtr.get(), &ClientParty::displayNameChanged, this, &ClientPartyModel::handleDisplayNameChanged);
   disconnect(clientPartyPtr.get(), &ClientParty::partyStateChanged, this, &ClientPartyModel::handlePartyStateChanged);
   disconnect(clientPartyPtr.get(), &ClientParty::clientStatusChanged, this, &ClientPartyModel::handlePartyStatusChanged);
}

void ClientPartyModel::handlePartyStatusChanged(const ClientStatus&)
{
   ClientParty* clientParty = qobject_cast<ClientParty*>(sender());

   if (!clientParty)
   {
      emit error(ClientPartyModelError::QObjectCast);
      return;
   }

   const ClientPartyPtr clientPartyPtr = getClientPartyById(clientParty->id());

   if (!clientPartyPtr)
   {
      return;
   }

   emit clientPartyStatusChanged(clientPartyPtr);
}

ClientPartyPtr ClientPartyModel::castToClientPartyPtr(const PartyPtr& partyPtr)
{
   const ClientPartyPtr clientPartyPtr = std::dynamic_pointer_cast<ClientParty>(partyPtr);

   if (!clientPartyPtr)
   {
      emit error(ClientPartyModelError::DynamicPointerCast, partyPtr->id());
      return nullptr;
   }

   return clientPartyPtr;
}

ClientPartyPtr ClientPartyModel::getClientPartyById(const std::string& party_id)
{
   const PartyPtr partyPtr = getPartyById(party_id);

   if (nullptr == partyPtr)
   {
      emit error(ClientPartyModelError::PartyNotFound, party_id);
      return nullptr;
   }

   const ClientPartyPtr clientPartyPtr = castToClientPartyPtr(partyPtr);

   return clientPartyPtr;
}

void ClientPartyModel::handlePartyStateChanged(const std::string& partyId)
{
   emit partyStateChanged(partyId);
   emit partyModelChanged();
}

PrivatePartyState ClientPartyModel::deducePrivatePartyStateForUser(const std::string& userName)
{
   const ClientPartyPtr clientPartyPtr = getPartyByUserName(userName);

   if (!clientPartyPtr)
   {
      return PrivatePartyState::Unknown;
   }

   const PartyState partyState = clientPartyPtr->partyState();

   if (PartyState::UNINITIALIZED == partyState)
   {
      return PrivatePartyState::Uninitialized;
   }

   if (PartyState::REQUESTED == partyState)
   {
      if (userName == ownUserName_)
      {
         return PrivatePartyState::RequestedOutgoing;
      }
      else
      {
         return PrivatePartyState::RequestedIncoming;
      }
   }

   if (PartyState::REJECTED == partyState || ownUserName_ == userName)
   {
      return PrivatePartyState::Rejected;
   }

   return PrivatePartyState::Initialized;
}

void ClientPartyModel::handleDisplayNameChanged()
{
   ClientParty* clientParty = qobject_cast<ClientParty*>(sender());

   if (!clientParty)
   {
      emit error(ClientPartyModelError::QObjectCast);
      return;
   }

   emit clientPartyDisplayNameChanged(clientParty->id());
}

ClientPartyPtr ClientPartyModel::getClientPartyByCreatorHash(const std::string& creatorHash)
{
   const IdPartyList idPartyList = getIdPartyList();

   for (const auto& partyId : idPartyList)
   {
      const ClientPartyPtr clientPartyPtr = getClientPartyById(partyId);

      if (clientPartyPtr && clientPartyPtr->partyCreatorHash() == creatorHash)
      {
         return clientPartyPtr;
      }
   }

   emit error(ClientPartyModelError::PartyCreatorHashNotFound, creatorHash);

   return nullptr;
}

ClientPartyPtr ClientPartyModel::getClientPartyByUserHash(const std::string& userHash)
{
   const IdPartyList idPartyList = getIdPartyList();

   for (const auto& partyId : idPartyList)
   {
      const ClientPartyPtr clientPartyPtr = getClientPartyById(partyId);

      if (clientPartyPtr && clientPartyPtr->userHash() == userHash)
      {
         return clientPartyPtr;
      }
   }

   emit error(ClientPartyModelError::UserHashNotFound, userHash);

   return nullptr;
}
