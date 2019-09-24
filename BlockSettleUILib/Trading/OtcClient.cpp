#include "OtcClient.h"

#include <QFile>
#include <QTimer>
#include <spdlog/spdlog.h>

#include "AddressVerificator.h"
#include "AuthAddressManager.h"
#include "BtcUtils.h"
#include "CommonTypes.h"
#include "EncryptionUtils.h"
#include "OfflineSigner.h"
#include "ProtobufUtils.h"
#include "SelectedTransactionInputs.h"
#include "SettlementMonitor.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include "Wallets/SyncHDLeaf.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"
#include "bs_proxy_terminal_pb.pb.h"
#include "otc.pb.h"

using namespace Blocksettle::Communication::Otc;
using namespace Blocksettle::Communication;
using namespace bs::network;
using namespace bs::network::otc;
using namespace bs::sync::dialog;

struct OtcClientDeal
{
   bs::network::otc::Side side{};

   std::string hdWalletId;
   BinaryData settlementId;
   bs::Address settlementAddr;

   bs::core::wallet::TXSignRequest payin;
   bs::core::wallet::TXSignRequest payout;

   bs::signer::RequestId payinReqId{};
   bs::signer::RequestId payoutReqId{};

   BinaryData payinSigned;
   BinaryData payoutSigned;

   bs::Address ourAuthAddress;
   BinaryData cpPubKey;

   int64_t amount{};
   int64_t fee{};
   int64_t price{};
   bool sellFromOffline{false};

   bool success{false};
   std::string errorMsg;

   std::unique_ptr<AddressVerificator> addressVerificator;

   bs::Address authAddress(bool isSeller) const
   {
      const bool weSell = (side == bs::network::otc::Side::Sell);
      if (isSeller == weSell) {
         return ourAuthAddress;
      } else {
         return bs::Address::fromPubKey(cpPubKey).display();
      }
   }

   bool isRequestor() const { return side == bs::network::otc::Side::Sell; }
   bs::Address requestorAuthAddress() const { return authAddress(isRequestor()); }
   bs::Address responderAuthAddress() const { return authAddress(!isRequestor()); }

   static OtcClientDeal error(const std::string &msg)
   {
      OtcClientDeal result;
      result.errorMsg = msg;
      return result;
   }
};

namespace {

   const int kSettlementIdSize = 32;
   const int kTxHashSize = 32;
   const int kPubKeySize = 33;

   const auto kStartOtcTimeout = std::chrono::seconds(10);

   bs::sync::PasswordDialogData toPasswordDialogData(const OtcClientDeal &deal, const bs::core::wallet::TXSignRequest &signRequest)
   {
      double price = fromCents(deal.price);

      QString qtyProd = UiUtils::XbtCurrency;
      QString fxProd = QString::fromStdString("EUR");

      bs::sync::PasswordDialogData dialogData;

      dialogData.setValue(keys::Market, "XBT");

      dialogData.setValue(keys::ProductGroup, QObject::tr(bs::network::Asset::toString(bs::network::Asset::SpotXBT)));
      dialogData.setValue(keys::Security, "XBT/EUR");
      dialogData.setValue(keys::Product, "XBT");
      dialogData.setValue(keys::Side, QObject::tr(bs::network::Side::toString(bs::network::Side::Type(deal.side))));

      dialogData.setValue(keys::Title, QObject::tr("Settlement Transaction"));

      dialogData.setValue(keys::Price, UiUtils::displayPriceXBT(price));

      dialogData.setValue(keys::SettlementAddress, deal.settlementAddr.display());
      dialogData.setValue(keys::SettlementId, deal.settlementId.toHexStr());

      dialogData.setValue(keys::RequesterAuthAddress, deal.requestorAuthAddress().display());
      dialogData.setValue(keys::RequesterAuthAddressVerified, deal.isRequestor());

      dialogData.setValue(keys::ResponderAuthAddress, deal.responderAuthAddress().display());
      dialogData.setValue(keys::ResponderAuthAddressVerified, !deal.isRequestor());

      return dialogData;
   }

   bs::sync::PasswordDialogData toPasswordDialogDataPayin(const OtcClientDeal &deal, const bs::core::wallet::TXSignRequest &signRequest)
   {
      auto dialogData = toPasswordDialogData(deal, signRequest);
      dialogData.setValue(keys::SettlementPayInVisible, true);
      dialogData.setValue(keys::Title, QObject::tr("Settlement Pay-In"));
      return dialogData;
   }

   bs::sync::PasswordDialogData toPasswordDialogDataPayout(const OtcClientDeal &deal, const bs::core::wallet::TXSignRequest &signRequest)
   {
      auto dialogData = toPasswordDialogData(deal, signRequest);
      dialogData.setValue(keys::SettlementPayOutVisible, true);
      dialogData.setValue(keys::Title, QObject::tr("Settlement Pay-Out"));
      return dialogData;
   }

   bool isValidOffer(const ContactMessage_Offer &offer)
   {
      return offer.price() > 0 && offer.amount() > 0;
   }

   void copyOffer(const Offer &src, ContactMessage_Offer *dst)
   {
      dst->set_price(src.price);
      dst->set_amount(src.amount);
   }

   void copyRange(const otc::Range &src, Otc::Range *dst)
   {
      dst->set_lower(src.lower);
      dst->set_upper(src.upper);
   }

   void copyRange(const Otc::Range&src, otc::Range *dst)
   {
      dst->lower = src.lower();
      dst->upper = src.upper();
   }

   Peer *findPeer(std::unordered_map<std::string, bs::network::otc::Peer> &map, const std::string &contactId)
   {
      auto it = map.find(contactId);
      return it == map.end() ? nullptr : &it->second;
   }

} // namespace

OtcClient::OtcClient(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr
   , const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<SignContainer> &signContainer
   , const std::shared_ptr<AuthAddressManager> &authAddressManager
   , OtcClientParams params
   , QObject *parent)
   : QObject (parent)
   , logger_(logger)
   , walletsMgr_(walletsMgr)
   , armory_(armory)
   , signContainer_(signContainer)
   , authAddressManager_(authAddressManager)
   , params_(std::move(params))
{
   connect(signContainer.get(), &SignContainer::TXSigned, this, &OtcClient::onTxSigned);
}

OtcClient::~OtcClient() = default;

Peer *OtcClient::contact(const std::string &contactId)
{
   return findPeer(contactMap_, contactId);
}

Peer *OtcClient::request(const std::string &contactId)
{
   return findPeer(requestMap_, contactId);
}

Peer *OtcClient::response(const std::string &contactId)
{
   return findPeer(responseMap_, contactId);
}

void OtcClient::setOwnContactId(const std::string &contactId)
{
   ownContactId_ = contactId;
}

const std::string &OtcClient::ownContactId() const
{
   return ownContactId_;
}


bool OtcClient::sendQuoteRequest(const QuoteRequest &request)
{
   if (ownRequest_) {
      SPDLOG_LOGGER_ERROR(logger_, "own quote request was already sent");
      return false;
   }

   if (ownContactId_.empty()) {
      SPDLOG_LOGGER_ERROR(logger_, "own contact id is not set");
      return false;
   }

   ownRequest_ = std::make_unique<Peer>(ownContactId_, PeerType::Request);
   ownRequest_->request = request;
   ownRequest_->isOwnRequest = true;

   Otc::PublicMessage msg;
   auto d = msg.mutable_request();
   d->set_sender_side(Otc::Side(request.ourSide));
   d->set_range(Otc::RangeType(request.rangeType));
   emit sendPublicMessage(msg.SerializeAsString());

   updatePublicLists();

   return true;
}

bool OtcClient::sendQuoteResponse(Peer *peer, const QuoteResponse &quoteResponse)
{
   if (peer->state != State::Idle) {
      SPDLOG_LOGGER_ERROR(logger_, "can't send offer to '{}', peer should be in IdleRequestor state", peer->toString());
      return false;
   }

   if (!isSubRange(otc::getRange(peer->request.rangeType), quoteResponse.amount)) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid range");
      return false;
   }

   if (peer->state != State::Idle) {
      SPDLOG_LOGGER_ERROR(logger_, "can't send quote response to '{}', peer should be in IdleRequestor state", peer->toString());
      return false;
   }

   changePeerState(peer, State::QuoteSent);
   peer->response = quoteResponse;

   Otc::ContactMessage msg;
   auto d = msg.mutable_quote_response();
   copyRange(quoteResponse.price, d->mutable_price());
   copyRange(quoteResponse.amount, d->mutable_amount());
   send(peer, msg);

   updatePublicLists();
   return true;
}

bool OtcClient::sendOffer(Peer *peer, const Offer &offer)
{
   SPDLOG_LOGGER_DEBUG(logger_, "send offer to {} (price: {}, amount: {})", peer->toString(), offer.price, offer.amount);

   if (!verifyOffer(offer)) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid offer details");
      return false;
   }

   auto settlementLeaf = findSettlementLeaf(offer.authAddress);
   if (!settlementLeaf) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find settlement leaf with address '{}'", offer.authAddress);
      return false;
   }

   settlementLeaf->getRootPubkey([this, peer, offer, handle = peer->validityFlag.handle()](const SecureBinaryData &ourPubKey) {
      if (handle.isValid()) {
         SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
         return;
      }

      if (ourPubKey.getSize() != kPubKeySize) {
         SPDLOG_LOGGER_ERROR(logger_, "invalid auth address root public key");
         return;
      }

      if (peer->state != State::Idle) {
         SPDLOG_LOGGER_ERROR(logger_, "can't send offer to '{}', peer should be in idle state", peer->toString());
         return;
      }

      peer->offer = offer;
      peer->ourAuthPubKey = ourPubKey;
      changePeerState(peer, State::OfferSent);

      ContactMessage msg;
      if (offer.ourSide == otc::Side::Buy) {
         auto d = msg.mutable_buyer_offers();
         copyOffer(offer, d->mutable_offer());
         d->set_auth_address_buyer(peer->ourAuthPubKey.toBinStr());
      } else {
         auto d = msg.mutable_seller_offers();
         copyOffer(offer, d->mutable_offer());
      }
      send(peer, msg);
   });

   return true;
}

bool OtcClient::pullOrReject(Peer *peer)
{
   if (peer->isOwnRequest) {
      assert(peer == ownRequest_.get());

      SPDLOG_LOGGER_DEBUG(logger_, "pull own quote request");
      ownRequest_.reset();

      // This will remove everything when we pull public request.
      // We could keep current shield and show that our public request was pulled instead.
      responseMap_.clear();

      Otc::PublicMessage msg;
      msg.mutable_close();
      emit sendPublicMessage(msg.SerializeAsString());

      updatePublicLists();
      return true;
   }

   switch (peer->state) {
      case State::QuoteSent:
      case State::OfferSent:
      case State::OfferRecv:
         break;
      default:
         SPDLOG_LOGGER_ERROR(logger_, "can't pull offer from '{}'", peer->toString());
         return false;
   }

   SPDLOG_LOGGER_DEBUG(logger_, "pull of reject offer from {}", peer->toString());

   ContactMessage msg;
   msg.mutable_close();
   send(peer, msg);

   changePeerState(peer, State::Idle);

   if (peer->type == PeerType::Request) {
      updatePublicLists();
   }

   return true;
}

bool OtcClient::acceptOffer(Peer *peer, const bs::network::otc::Offer &offer)
{
   SPDLOG_LOGGER_DEBUG(logger_, "accept offer from {} (price: {}, amount: {})", peer->toString(), offer.price, offer.amount);

   if (!verifyOffer(offer)) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid offer details");
      return false;
   }

   auto settlementLeaf = findSettlementLeaf(offer.authAddress);
   if (!settlementLeaf) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find settlement leaf with address '{}'", offer.authAddress);
      return false;
   }

   settlementLeaf->getRootPubkey([this, offer, peer, handle = peer->validityFlag.handle()](const SecureBinaryData &ourPubKey) {
      if (handle.isValid()) {
         SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
         return;
      }

      if (peer->state != State::OfferRecv) {
         SPDLOG_LOGGER_ERROR(logger_, "can't accept offer from '{}', we should be in OfferRecv state", peer->toString());
         return;
      }

      if (ourPubKey.getSize() != kPubKeySize) {
         SPDLOG_LOGGER_ERROR(logger_, "invalid auth address root public key");
         return;
      }

      assert(offer == peer->offer);

      peer->offer = offer;
      peer->ourAuthPubKey = ourPubKey;

      if (peer->offer.ourSide == otc::Side::Sell) {
         sendSellerAccepts(peer);
         return;
      }

      // Need to get other details from seller first.
      // They should be available from Accept reply.
      ContactMessage msg;
      auto d = msg.mutable_buyer_accepts();
      copyOffer(offer, d->mutable_offer());
      d->set_auth_address_buyer(peer->ourAuthPubKey.toBinStr());
      send(peer, msg);

      changePeerState(peer, State::WaitPayinInfo);
   });

   return true;
}

bool OtcClient::updateOffer(Peer *peer, const Offer &offer)
{
   SPDLOG_LOGGER_DEBUG(logger_, "update offer from {} (price: {}, amount: {})", peer->toString(), offer.price, offer.amount);

   if (!verifyOffer(offer)) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid offer details");
      return false;
   }

   auto settlementLeaf = findSettlementLeaf(offer.authAddress);
   if (!settlementLeaf) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find settlement leaf with address '{}'", offer.authAddress);
      return false;
   }

   settlementLeaf->getRootPubkey([this, offer, peer, handle = peer->validityFlag.handle()](const SecureBinaryData &ourPubKey) {
      if (handle.isValid()) {
         SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
         return;
      }

      if (peer->state != State::OfferRecv) {
         SPDLOG_LOGGER_ERROR(logger_, "can't pull offer from '{}', we should be in OfferRecv state", peer->toString());
         return;
      }

      if (ourPubKey.getSize() != kPubKeySize) {
         SPDLOG_LOGGER_ERROR(logger_, "invalid auth address root public key");
         return;
      }

      // Only price could be updated, amount and side must be the same
      assert(offer.price != peer->offer.price);
      assert(offer.amount == peer->offer.amount);
      assert(offer.ourSide == peer->offer.ourSide);

      peer->offer = offer;
      peer->ourAuthPubKey = ourPubKey;

      ContactMessage msg;
      if (offer.ourSide == otc::Side::Buy) {
         auto d = msg.mutable_buyer_offers();
         copyOffer(offer, d->mutable_offer());

         d->set_auth_address_buyer(peer->ourAuthPubKey.toBinStr());
      } else {
         auto d = msg.mutable_seller_offers();
         copyOffer(offer, d->mutable_offer());
      }
      send(peer, msg);

      changePeerState(peer, State::OfferSent);
   });

   return true;
}

Peer *OtcClient::ownRequest() const
{
   return ownRequest_.get();
}

void OtcClient::contactConnected(const std::string &contactId)
{
   assert(!contact(contactId));
   contactMap_.emplace(contactId, otc::Peer(contactId, PeerType::Contact));
   emit publicUpdated();
}

void OtcClient::contactDisconnected(const std::string &contactId)
{
   contactMap_.erase(contactId);

   // TODO: Close tradings
   emit publicUpdated();
}

void OtcClient::processContactMessage(const std::string &contactId, const BinaryData &data)
{
   Peer *peer = contact(contactId);
   if (!peer) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find peer '{}'", contactId);
      return;
   }

   if (peer->state == State::Blacklisted) {
      SPDLOG_LOGGER_DEBUG(logger_, "ignoring message from blacklisted peer '{}'", contactId);
      return;
   }

   processPeerMessage(peer, data);
}

void OtcClient::processPbMessage(const std::string &data)
{
   ProxyTerminalPb::Response response;
   bool result = response.ParseFromString(data);
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "can't parse message from PB");
      return;
   }

   SPDLOG_LOGGER_DEBUG(logger_, "process PB message: {}", ProtobufUtils::toJsonCompact(response));

   switch (response.data_case()) {
      case ProxyTerminalPb::Response::kStartOtc:
         processPbStartOtc(response.start_otc());
         return;
      case ProxyTerminalPb::Response::kVerifyOtc:
         processPbVerifyOtc(response.verify_otc());
         return;
      case ProxyTerminalPb::Response::DATA_NOT_SET:
         SPDLOG_LOGGER_ERROR(logger_, "response from PB is invalid");
         return;
   }

   SPDLOG_LOGGER_CRITICAL(logger_, "unknown response was detected!");
}

void OtcClient::processPublicMessage(QDateTime timestamp, const std::string &contactId, const BinaryData &data)
{
   assert(!ownContactId_.empty());
   if (contactId == ownContactId_) {
      return;
   }

   Otc::PublicMessage msg;
   bool result = msg.ParseFromArray(data.getPtr(), int(data.getSize()));
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "parsing public OTC message failed");
      return;
   }

   switch (msg.data_case()) {
      case Otc::PublicMessage::kRequest:
         processPublicRequest(timestamp, contactId, msg.request());
         return;
      case Otc::PublicMessage::kClose:
         processPublicClose(timestamp, contactId, msg.close());
         return;
      case Otc::PublicMessage::kPrivateMessage:
         processPublicPrivateMessage(timestamp, contactId, msg.private_message());
         return;
      case Otc::PublicMessage::DATA_NOT_SET:
         SPDLOG_LOGGER_ERROR(logger_, "invalid public request detected");
         return;
   }

   SPDLOG_LOGGER_CRITICAL(logger_, "unknown public message was detected!");
}

void OtcClient::processPrivateMessage(QDateTime timestamp, const std::string &contactId, bool isResponse, const BinaryData &data)
{
   if (isResponse) {
      if (!ownRequest_) {
         SPDLOG_LOGGER_ERROR(logger_, "response is not expected");
         return;
      }

      auto peer = response(contactId);
      if (!peer) {
         auto result = responseMap_.emplace(contactId, Peer(contactId, PeerType::Response));
         peer = &result.first->second;
      }

      processPeerMessage(peer, data);
      emit publicUpdated();
      return;
   }

   auto peer = request(contactId);
   if (!peer) {
      SPDLOG_LOGGER_ERROR(logger_, "request is not expected");
      return;
   }

   processPeerMessage(peer, data);
}

void OtcClient::onTxSigned(unsigned reqId, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason)
{
   auto it = signRequestIds_.find(reqId);
   if (it == signRequestIds_.end()) {
      return;
   }
   const auto settlementId = std::move(it->second);
   signRequestIds_.erase(it);

   if (result != bs::error::ErrorCode::NoError) {
      // TODO: Cancel deal
      return;
   }

   auto dealIt = deals_.find(settlementId);
   if (dealIt == deals_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unknown sign request");
      return;
   }
   OtcClientDeal *deal = dealIt->second.get();

   if (deal->payinReqId == reqId) {
      if (deal->sellFromOffline) {
         auto loadPath = params_.offlineLoadPathCb();
         if (loadPath.empty()) {
            SPDLOG_LOGGER_DEBUG(logger_, "got empty path to load signed offline request, cancel OTC deal");
            // TODO: Cancel deal
            return;
         }
         QFile f(QString::fromStdString(loadPath));
         bool result = f.open(QIODevice::ReadOnly);
         if (!result) {
            SPDLOG_LOGGER_ERROR(logger_, "can't open file ('{}') to load signed offline request", loadPath);
            // TODO: Report error and cancel deal
            return;
         }
         auto results = ParseOfflineTXFile(f.readAll().toStdString());
         if (results.empty()) {
            SPDLOG_LOGGER_ERROR(logger_, "loading signed offline request failed from '{}'", loadPath);
            // TODO: Report error and cancel deal
            return;
         }
         if (results.size() != 1 || results.front().prevStates.size() != 1) {
            // TODO: Report error and cancel deal
            SPDLOG_LOGGER_DEBUG(logger_, "invalid signed offline request in '{}'", loadPath);
            return;
         }

         // TODO: Verify that we have correct signed request

         SPDLOG_LOGGER_DEBUG(logger_, "pay-in was succesfully signed (using offline wallet), settlementId: {}", deal->settlementId.toHexStr());
         deal->payinSigned = std::move(results.front().prevStates.front());
      } else {
         SPDLOG_LOGGER_DEBUG(logger_, "pay-in was succesfully signed, settlementId: {}", deal->settlementId.toHexStr());
         deal->payinSigned = signedTX;
      }
      trySendSignedTxs(deal);
   }

   if (deal->payoutReqId == reqId) {
      SPDLOG_LOGGER_DEBUG(logger_, "pay-out was succesfully signed, settlementId: {}", deal->settlementId.toHexStr());
      deal->payoutSigned = signedTX;
      trySendSignedTxs(deal);
   }
}

void OtcClient::processPeerMessage(Peer *peer, const BinaryData &data)
{
   ContactMessage message;
   bool result = message.ParseFromArray(data.getPtr(), int(data.getSize()));
   if (!result) {
      blockPeer("can't parse OTC message", peer);
      return;
   }

   switch (message.data_case()) {
      case ContactMessage::kBuyerOffers:
         processBuyerOffers(peer, message.buyer_offers());
         return;
      case ContactMessage::kSellerOffers:
         processSellerOffers(peer, message.seller_offers());
         return;
      case ContactMessage::kBuyerAccepts:
         processBuyerAccepts(peer, message.buyer_accepts());
         return;
      case ContactMessage::kSellerAccepts:
         processSellerAccepts(peer, message.seller_accepts());
         return;
      case ContactMessage::kBuyerAcks:
         processBuyerAcks(peer, message.buyer_acks());
         return;
      case ContactMessage::kClose:
         processClose(peer, message.close());
         return;
      case ContactMessage::kQuoteResponse:
         processQuoteResponse(peer, message.quote_response());
         return;
      case ContactMessage::DATA_NOT_SET:
         blockPeer("unknown or empty OTC message", peer);
         return;
   }

   SPDLOG_LOGGER_CRITICAL(logger_, "unknown response was detected!");
}

void OtcClient::processBuyerOffers(Peer *peer, const ContactMessage_BuyerOffers &msg)
{
   if (!isValidOffer(msg.offer())) {
      blockPeer("invalid offer", peer);
      return;
   }

   if (msg.auth_address_buyer().size() != kPubKeySize) {
      blockPeer("invalid auth_address_buyer in buyer offer", peer);
      return;
   }
   peer->authPubKey = msg.auth_address_buyer();

   switch (peer->state) {
      case State::Idle:
         peer->offer.ourSide = otc::Side::Sell;
         peer->offer.amount = msg.offer().amount();
         peer->offer.price = msg.offer().price();
         changePeerState(peer, State::OfferRecv);
         break;

      case State::OfferSent:
         if (peer->offer.ourSide != otc::Side::Sell) {
            blockPeer("unexpected side in counter-offer", peer);
            return;
         }
         if (peer->offer.amount != msg.offer().amount()) {
            blockPeer("invalid amount in counter-offer", peer);
            return;
         }

         peer->offer.price = msg.offer().price();
         changePeerState(peer, State::OfferRecv);
         break;

      case State::OfferRecv:
      case State::WaitPayinInfo:
      case State::SentPayinInfo:
         blockPeer("unexpected offer", peer);
         break;

      case State::Blacklisted:
         assert(false);
         break;
   }
}

void OtcClient::processSellerOffers(Peer *peer, const ContactMessage_SellerOffers &msg)
{
   if (!isValidOffer(msg.offer())) {
      blockPeer("invalid offer", peer);
      return;
   }

   switch (peer->state) {
      case State::Idle:
         peer->offer.ourSide = otc::Side::Buy;
         peer->offer.amount = msg.offer().amount();
         peer->offer.price = msg.offer().price();
         changePeerState(peer, State::OfferRecv);
         break;

      case State::OfferSent:
         if (peer->offer.ourSide != otc::Side::Buy) {
            blockPeer("unexpected side in counter-offer", peer);
            return;
         }
         if (peer->offer.amount != msg.offer().amount()) {
            blockPeer("invalid amount in counter-offer", peer);
            return;
         }

         peer->offer.price = msg.offer().price();
         changePeerState(peer, State::OfferRecv);
         break;

      case State::OfferRecv:
      case State::WaitPayinInfo:
      case State::SentPayinInfo:
         blockPeer("unexpected offer", peer);
         break;

      case State::Blacklisted:
         assert(false);
         break;
   }
}

void OtcClient::processBuyerAccepts(Peer *peer, const ContactMessage_BuyerAccepts &msg)
{
   if (peer->state != State::OfferSent || peer->offer.ourSide != otc::Side::Sell) {
      blockPeer("unexpected BuyerAccepts message, should be in OfferSent state and be seller", peer);
      return;
   }

   if (msg.offer().price() != peer->offer.price || msg.offer().amount() != peer->offer.amount) {
      blockPeer("wrong accepted price or amount in BuyerAccepts message", peer);
      return;
   }

   if (msg.auth_address_buyer().size() != kPubKeySize) {
      blockPeer("invalid auth_address in BuyerAccepts message", peer);
      return;
   }
   peer->authPubKey = msg.auth_address_buyer();

   sendSellerAccepts(peer);
}

void OtcClient::processSellerAccepts(Peer *peer, const ContactMessage_SellerAccepts &msg)
{
   if (msg.offer().price() != peer->offer.price || msg.offer().amount() != peer->offer.amount) {
      blockPeer("wrong accepted price or amount in SellerAccepts message", peer);
      return;
   }

   if (msg.settlement_id().size() != kSettlementIdSize) {
      blockPeer("invalid settlement_id in SellerAccepts message", peer);
      return;
   }
   const BinaryData settlementId(BinaryData(msg.settlement_id()));

   if (msg.auth_address_seller().size() != kPubKeySize) {
      blockPeer("invalid auth_address_seller in SellerAccepts message", peer);
      return;
   }
   peer->authPubKey = msg.auth_address_seller();

   if (msg.payin_tx_id().size() != kTxHashSize) {
      blockPeer("invalid payin_tx_id in SellerAccepts message", peer);
      return;
   }
   peer->payinTxIdFromSeller = BinaryData(msg.payin_tx_id());

   createRequests(settlementId, peer, [this, peer, settlementId, offer = peer->offer
      , handle = peer->validityFlag.handle()] (OtcClientDeal &&deal)
   {
      if (handle.isValid()) {
         SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
         return;
      }

      if (!deal.success) {
         SPDLOG_LOGGER_ERROR(logger_, "creating pay-out sign request fails: {}", deal.errorMsg);
         return;
      }

      if ((peer->state != State::WaitPayinInfo && peer->state != State::OfferSent) || peer->offer.ourSide != otc::Side::Buy) {
         blockPeer("unexpected SellerAccepts message, should be in WaitPayinInfo or OfferSent state and be buyer", peer);
         return;
      }

      if (offer != peer->offer) {
         SPDLOG_LOGGER_ERROR(logger_, "offer details have changed unexpectedly");
         return;
      }

      auto unsignedPayout = deal.payout.serializeState();
      deals_.emplace(settlementId, std::make_unique<OtcClientDeal>(std::move(deal)));

      ContactMessage msg;
      msg.mutable_buyer_acks()->set_settlement_id(settlementId.toBinStr());
      send(peer, msg);

      changePeerState(peer, State::Idle);

      ProxyTerminalPb::Request request;
      auto d = request.mutable_verify_otc();
      d->set_is_seller(false);
      d->set_price(peer->offer.price);
      d->set_amount(peer->offer.amount);
      d->set_settlement_id(settlementId.toBinStr());
      d->set_auth_address_buyer(peer->ourAuthPubKey.toBinStr());
      d->set_auth_address_seller(peer->authPubKey.toBinStr());
      d->set_unsigned_payout(unsignedPayout.toBinStr());
      d->set_chat_id_buyer(ownContactId_);
      d->set_chat_id_seller(peer->contactId);
      emit sendPbMessage(request.SerializeAsString());

      *peer = Peer(peer->contactId, peer->type);
      emit peerUpdated(peer);
   });
}

void OtcClient::processBuyerAcks(Peer *peer, const ContactMessage_BuyerAcks &msg)
{
   if (peer->state != State::SentPayinInfo || peer->offer.ourSide != otc::Side::Sell) {
      blockPeer("unexpected BuyerAcks message, should be in SentPayinInfo state and be seller", peer);
      return;
   }

   const auto settlementId = BinaryData(msg.settlement_id());

   const auto it = deals_.find(settlementId);
   if (it == deals_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unknown settlementId from BuyerAcks: {}", settlementId.toHexStr());
      return;
   }
   const auto &deal = it->second;
   assert(deal->success);

   ProxyTerminalPb::Request request;
   auto d = request.mutable_verify_otc();
   d->set_is_seller(true);
   d->set_price(peer->offer.price);
   d->set_amount(peer->offer.amount);
   d->set_settlement_id(settlementId.toBinStr());
   d->set_auth_address_buyer(peer->authPubKey.toBinStr());
   d->set_auth_address_seller(peer->ourAuthPubKey.toBinStr());
   d->set_unsigned_payout(deal->payout.serializeState().toBinStr());
   d->set_chat_id_buyer(ownContactId_);
   d->set_chat_id_seller(peer->contactId);
   emit sendPbMessage(request.SerializeAsString());

   *peer = Peer(peer->contactId, peer->type);
   emit peerUpdated(peer);
}

void OtcClient::processClose(Peer *peer, const ContactMessage_Close &msg)
{
   switch (peer->state) {
      case State::QuoteSent:
      case State::QuoteRecv:
      case State::OfferSent:
      case State::OfferRecv: {
         if (peer->type == PeerType::Response) {
            SPDLOG_LOGGER_DEBUG(logger_, "remove active response because peer have sent close message");
            responseMap_.erase(peer->contactId);
            updatePublicLists();
            return;
         }

         Peer newPeer(peer->contactId, peer->type);
         // preserve original request for quote requests (required only for public OTC)
         newPeer.request = std::move(peer->request);
         *peer = std::move(newPeer);
         if (peer->type != PeerType::Contact) {
            updatePublicLists();
         }
         emit peerUpdated(peer);
         break;
      }

      case State::Idle:
      case State::WaitPayinInfo:
      case State::SentPayinInfo:
         blockPeer("unexpected close", peer);
         break;

      case State::Blacklisted:
         assert(false);
         break;
   }
}

void OtcClient::processQuoteResponse(Peer *peer, const ContactMessage_QuoteResponse &msg)
{
   if (!ownRequest_) {
      SPDLOG_LOGGER_ERROR(logger_, "own request is not available");
      return;
   }

   changePeerState(peer, State::QuoteRecv);
   peer->response.ourSide = otc::switchSide(otc::Side(msg.sender_side()));
   copyRange(msg.price(), &peer->response.price);
   copyRange(msg.amount(), &peer->response.amount);

   updatePublicLists();
}

void OtcClient::processPublicRequest(QDateTime timestamp, const std::string &contactId, const PublicMessage_Request &msg)
{
   auto range = otc::RangeType(msg.range());
   if (range < otc::firstRangeValue(params_.env) || range > otc::lastRangeValue(params_.env)) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid range");
      return;
   }

   requestMap_.erase(contactId);
   auto result = requestMap_.emplace(contactId, Peer(contactId, PeerType::Request));
   auto peer = &result.first->second;

   peer->request.ourSide = otc::switchSide(otc::Side(msg.sender_side()));
   peer->request.rangeType = range;
   peer->request.timestamp = timestamp;

   updatePublicLists();
}

void OtcClient::processPublicClose(QDateTime timestamp, const std::string &contactId, const PublicMessage_Close &msg)
{
   requestMap_.erase(contactId);

   // This will remove everything when remote peer pulls public request.
   // We could keep current shield and show that public request was pulled instead.
   responseMap_.erase(contactId);

   updatePublicLists();
}

void OtcClient::processPublicPrivateMessage(QDateTime timestamp, const std::string &contactId, const PublicMessage_PrivateMessage &msg)
{
   // FIXME: Remove this and send messages directly
   processPrivateMessage(timestamp, contactId, msg.is_response(), msg.data());
}

void OtcClient::processPbStartOtc(const ProxyTerminalPb::Response_StartOtc &response)
{
   auto it = waitSettlementIds_.find(response.request_id());
   if (it == waitSettlementIds_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unexpected StartOtc response: can't find request");
      return;
   }
   auto handle = std::move(it->second.handle);
   auto peer = it->second.peer;
   waitSettlementIds_.erase(it);

   const auto settlementId = BinaryData(response.settlement_id());

   if (handle.isValid()) {
      SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
      return;
   }

   createRequests(settlementId, peer, [this, peer, settlementId, offer = peer->offer
      , handle = peer->validityFlag.handle()](OtcClientDeal &&deal)
   {
      if (handle.isValid()) {
         SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
         return;
      }

      if (!deal.success) {
         SPDLOG_LOGGER_ERROR(logger_, "creating pay-in sign request fails: {}", deal.errorMsg);
         return;
      }

      if (offer.ourSide != otc::Side::Sell) {
         SPDLOG_LOGGER_ERROR(logger_, "can't send pay-in info, wrong side");
         return;
      }

      if (offer != peer->offer) {
         SPDLOG_LOGGER_ERROR(logger_, "offer details have changed unexpectedly");
         return;
      }

      auto payinTxId = deal.payin.txId();

      ContactMessage msg;
      auto d = msg.mutable_seller_accepts();
      copyOffer(peer->offer, d->mutable_offer());
      d->set_settlement_id(settlementId.toBinStr());
      d->set_auth_address_seller(peer->ourAuthPubKey.toBinStr());
      d->set_payin_tx_id(payinTxId.toBinStr());
      send(peer, msg);

      changePeerState(peer, State::SentPayinInfo);

      deals_.emplace(settlementId, std::make_unique<OtcClientDeal>(std::move(deal)));
   });
}

void OtcClient::processPbVerifyOtc(const ProxyTerminalPb::Response_VerifyOtc &response)
{
   if (!response.success()) {
      SPDLOG_LOGGER_ERROR(logger_, "OTC verification failed: {}", response.error_msg());
      return;
   }

   const auto settlementId = BinaryData(response.settlement_id());

   auto it = deals_.find(response.settlement_id());
   if (it == deals_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "unknown settlementId in VerifyOtc message");
      return;
   }
   auto deal = it->second.get();

   switch (deal->side) {
      case otc::Side::Buy: {
         assert(deal->payout.isValid());

         bs::core::wallet::SettlementData settlData;
         settlData.settlementId = settlementId;
         settlData.cpPublicKey = deal->cpPubKey;
         settlData.ownKeyFirst = true;

         auto payoutInfo = toPasswordDialogDataPayout(*deal, deal->payout);
         auto reqId = signContainer_->signSettlementPayoutTXRequest(deal->payout, settlData, payoutInfo);
         signRequestIds_[reqId] = settlementId;
         deal->payoutReqId = reqId;
         verifyAuthAddresses(deal);

         break;
      }
      case otc::Side::Sell: {
         assert(deal->payin.isValid());

         bs::core::wallet::SettlementData settlData;
         settlData.settlementId = settlementId;
         settlData.cpPublicKey = deal->cpPubKey;
         settlData.ownKeyFirst = false;

         if (deal->sellFromOffline) {
            SPDLOG_LOGGER_DEBUG(logger_, "sell OTC from offline wallet...");

            auto savePath = params_.offlineSavePathCb(deal->hdWalletId);
            if (savePath.empty()) {
               SPDLOG_LOGGER_DEBUG(logger_, "got empty path to save offline sign request, cancel OTC deal");
               // TODO: Cancel deal
               return;
            }
            deal->payin.offlineFilePath = std::move(savePath);
            auto reqId = signContainer_->signTXRequest(deal->payin);
            signRequestIds_[reqId] = settlementId;
            deal->payinReqId = reqId;
         } else {
            auto payinInfo = toPasswordDialogDataPayin(*deal, deal->payin);
            auto reqId = signContainer_->signSettlementTXRequest(deal->payin, payinInfo);
            signRequestIds_[reqId] = settlementId;
            deal->payinReqId = reqId;
            verifyAuthAddresses(deal);
         }

         break;
      }
      default:
         assert(false);
         break;
   }
}

bool OtcClient::verifyOffer(const Offer &offer) const
{
   assert(offer.ourSide != otc::Side::Unknown);
   assert(offer.amount > 0);
   assert(offer.price > 0);
   assert(!offer.hdWalletId.empty());
   assert(bs::Address(offer.authAddress).isValid());

   if (!offer.recvAddress.empty()) {
      assert(bs::Address(offer.recvAddress).isValid());
      auto wallet = walletsMgr_->getWalletByAddress(offer.recvAddress);

      if (!wallet || wallet->type() != bs::core::wallet::Type::Bitcoin) {
         SPDLOG_LOGGER_CRITICAL(logger_, "invalid receiving address selected for OTC, selected address: {}", offer.recvAddress);
         return false;
      }

      auto hdWalletRecv = walletsMgr_->getHDRootForLeaf(wallet->walletId());
      if (!hdWalletRecv || hdWalletRecv->walletId() != offer.hdWalletId) {
         SPDLOG_LOGGER_CRITICAL(logger_, "invalid receiving address selected for OTC (invalid hd wallet), selected address: {}", offer.recvAddress);
         return false;
      }
   }

   return true;
}

void OtcClient::blockPeer(const std::string &reason, Peer *peer)
{
   SPDLOG_LOGGER_ERROR(logger_, "block broken peer '{}': {}", peer->toString(), reason);
   changePeerState(peer, State::Blacklisted);
   emit peerUpdated(peer);
}

void OtcClient::send(Peer *peer, const ContactMessage &msg)
{
   assert(!peer->contactId.empty());
   switch (peer->type) {
      case PeerType::Contact:
         emit sendContactMessage(peer->contactId, msg.SerializeAsString());
         break;
      case PeerType::Request:
      case PeerType::Response: {
         PublicMessage publicMessage;
         auto d = publicMessage.mutable_private_message();
         d->set_data(msg.SerializeAsString());
         d->set_receiver_id(peer->contactId);
         d->set_is_response(peer->type == PeerType::Request);
         emit sendPublicMessage(publicMessage.SerializeAsString());
         break;
      }
   }
}

void OtcClient::createRequests(const BinaryData &settlementId, Peer *peer, const OtcClientDealCb &cb)
{
   assert(peer->authPubKey.getSize() == kPubKeySize);
   assert(settlementId.getSize() == kSettlementIdSize);
   if (peer->offer.ourSide == bs::network::otc::Side::Buy) {
      assert(peer->payinTxIdFromSeller.getSize() == kTxHashSize);
   }
   assert(!peer->offer.authAddress.empty());

   auto leaf = findSettlementLeaf(peer->offer.authAddress);
   if (!leaf) {
      cb(OtcClientDeal::error("can't find settlement leaf"));
      return;
   }

   leaf->setSettlementID(settlementId, [this, settlementId, peer, cb, handle = peer->validityFlag.handle()](bool result) {
      if (handle.isValid()) {
         SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
         return;
      }

      if (!result) {
         cb(OtcClientDeal::error("setSettlementID failed"));
         return;
      }

      auto cbFee = [this, cb, peer, settlementId, handle](float feePerByte) {
         if (handle.isValid()) {
            SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
            return;
         }

         if (feePerByte < 1) {
            cb(OtcClientDeal::error("invalid fees"));
            return;
         }

         auto primaryHdWallet = walletsMgr_->getPrimaryWallet();
         if (!primaryHdWallet) {
            cb(OtcClientDeal::error("can't find primary wallet"));
            return;
         }

         auto targetHdWallet = walletsMgr_->getHDWalletById(peer->offer.hdWalletId);
         if (!targetHdWallet) {
            cb(OtcClientDeal::error(fmt::format("can't find wallet: {}", peer->offer.hdWalletId)));
            return;
         }

         auto cbSettlAddr = [this, cb, peer, feePerByte, settlementId, targetHdWallet, handle](const bs::Address &settlAddr) {
            if (handle.isValid()) {
               SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
               return;
            }

            if (settlAddr.isNull()) {
               cb(OtcClientDeal::error("invalid settl addr"));
               return;
            }

            const auto changedCallback = nullptr;
            const bool isSegWitInputsOnly = true;
            const bool confirmedOnly = true;
            auto transaction = std::make_shared<TransactionData>(changedCallback, logger_, isSegWitInputsOnly, confirmedOnly);

            auto resetInputsCb = [this, cb, peer, transaction, settlAddr, feePerByte, settlementId, targetHdWallet, handle]() {
               // resetInputsCb will be destroyed when returns, create one more callback to hold variables
               QMetaObject::invokeMethod(this, [this, cb, peer, transaction, settlAddr, feePerByte, settlementId, targetHdWallet, handle] {
                  if (handle.isValid()) {
                     SPDLOG_LOGGER_ERROR(logger_, "peer was destroyed");
                     return;
                  }

                  const double amount = peer->offer.amount / BTCNumericTypes::BalanceDivider;

                  OtcClientDeal result;
                  result.settlementId = settlementId;
                  result.settlementAddr = settlAddr;
                  result.ourAuthAddress = peer->offer.authAddress;
                  result.cpPubKey = peer->authPubKey;
                  result.amount = peer->offer.amount;
                  result.price = peer->offer.price;
                  result.hdWalletId = targetHdWallet->walletId();

                  if (peer->offer.ourSide == bs::network::otc::Side::Sell) {
                     // Seller
                     auto index = transaction->RegisterNewRecipient();
                     assert(index == 0);
                     transaction->UpdateRecipient(0, amount, settlAddr);

                     if (!transaction->IsTransactionValid()) {
                        cb(OtcClientDeal::error("invalid pay-in transaction"));
                        return;
                     }

                     result.success = true;
                     result.side = otc::Side::Sell;
                     result.payin = transaction->createTXRequest();
                     auto payinTxId = result.payin.txId();
                     auto payinUTXO = bs::SettlementMonitor::getInputFromTX(settlAddr, payinTxId, amount);
                     result.fee = int64_t(result.payin.fee);
                     result.sellFromOffline = targetHdWallet->isOffline();
                     cb(std::move(result));
                     return;
                  }

                  // Buyer
                  result.success = true;
                  result.side = otc::Side::Buy;
                  auto outputAddr = peer->offer.recvAddress.empty() ? transaction->GetFallbackRecvAddress() : bs::Address(peer->offer.recvAddress);
                  auto payinUTXO = bs::SettlementMonitor::getInputFromTX(settlAddr, peer->payinTxIdFromSeller, amount);
                  result.payout = bs::SettlementMonitor::createPayoutTXRequest(
                     payinUTXO, outputAddr, feePerByte, armory_->topBlock());
                  result.fee = int64_t(result.payout.fee);
                  cb(std::move(result));
               }, Qt::QueuedConnection);
            };

            transaction->setFeePerByte(feePerByte);

            if (peer->offer.inputs.empty()) {
               transaction->setGroup(targetHdWallet->getGroup(targetHdWallet->getXBTGroupType()), armory_->topBlock(), false, resetInputsCb);
            } else {
               transaction->setGroupAndInputs(targetHdWallet->getGroup(targetHdWallet->getXBTGroupType()), peer->offer.inputs, armory_->topBlock());
               transaction->getSelectedInputs()->SetUseAutoSel(true);
               resetInputsCb();
            }
         };

         const bool myKeyFirst = (peer->offer.ourSide == bs::network::otc::Side::Buy);
         primaryHdWallet->getSettlementPayinAddress(settlementId, peer->authPubKey, cbSettlAddr, myKeyFirst);
      };
      walletsMgr_->estimatedFeePerByte(2, cbFee, this);
   });
}

void OtcClient::sendSellerAccepts(Peer *peer)
{
   int requestId = genLocalUniqueId();
   waitSettlementIds_.emplace(requestId, SettlementIdRequest{peer, peer->validityFlag.handle()});

   ProxyTerminalPb::Request request;
   auto d = request.mutable_start_otc();
   d->set_request_id(requestId);
   emit sendPbMessage(request.SerializeAsString());

   QTimer::singleShot(kStartOtcTimeout, this, [this, requestId] {
      auto it = waitSettlementIds_.find(requestId);
      if (it != waitSettlementIds_.end()) {
         waitSettlementIds_.erase(it);
         SPDLOG_LOGGER_ERROR(logger_, "can't get settlementId from PB: timeout");

         // TODO: Report error that PB is not accessible to peer and cancel deal
      }
   });
}

std::shared_ptr<bs::sync::hd::SettlementLeaf> OtcClient::findSettlementLeaf(const std::string &ourAuthAddress)
{
   auto wallet = walletsMgr_->getPrimaryWallet();
   if (!wallet) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find primary wallet");
      return nullptr;
   }

   auto group = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(wallet->getGroup(bs::hd::BlockSettle_Settlement));
   if (!group) {
      SPDLOG_LOGGER_ERROR(logger_, "don't have settlement group");
      return nullptr;
   }

   return group->getLeaf(bs::Address(ourAuthAddress));
}

void OtcClient::changePeerState(Peer *peer, bs::network::otc::State state)
{
   SPDLOG_LOGGER_DEBUG(logger_, "changing peer '{}' state from {} to {}"
      , peer->toString(), toString(peer->state), toString(state));
   peer->state = state;
   emit peerUpdated(peer);
}

void OtcClient::trySendSignedTxs(OtcClientDeal *deal)
{
   ProxyTerminalPb::Request request;
   auto d = request.mutable_broadcast_xbt();

   switch (deal->side) {
      case otc::Side::Buy:
         d->set_signed_payout(deal->payoutSigned.toBinStr());
         break;
      case otc::Side::Sell:
         d->set_signed_payin(deal->payinSigned.toBinStr());
         break;
      default:
         assert(false);
   }

   d->set_settlement_id(deal->settlementId.toBinStr());
   emit sendPbMessage(request.SerializeAsString());

   setComments(deal);
}

void OtcClient::verifyAuthAddresses(OtcClientDeal *deal)
{
   if (!authAddressManager_) {
      SPDLOG_LOGGER_DEBUG(logger_, "authAddressManager_ is not set, auth address verification skipped");
      return;
   }

   deal->addressVerificator = std::make_unique<AddressVerificator>(logger_, armory_, [this, deal]
      (const bs::Address &address, AddressVerificationState state)
   {
      SPDLOG_LOGGER_DEBUG(logger_, "counterparty's address verification {} for {}", to_string(state), address.display());
      if (state == AddressVerificationState::Verified) {
         bs::sync::PasswordDialogData dialogData;
         dialogData.setValue(deal->isRequestor() ? keys::ResponderAuthAddressVerified : keys::RequesterAuthAddressVerified, true);
         dialogData.setValue(keys::SettlementId, deal->settlementId.toHexStr());
         signContainer_->updateDialogData(dialogData);
      }
   });

   deal->addressVerificator->SetBSAddressList(authAddressManager_->GetBSAddresses());
   // Verify only peer's auth address
   deal->addressVerificator->addAddress(deal->isRequestor() ? deal->responderAuthAddress() : deal->requestorAuthAddress());
   deal->addressVerificator->startAddressVerification();
}

void OtcClient::setComments(OtcClientDeal *deal)
{
   const auto &signedTx = (deal->side == bs::network::otc::Side::Sell) ? deal->payinSigned : deal->payoutSigned;

   auto hdWallet = walletsMgr_->getHDWalletById(deal->hdWalletId);
   auto group = hdWallet ? hdWallet->getGroup(hdWallet->getXBTGroupType()) : nullptr;
   auto leaves = group ? group->getAllLeaves() : std::vector<std::shared_ptr<bs::sync::Wallet>>();
   for (const auto & leaf : leaves) {
      auto comment = fmt::format("{} XBT/EUR @ {} (OTC)"
         , bs::network::otc::toString(deal->side), UiUtils::displayPriceFX(bs::network::otc::fromCents(deal->price)).toStdString());
      leaf->setTransactionComment(signedTx, comment);
   }
}

void OtcClient::updatePublicLists()
{
   contacts_.clear();
   contacts_.reserve(contactMap_.size());
   for (auto &item : contactMap_) {
      contacts_.push_back(&item.second);
   }

   requests_.clear();
   requests_.reserve(requestMap_.size() + (ownRequest_ ? 1 : 0));
   if (ownRequest_) {
      requests_.push_back(ownRequest_.get());
   }
   for (auto &item : requestMap_) {
      requests_.push_back(&item.second);
   }

   responses_.clear();
   responses_.reserve(responseMap_.size());
   for (auto &item : responseMap_) {
      responses_.push_back(&item.second);
   }

   emit publicUpdated();
}
