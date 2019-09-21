#include "ChatOTCHelper.h"

#include <QFileDialog>
#include <spdlog/spdlog.h>

#include "ApplicationSettings.h"
#include "ArmoryConnection.h"
#include "OtcClient.h"
#include "OtcUtils.h"
#include "SignContainer.h"
#include "chat.pb.h"

ChatOTCHelper::ChatOTCHelper(QObject* parent /*= nullptr*/)
   : QObject(parent)
{
}

void ChatOTCHelper::init(bs::network::otc::Env env
   , const std::shared_ptr<spdlog::logger>& loggerPtr
   , const std::shared_ptr<bs::sync::WalletsManager>& walletsMgr
   , const std::shared_ptr<ArmoryConnection>& armory
   , const std::shared_ptr<SignContainer>& signContainer
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , const std::shared_ptr<ApplicationSettings> &applicationSettings)
{
   loggerPtr_ = loggerPtr;

   OtcClientParams params;

   params.env = env;

   params.offlineLoadPathCb = [applicationSettings]() -> std::string {
      QString signerOfflineDir = applicationSettings->get<QString>(ApplicationSettings::signerOfflineDir);

      QString filePath = QFileDialog::getOpenFileName(nullptr, tr("Select Transaction file"), signerOfflineDir
         , tr("TX files (*.bin)"));

      if (!filePath.isEmpty()) {
         // Update latest used directory if needed
         QString newSignerOfflineDir = QFileInfo(filePath).absoluteDir().path();
         if (signerOfflineDir != newSignerOfflineDir) {
            applicationSettings->set(ApplicationSettings::signerOfflineDir, newSignerOfflineDir);
         }
      }

      return filePath.toStdString();
   };

   params.offlineSavePathCb = [applicationSettings](const std::string &walletId) -> std::string {
      QString signerOfflineDir = applicationSettings->get<QString>(ApplicationSettings::signerOfflineDir);

      const qint64 timestamp = QDateTime::currentDateTime().toSecsSinceEpoch();
      const std::string fileName = fmt::format("{}_{}.bin", walletId, timestamp);

      QString defaultFilePath = QDir(signerOfflineDir).filePath(QString::fromStdString(fileName));
      QString filePath = QFileDialog::getSaveFileName(nullptr, tr("Save Offline TX as..."), defaultFilePath);

      if (!filePath.isEmpty()) {
         QString newSignerOfflineDir = QFileInfo(filePath).absoluteDir().path();
         if (signerOfflineDir != newSignerOfflineDir) {
            applicationSettings->set(ApplicationSettings::signerOfflineDir, newSignerOfflineDir);
         }
      }

      return filePath.toStdString();
   };

   otcClient_ = new OtcClient(loggerPtr, walletsMgr, armory, signContainer, authAddressManager, std::move(params), this);
}

OtcClient* ChatOTCHelper::client() const
{
   return otcClient_;
}

const bs::network::otc::Peer *ChatOTCHelper::peer(const bs::network::otc::PeerId &peerId) const
{
   return otcClient_->peer(peerId);
}

void ChatOTCHelper::setCurrentUserId(const std::string& ownUserId)
{
   otcClient_->setOwnContactId(ownUserId);
}

void ChatOTCHelper::onLogout()
{
   for (const auto &partyId : connectedPeers_) {
      otcClient_->peerDisconnected(partyId);
   }
   connectedPeers_.clear();
}

void ChatOTCHelper::onProcessOtcPbMessage(const std::string& data)
{
   otcClient_->processPbMessage(data);
}

void ChatOTCHelper::onOtcRequestSubmit(const bs::network::otc::PeerId &peerId, const bs::network::otc::Offer& offer)
{
   bool result = otcClient_->sendOffer(offer, peerId);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "send offer failed");
      return;
   }
}

void ChatOTCHelper::onOtcPullOrReject(const bs::network::otc::PeerId &peerId)
{
   bool result = otcClient_->pullOrReject(peerId);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "pull or reject failed");
      return;
   }
}

void ChatOTCHelper::onOtcResponseAccept(const bs::network::otc::PeerId &peerId, const bs::network::otc::Offer& offer)
{
   bool result = otcClient_->acceptOffer(offer, peerId);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "accept offer failed");
      return;
   }
}

void ChatOTCHelper::onOtcResponseUpdate(const bs::network::otc::PeerId &peerId, const bs::network::otc::Offer& offer)
{
   bool result = otcClient_->updateOffer(offer, peerId);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "update offer failed");
      return;
   }
}

void ChatOTCHelper::onOtcResponseReject(const bs::network::otc::PeerId &peerId)
{
   bool result = otcClient_->pullOrReject(peerId);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "reject offer failed");
      return;
   }
}

void ChatOTCHelper::onOtcQuoteRequestSubmit(const bs::network::otc::QuoteRequest &request)
{
   bool result = otcClient_->sendQuoteRequest(request);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "sending quote request failed");
      return;
   }
}

void ChatOTCHelper::onOtcPullOwnRequest()
{
   bool result = otcClient_->pullOwnRequest();
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "pulling own request failed");
      return;
   }
}

void ChatOTCHelper::onOtcQuoteResponseSubmit(const bs::network::otc::QuoteResponse &response)
{
   bool result = otcClient_->sendQuoteResponse(response);
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "sending response failed");
      return;
   }
}

void ChatOTCHelper::onMessageArrived(const Chat::MessagePtrList& messagePtr)
{
   for (const auto &msg : messagePtr) {
      if (msg->partyId() == Chat::OtcRoomName) {
         auto data = OtcUtils::deserializePublicMessage(msg->messageText());
         if (!data.isNull()) {
            otcClient_->processPublicMessage(msg->timestamp(), msg->senderHash(), data);
         }
      } else if (msg->partyMessageState() == Chat::SENT && msg->senderHash() != otcClient_->ownContactId()) {
         auto connIt = connectedPeers_.find(msg->partyId());
         if (connIt == connectedPeers_.end()) {
            continue;
         }

         auto data = OtcUtils::deserializeMessage(msg->messageText());
         if (!data.isNull()) {
            otcClient_->processContactMessage(msg->partyId(), data);
         }
      }
   }
}

void ChatOTCHelper::onPartyStateChanged(const Chat::ClientPartyPtr& clientPartyPtr)
{
   if (clientPartyPtr->partyType() != Chat::PRIVATE_DIRECT_MESSAGE) {
      return;
   }
   
   const std::string& partyId = clientPartyPtr->id();
   auto connIt = connectedPeers_.find(partyId);
   if (clientPartyPtr->clientStatus() == Chat::ONLINE && connIt == connectedPeers_.end()) {
      otcClient_->peerConnected(partyId);
      connectedPeers_.insert(partyId);
   } else if (clientPartyPtr->clientStatus() == Chat::OFFLINE && connIt != connectedPeers_.end()) {
      otcClient_->peerDisconnected(partyId);
      connectedPeers_.erase(connIt);
   }
}
