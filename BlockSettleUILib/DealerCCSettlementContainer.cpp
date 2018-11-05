#include "DealerCCSettlementContainer.h"
#include <QtConcurrent/QtConcurrentRun>
#include <spdlog/spdlog.h>
#include "CheckRecipSigner.h"
#include "MetaData.h"
#include "SignContainer.h"
#include "TransactionData.h"


DealerCCSettlementContainer::DealerCCSettlementContainer(const std::shared_ptr<spdlog::logger> &logger
      , const bs::network::Order &order, const std::string &quoteReqId, uint64_t lotSize
      , const bs::Address &genAddr, const std::string &ownRecvAddr
      , const std::shared_ptr<TransactionData> &txData, const std::shared_ptr<SignContainer> &container
      , const std::shared_ptr<ArmoryConnection> &armory, bool autoSign)
   : bs::SettlementContainer(armory)
   , logger_(logger)
   , order_(order)
   , quoteReqId_(quoteReqId)
   , lotSize_(lotSize)
   , genesisAddr_(genAddr)
   , autoSign_(autoSign)
   , delivery_(order.side == bs::network::Side::Sell)
   , transactionData_(txData)
   , wallet_(txData->GetSigningWallet())
   , signingContainer_(container)
   , txReqData_(BinaryData::CreateFromHex(order.reqTransaction))
   , ownRecvAddr_(ownRecvAddr)
   , orderId_(QString::fromStdString(order.clOrderId))
   , signer_(armory)
{
   connect(signingContainer_.get(), &SignContainer::TXSigned, this, &DealerCCSettlementContainer::onTXSigned);
   connect(this, &DealerCCSettlementContainer::genAddressVerified, this
      , &DealerCCSettlementContainer::onGenAddressVerified, Qt::QueuedConnection);

   utxoAdapter_ = std::make_shared<bs::UtxoReservation::Adapter>();
   bs::UtxoReservation::addAdapter(utxoAdapter_);

   walletName_ = QString::fromStdString(wallet_->GetWalletName());
}

DealerCCSettlementContainer::~DealerCCSettlementContainer()
{
   bs::UtxoReservation::delAdapter(utxoAdapter_);
}

void DealerCCSettlementContainer::activate()
{
   try {
      signer_.deserializeState(txReqData_);
      foundRecipAddr_ = signer_.findRecipAddress(ownRecvAddr_, [this](uint64_t value, uint64_t valReturn, uint64_t valInput) {
         if ((order_.side == bs::network::Side::Buy) && qFuzzyCompare(order_.quantity, value / lotSize_)) {
            amountValid_ = true; //valInput == (value + valReturn);
         }
         else if ((order_.side == bs::network::Side::Sell) &&
         (value == static_cast<uint64_t>(order_.quantity * order_.price * BTCNumericTypes::BalanceDivider))) {
            amountValid_ = valInput > (value + valReturn);
         }
      });
   }
   catch (const std::exception &e) {
      logger_->error("Signer deser exc: {}", e.what());
      emit genAddressVerified(false);
      return;
   }

   if (!foundRecipAddr_ || !amountValid_) {
      logger_->warn("[DealerCCSettlementContainer::activate] requester's TX verification failed");
      wallet_ = nullptr;
      emit genAddressVerified(false);
   }
   else if (order_.side == bs::network::Side::Buy) {
      emit info(tr("Waiting for genesis address verification to complete..."));
      const auto &cbHasInput = [this](bool has) {
         emit genAddressVerified(has);
      };
      signer_.hasInputAddress(genesisAddr_, cbHasInput, lotSize_);
   }
   else {
      emit genAddressVerified(true);
   }

   startTimer(30);

   signingContainer_->SyncAddresses(transactionData_->createAddresses());
}

void DealerCCSettlementContainer::onGenAddressVerified(bool addressVerified)
{
   genAddrVerified_ = addressVerified;
   if (addressVerified) {
      emit info(tr("Accept offer to send your own signed half of the CoinJoin transaction"));
      emit readyToAccept();
   }
   else {
      logger_->warn("[DealerCCSettlementContainer::onGenAddressVerified] counterparty's TX is unverified");
      emit error(tr("Failed to verify counterparty's transaction"));
      wallet_ = nullptr;
   }
}

bool DealerCCSettlementContainer::isAcceptable() const
{
   return (foundRecipAddr_ && amountValid_ && genAddrVerified_ && wallet_);
}

bool DealerCCSettlementContainer::accept(const SecureBinaryData &password)
{
   if (cancelled_) {
      return false;
   }
   if (!wallet_) {
      logger_->error("[DealerCCSettlementContainer::accept] failed to validate counterparty's TX - aborting");
      emit failed();
      return false;
   }

   bs::wallet::TXSignRequest txReq;
   txReq.walletId = wallet_->GetWalletId();
   txReq.prevStates = { txReqData_ };
   txReq.populateUTXOs = true;
   txReq.inputs = utxoAdapter_->get(id());
   logger_->debug("[DealerCCSettlementContainer::accept] signing with wallet {}, {} inputs"
      , wallet_->GetWalletName(), txReq.inputs.size());
   signId_ = signingContainer_->SignPartialTXRequest(txReq, autoSign_, password);
   emit info(tr("Waiting for TX half signing..."));
   return true;
}

bool DealerCCSettlementContainer::cancel()
{
   utxoAdapter_->unreserve(id());
   cancelled_ = true;
   return true;
}

void DealerCCSettlementContainer::onTXSigned(unsigned int id, BinaryData signedTX,
   std::string errMsg, bool cancelledByUser)
{
   if (signId_ && (signId_ == id)) {
      signId_ = 0;
      if (!errMsg.empty()) {
         logger_->error("[DealerCCSettlementContainer::onTXSigned] failed to sign TX half: {}", errMsg);
         emit error(tr("Create transaction error"));
         emit failed();
         return;
      }
      emit signTxRequest(orderId_, signedTX.toHexStr());
      emit completed();
   }
}

QString DealerCCSettlementContainer::GetSigningWalletName() const
{
   return walletName_;
}
