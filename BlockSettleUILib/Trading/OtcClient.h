#ifndef OTC_CLIENT_H
#define OTC_CLIENT_H

#include <functional>
#include <memory>
#include <unordered_map>
#include <QObject>

#include "BSErrorCode.h"
#include "BinaryData.h"
#include "OtcTypes.h"

namespace spdlog {
   class logger;
}

namespace Blocksettle {
   namespace Communication {
      namespace Otc {
         class ContactMessage;
         class ContactMessage_BuyerOffers;
         class ContactMessage_SellerOffers;
         class ContactMessage_BuyerAccepts;
         class ContactMessage_SellerAccepts;
         class ContactMessage_BuyerAcks;
         class ContactMessage_Close;
         class ContactMessage_QuoteResponse;
         class PublicMessage_Request;
         class PublicMessage_Close;
         class PublicMessage_PrivateMessage;
      }
   }
}

namespace Blocksettle {
   namespace Communication {
      namespace ProxyTerminalPb {
         class Response;
         class Response_StartOtc;
         class Response_VerifyOtc;
      }
   }
}

namespace bs {
   class Address;
   namespace core {
      namespace wallet {
         struct TXSignRequest;
      }
   }
   namespace sync {
      class Wallet;
      class WalletsManager;
      namespace hd {
         class SettlementLeaf;
      }
   }
}

class ArmoryConnection;
class AuthAddressManager;
class SignContainer;
struct OtcClientDeal;

struct OtcClientParams
{
   // Return path that will be used to save offline sign request.
   // Must be set if offline wallet will be used for sell.
   std::function<std::string(const std::string &walletId)> offlineSavePathCb;

   // Return path that will be used to load signed offline request.
   // Must be set if offline wallet will be used for sell.
   std::function<std::string()> offlineLoadPathCb;

   bs::network::otc::Env env{};
};

class OtcClient : public QObject
{
   Q_OBJECT

public:
   // authAddressManager could be null. If not set peer's auth address won't be verified (and verification affects only signer UI for now).
   OtcClient(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr
      , const std::shared_ptr<ArmoryConnection> &armory
      , const std::shared_ptr<SignContainer> &signContainer
      , const std::shared_ptr<AuthAddressManager> &authAddressManager
      , OtcClientParams params
      , QObject *parent = nullptr);
   ~OtcClient() override;

   const bs::network::otc::Peer *peer(const std::string &peerId) const;

   void setCurrentUserId(const std::string &userId);
   const std::string &getCurrentUser() const;

   bool sendOffer(const bs::network::otc::Offer &offer, const std::string &peerId);
   bool pullOrRejectOffer(const std::string &peerId);
   bool acceptOffer(const bs::network::otc::Offer &offer, const std::string &peerId);
   bool updateOffer(const bs::network::otc::Offer &offer, const std::string &peerId);

   bool sendQuoteRequest(const bs::network::otc::QuoteRequest &request);
   bool pullOwnRequest();
   bool sendQuoteResponse(const bs::network::otc::QuoteResponse &quoteResponse);
   bool pullQuoteResponse(const std::string &peerId);

   const bs::network::otc::Requests &requests() const { return allRequests_; }
   const bs::network::otc::Request *ownRequest() const;
   const bs::network::otc::Responses &recvResponses() const { return recvResponses_; }
   const bs::network::otc::Responses &sentResponses() const { return sentResponses_; }

public slots:
   void peerConnected(const std::string &peerId);
   void peerDisconnected(const std::string &peerId);
   void processContactMessage(const std::string &peerId, const BinaryData &data);
   void processPbMessage(const std::string &data);
   void processPublicMessage(QDateTime timestamp, const std::string &peerId, const BinaryData &data);
   void processPrivateMessage(QDateTime timestamp, const std::string &peerId, const BinaryData &data);

signals:
   void sendContactMessage(const std::string &peerId, const BinaryData &data);
   void sendPbMessage(const std::string &data);
   void sendPublicMessage(const BinaryData &data);

   void peerUpdated(const std::string &peerId);
   void publicUpdated();

private slots:
   void onTxSigned(unsigned reqId, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason);

private:
   using OtcClientDealCb = std::function<void(OtcClientDeal &&deal)>;

   void processBuyerOffers(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_BuyerOffers &msg);
   void processSellerOffers(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_SellerOffers &msg);
   void processBuyerAccepts(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_BuyerAccepts &msg);
   void processSellerAccepts(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_SellerAccepts &msg);
   void processBuyerAcks(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_BuyerAcks &msg);
   void processClose(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_Close &msg);
   void processQuoteResponse(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage_QuoteResponse &msg);

   void processPublicRequest(QDateTime timestamp, const std::string &peerId, const Blocksettle::Communication::Otc::PublicMessage_Request &msg);
   void processPublicClose(QDateTime timestamp, const std::string &peerId, const Blocksettle::Communication::Otc::PublicMessage_Close &msg);
   void processPublicPrivateMessage(QDateTime timestamp, const std::string &peerId, const Blocksettle::Communication::Otc::PublicMessage_PrivateMessage &msg);

   void processPbStartOtc(const Blocksettle::Communication::ProxyTerminalPb::Response_StartOtc &response);
   void processPbVerifyOtc(const Blocksettle::Communication::ProxyTerminalPb::Response_VerifyOtc &response);

   // Checks that hdWallet, auth address and recv address (is set) are valid
   bool verifyOffer(const bs::network::otc::Offer &offer) const;
   void blockPeer(const std::string &reason, bs::network::otc::Peer *peer);
   bs::network::otc::Peer *findPeer(const std::string &peerId);

   void send(bs::network::otc::Peer *peer, const Blocksettle::Communication::Otc::ContactMessage &msg);

   void createRequests(const BinaryData &settlementId, const bs::network::otc::Peer &peer, const OtcClientDealCb &cb);
   void sendSellerAccepts(bs::network::otc::Peer *peer);

   std::shared_ptr<bs::sync::hd::SettlementLeaf> findSettlementLeaf(const std::string &ourAuthAddress);

   void changePeerState(bs::network::otc::Peer *peer, bs::network::otc::State state);
   int genLocalUniqueId() { return ++latestUniqueId_; }
   void trySendSignedTxs(OtcClientDeal *deal);
   void verifyAuthAddresses(OtcClientDeal *deal);
   void setComments(OtcClientDeal *deal);

   void updatePublicLists();

   void sendPrivateMessage(const std::string &peerId, const BinaryData &data);

   std::shared_ptr<spdlog::logger> logger_;
   std::unordered_map<std::string, bs::network::otc::Peer> peers_;

   std::shared_ptr<bs::sync::WalletsManager> walletsMgr_;
   std::shared_ptr<ArmoryConnection> armory_;
   std::shared_ptr<SignContainer> signContainer_;
   std::shared_ptr<AuthAddressManager> authAddressManager_;

   std::string currentUserId_;

   std::map<BinaryData, std::unique_ptr<OtcClientDeal>> deals_;

   int latestUniqueId_{};
   std::map<int, bs::network::otc::Peer> waitSettlementIds_;

   // Maps sign requests to settlementId
   std::map<unsigned, BinaryData> signRequestIds_;

   std::map<std::string, bs::network::otc::Request> allRequestMap_;
   std::map<std::string, bs::network::otc::Response> sentResponseMap_;
   std::map<std::string, bs::network::otc::Response> recvResponseMap_;

   bs::network::otc::Requests allRequests_;
   bs::network::otc::Responses sentResponses_;
   bs::network::otc::Responses recvResponses_;

   OtcClientParams params_;
};

#endif
