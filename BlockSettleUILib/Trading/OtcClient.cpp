#include "OtcClient.h"

#include <spdlog/spdlog.h>
#include "otc.pb.h"

using namespace Blocksettle::Communication::Otc;

namespace {

   bool isValidSide(Side side)
   {
      switch (side) {
         case SIDE_BUY:
         case SIDE_SELL:
            return true;
         default:
            return false;
      }
   }

   Side switchSide(Side side)
   {
      switch (side) {
         case SIDE_BUY: return SIDE_SELL;
         case SIDE_SELL: return SIDE_BUY;
         default: return SIDE_UNDEFINED;
      }
   }

}

OtcClient::OtcClient(const std::shared_ptr<spdlog::logger> &logger, QObject *parent)
   : QObject (parent)
   , logger_(logger)
{
}

void OtcClient::peerConnected(const std::string &peerId)
{
   Peer *oldPeer = findPeer(peerId);
   assert(!oldPeer);
   if (oldPeer) {
      oldPeer->state = State::Blacklisted;
   }
}

void OtcClient::peerDisconnected(const std::string &peerId)
{

}

void OtcClient::processMessage(const std::string &peerId, const std::string &data)
{
   Peer *peer = findPeer(peerId);
   assert(peer);
   if (!peer) {
      SPDLOG_LOGGER_CRITICAL(logger_, "can't find peer '{}'", peerId);
      return;
   }

   if (peer->state == State::Blacklisted) {
      SPDLOG_LOGGER_DEBUG(logger_, "ignoring message from blacklisted peer '{}'", peerId);
      return;
   }

   Message message;
   bool result = message.ParseFromString(data);
   if (!result) {
      blockPeer("can't parse OTC message", peer);
      return;
   }

   switch (message.data_case()) {
      case Message::kOffer:
         processOffer(peer, message.offer());
         break;
      case Message::kAccept:
         processAccept(peer, message.accept());
         break;
      case Message::kClose:
         processClose(peer, message.close());
         break;
      case Message::DATA_NOT_SET:
         blockPeer("unknown or empty OTC message", peer);
         break;
   }
}

void OtcClient::processOffer(Peer *peer, const Message_Offer &msg)
{
   if (!isValidSide(msg.sender_side())) {
      blockPeer("invalid offer side", peer);
      return;
   }

   if (msg.amount() <= 0) {
      blockPeer("invalid offer amount", peer);
      return;
   }

   if (msg.price() <= 0) {
      blockPeer("invalid offer price", peer);
      return;
   }

   switch (peer->state) {
      case State::Idle:
      case State::OfferSent:
         peer->state = State::OfferRecv;
         peer->offer.ourSide = switchSide(msg.sender_side());
         peer->offer.amount = msg.amount();
         peer->offer.price = msg.price();
         break;

      case State::OfferRecv:
      case State::WaitAcceptAck:
         blockPeer("unexpected offer", peer);
         break;

      case State::Blacklisted:
         assert(false);
         break;
   }
}

void OtcClient::processAccept(Peer *peer, const Message_Accept &msg)
{
   switch (peer->state) {
      case State::OfferSent: {
         if (msg.sender_side() != switchSide(peer->offer.ourSide)) {
            blockPeer("unexpected accepted sender side", peer);
            return;
         }

         if (msg.price() != peer->offer.price || msg.amount() != peer->offer.amount) {
            blockPeer("unexpected accepted price or amount", peer);
            return;
         }

         Message reply;
         auto d = reply.mutable_accept();
         d->set_sender_side(peer->offer.ourSide);
         d->set_amount(peer->offer.amount);
         d->set_price(peer->offer.price);
         send(peer, reply);

         // TODO: Send details to PB
         *peer = Peer();
         break;
      }

      case State::WaitAcceptAck: {
         if (msg.sender_side() != switchSide(peer->offer.ourSide)) {
            blockPeer("unexpected accepted sender side", peer);
            return;
         }

         if (msg.price() != peer->offer.price || msg.amount() != peer->offer.amount) {
            blockPeer("unexpected accepted price or amount", peer);
            return;
         }

         // TODO: Send details to PB
         *peer = Peer();
         break;
      }

      case State::Idle:
      case State::OfferRecv:
         blockPeer("unexpected accept", peer);
         break;

      case State::Blacklisted:
         assert(false);
         break;
   }
}

void OtcClient::processClose(Peer *peer, const Message_Close &msg)
{
   switch (peer->state) {
      case State::OfferSent:
      case State::OfferRecv:
         *peer = Peer();
         break;

      case State::WaitAcceptAck:
      case State::Idle:
         blockPeer("unexpected close", peer);
         break;

      case State::Blacklisted:
         assert(false);
         break;
   }
}

void OtcClient::blockPeer(const std::string &reason, OtcClient::Peer *peer)
{
   SPDLOG_LOGGER_ERROR(logger_, "block broken peer '{}': {}, current state: {}"
      , peer->peerId, reason, stateName(peer->state));
   peer->state = State::Blacklisted;
}

OtcClient::Peer *OtcClient::findPeer(const std::string &peerId)
{
   auto it = peers_.find(peerId);
   if (it == peers_.end()) {
      return nullptr;
   }
   return &it->second;
}

void OtcClient::send(OtcClient::Peer *peer, const Message &msg)
{
   assert(!peer->peerId.empty());
   emit sendMessage(peer->peerId, msg.SerializeAsString());
}

std::string OtcClient::stateName(OtcClient::State state)
{
   switch (state) {
      case State::OfferSent:        return "OfferSent";
      case State::OfferRecv:        return "OfferRecv";
      case State::WaitAcceptAck:    return "WaitAcceptAck";
      case State::Idle:             return "Idle";
      case State::Blacklisted:      return "Blacklisted";
   }
   assert(false);
   return "";
}
