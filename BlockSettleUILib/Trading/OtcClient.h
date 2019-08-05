#ifndef OTC_CLIENT_H
#define OTC_CLIENT_H

#include <memory>
#include <unordered_map>
#include <QObject>

namespace spdlog {
   class logger;
}

namespace Blocksettle { namespace Communication { namespace Otc {
   enum Side : int;
   class Message;
   class Message_Offer;
   class Message_Accept;
   class Message_Close;
}}}


class OtcClient : public QObject
{
   Q_OBJECT
public:
   OtcClient(const std::shared_ptr<spdlog::logger> &logger, QObject *parent = nullptr);

public slots:
   void peerConnected(const std::string &peerId);
   void peerDisconnected(const std::string &peerId);
   void processMessage(const std::string &peerId, const std::string &data);

signals:
   void sendMessage(const std::string &peerId, const std::string &data);

private:
   enum class State
   {
      // No data received
      Idle,

      // We sent offer
      OfferSent,

      // We recv offer
      OfferRecv,

      // We have accepted recv offer and wait for ack.
      // This should happen without user's intervention.
      WaitAcceptAck,

      // Peer does not comply to protocol, block it
      Blacklisted,
   };

   struct Offer
   {
      Blocksettle::Communication::Otc::Side ourSide{};
      int64_t amount{};
      int64_t price{};
   };

   struct Peer
   {
      std::string peerId;
      Offer offer;
      State state{State::Idle};
   };

   void processOffer(Peer *peer, const Blocksettle::Communication::Otc::Message_Offer &msg);
   void processAccept(Peer *peer, const Blocksettle::Communication::Otc::Message_Accept &msg);
   void processClose(Peer *peer, const Blocksettle::Communication::Otc::Message_Close &msg);

   void blockPeer(const std::string &reason, Peer *peer);

   Peer *findPeer(const std::string &peerId);

   void send(Peer *peer, const Blocksettle::Communication::Otc::Message &msg);

   static std::string stateName(State state);

   std::shared_ptr<spdlog::logger> logger_;
   std::unordered_map<std::string, Peer> peers_;
};

#endif
