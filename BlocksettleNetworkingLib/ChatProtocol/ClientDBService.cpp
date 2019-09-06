#include "ChatProtocol/ClientDBService.h"

using namespace Chat;

ClientDBService::ClientDBService(QObject* parent /* = nullptr */)
   : ServiceThread<ClientDBLogic>(new ClientDBLogic, parent)
{
   qRegisterMetaType<Chat::ApplicationSettingsPtr>();
   qRegisterMetaType<Chat::CryptManagerPtr>();

   ////////// PROXY SIGNALS //////////
   connect(this, &ClientDBService::Init, worker(), &ClientDBLogic::Init);
   connect(this, &ClientDBService::saveMessage, worker(), &ClientDBLogic::saveMessage);
   connect(this, &ClientDBService::updateMessageState, worker(), &ClientDBLogic::updateMessageState);
   connect(this, &ClientDBService::createNewParty, worker(), &ClientDBLogic::createNewParty);
   connect(this, &ClientDBService::readUnsentMessages, worker(), &ClientDBLogic::readUnsentMessages);
   connect(this, &ClientDBService::updateDisplayNameForParty, worker(), &ClientDBLogic::updateDisplayNameForParty);
   connect(this, &ClientDBService::loadPartyDisplayName, worker(), &ClientDBLogic::loadPartyDisplayName);
   connect(this, &ClientDBService::checkUnsentMessages, worker(), &ClientDBLogic::checkUnsentMessages);
   connect(this, &ClientDBService::readHistoryMessages, worker(), &ClientDBLogic::readHistoryMessages);

   ////////// RETURN SIGNALS //////////
   connect(worker(), &ClientDBLogic::initDone, this, &ClientDBService::initDone);
   connect(worker(), &ClientDBLogic::messageArrived, this, &ClientDBService::messageArrived);
   connect(worker(), &ClientDBLogic::messageStateChanged, this, &ClientDBService::messageStateChanged);
   connect(worker(), &ClientDBLogic::messageLoaded, this, &ClientDBService::messageLoaded);
   connect(worker(), &ClientDBLogic::partyDisplayNameLoaded, this, &ClientDBService::partyDisplayNameLoaded);
   connect(worker(), &ClientDBLogic::unsentMessagesFound, this, &ClientDBService::unsentMessagesFound);
}
