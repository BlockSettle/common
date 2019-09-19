#include "ReqXBTSettlementContainer.h"

#include <QApplication>
#include <QPointer>
#include <spdlog/spdlog.h>
#include "AssetManager.h"
#include "AuthAddressManager.h"
#include "CheckRecipSigner.h"
#include "CurrencyPair.h"
#include "QuoteProvider.h"
#include "SignContainer.h"
#include "QuoteProvider.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"
#include "UiUtils.h"

static const unsigned int kWaitTimeoutInSec = 30;

using namespace bs::sync::dialog;

Q_DECLARE_METATYPE(AddressVerificationState)

ReqXBTSettlementContainer::ReqXBTSettlementContainer(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<AuthAddressManager> &authAddrMgr
   , const std::shared_ptr<AssetManager> &assetMgr
   , const std::shared_ptr<SignContainer> &signContainer
   , const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr
   , const bs::network::RFQ &rfq
   , const bs::network::Quote &quote
   , const std::shared_ptr<TransactionData> &txData
   , const bs::Address &authAddr)
   : bs::SettlementContainer()
   , logger_(logger)
   , authAddrMgr_(authAddrMgr)
   , assetMgr_(assetMgr)
   , walletsMgr_(walletsMgr)
   , signContainer_(signContainer)
   , armory_(armory)
   , transactionData_(txData)
   , rfq_(rfq)
   , quote_(quote)
   , clientSells_(!rfq.isXbtBuy())
   , authAddr_(authAddr)
{
   assert(authAddr.isValid());

   qRegisterMetaType<AddressVerificationState>();

   utxoAdapter_ = std::make_shared<bs::UtxoReservation::Adapter>();
   bs::UtxoReservation::addAdapter(utxoAdapter_);

   connect(signContainer_.get(), &SignContainer::QWalletInfo, this, &ReqXBTSettlementContainer::onWalletInfo);
   connect(signContainer_.get(), &SignContainer::TXSigned, this, &ReqXBTSettlementContainer::onTXSigned);

   connect(this, &ReqXBTSettlementContainer::timerExpired, this, &ReqXBTSettlementContainer::onTimerExpired);

   CurrencyPair cp(quote_.security);
   const bool isFxProd = (quote_.product != bs::network::XbtCurrency);
   fxProd_ = cp.ContraCurrency(bs::network::XbtCurrency);
   amount_ = isFxProd ? quantity() / price() : quantity();

   comment_ = std::string(bs::network::Side::toString(bs::network::Side::invert(quote_.side))) + " "
      + quote_.security + " @ " + std::to_string(price());

   dealerTx_ = BinaryData::CreateFromHex(quote_.dealerTransaction);
}

ReqXBTSettlementContainer::~ReqXBTSettlementContainer()
{
   bs::UtxoReservation::delAdapter(utxoAdapter_);
}

unsigned int ReqXBTSettlementContainer::createPayoutTx(const BinaryData& payinHash, double qty
   , const bs::Address &recvAddr)
{
   try {
      const auto txReq = bs::SettlementMonitor::createPayoutTXRequest(
         bs::SettlementMonitor::getInputFromTX(settlAddr_, payinHash, qty), recvAddr
         , transactionData_->GetTransactionSummary().feePerByte, armory_->topBlock());

      bs::sync::PasswordDialogData dlgData = toPayOutTxDetailsPasswordDialogData(txReq);
      dlgData.setValue(keys::Market, "XBT");
      dlgData.setValue(keys::SettlementId, settlementId_.toHexStr());
      dlgData.setValue(keys::ResponderAuthAddressVerified, true);
      dlgData.setValue(keys::SigningAllowed, true);

      // mark revoke tx
      if ((side() == bs::network::Side::Type::Sell && product() == bs::network::XbtCurrency)
          || (side() == bs::network::Side::Type::Buy && product() != bs::network::XbtCurrency)) {
         dlgData.setValue(keys::PayOutRevokeType, true);
         dlgData.setValue(keys::Title, tr("Settlement Pay-Out (For Revoke)"));
         dlgData.setValue(keys::Duration, 30000 - (QDateTime::currentMSecsSinceEpoch() - payinSignedTs_));
      }

      logger_->debug("[{}] pay-out fee={}, qty={} ({}), payin hash={}", __func__
         , txReq.fee, qty, qty * BTCNumericTypes::BalanceDivider, payinHash.toHexStr(true));
      return signContainer_->signSettlementPayoutTXRequest(txReq
         , {settlementId_, dealerAuthKey_, !clientSells_ }, dlgData);
   }
   catch (const std::exception &e) {
      logger_->warn("[{}] failed to create pay-out transaction based on {}: {}", __func__
         , payinHash.toHexStr(), e.what());
      emit error(tr("Pay-out transaction creation failure: %1").arg(QLatin1String(e.what())));
   }
   return 0;
}

void ReqXBTSettlementContainer::acceptSpotXBT()
{
   emit info(tr("Waiting for transactions signing..."));
   if (clientSells_) {
      const auto &cbChangeAddr = [this](const bs::Address &changeAddr) {
         payInTxRequest_ = transactionData_->createTXRequest(false, changeAddr);

         bs::sync::PasswordDialogData dlgData = toPasswordDialogData();
         dlgData.setValue(keys::SettlementPayInVisible, true);

         payinSignedTs_ = QDateTime::currentMSecsSinceEpoch();
         payinSignId_ = signContainer_->signSettlementTXRequest(payInTxRequest_, dlgData);
      };
      if (transactionData_->GetTransactionSummary().hasChange) {
         transactionData_->getWallet()->getNewChangeAddress(cbChangeAddr);
      }
      else {
         cbChangeAddr({});
      }
   }
   else {
      try {    // create payout based on dealer TX
         if (dealerTx_.isNull()) {
            logger_->error("[XBTSettlementTransactionWidget::acceptSpotXBT] empty dealer payin hash");
            emit error(tr("empty dealer payin hash"));
         }
         else {
            payoutSignId_ = createPayoutTx(dealerTx_, amount_, recvAddr_);
         }
      }
      catch (const std::exception &e) {
         logger_->warn("[XBTSettlementTransactionWidget::acceptSpotXBT] Pay-Out failed: {}", e.what());
         emit error(tr("Pay-Out to dealer failed: %1").arg(QString::fromStdString(e.what())));
         return;
      }
   }
}

bool ReqXBTSettlementContainer::startSigning()
{
   if (!payinReceived()) {
      acceptSpotXBT();
   }
   else {
      payoutSignId_ = createPayoutTx(Tx(payinData_).getThisHash(), amount_, recvAddr_);
   }
   return true;
}

bool ReqXBTSettlementContainer::cancel()
{
   deactivate();
   if (clientSells_) {
      if (!payoutData_.isNull()) {
         payoutOnCancel();
      }
      utxoAdapter_->unreserve(id());
   }
   emit settlementCancelled();
   return true;
}

void ReqXBTSettlementContainer::onTimerExpired()
{
   if (!clientSells_ && (waitForPayin_ || waitForPayout_)) {
      emit error(tr("Dealer didn't push transactions to blockchain within a given time interval"));
   }
   else {
      if (clientSells_ && waitForPayout_) {
         waitForPayout_ = false;
      }
   }
   cancel();
}

bool ReqXBTSettlementContainer::isAcceptable() const
{
   return (userKeyOk_ && (dealerVerifState_ == AddressVerificationState::Verified));
}

void ReqXBTSettlementContainer::activate()
{
   startTimer(kWaitTimeoutInSec);

   const auto &authWallet = walletsMgr_->getAuthWallet();
   auto rootAuthWallet = walletsMgr_->getHDRootForLeaf(authWallet->walletId());

   walletInfoAuth_.setName(rootAuthWallet->name());
   walletInfoAuth_.setRootId(rootAuthWallet->walletId());

   walletInfo_.setRootId(walletsMgr_->getHDRootForLeaf(transactionData_->getWallet()->walletId())->walletId());

   if (clientSells_) {
      sellFromPrimary_ = (walletInfoAuth_.rootId() == walletInfo_.rootId());

      emit info(tr("Enter password for \"%1\" wallet to sign Pay-In")
         .arg(QString::fromStdString(walletsMgr_->getHDRootForLeaf(
            transactionData_->getWallet()->walletId())->name())));

      if (!sellFromPrimary_) {
         infoReqIdAuth_ = signContainer_->GetInfo(rootAuthWallet->walletId());
      }
   }

   infoReqId_ = signContainer_->GetInfo(walletInfo_.rootId().toStdString());

   QPointer<ReqXBTSettlementContainer> thisPtr = this;
   addrVerificator_ = std::make_shared<AddressVerificator>(logger_, armory_
      , [thisPtr](const bs::Address &address, AddressVerificationState state)
   {
      QMetaObject::invokeMethod(qApp, [thisPtr, address, state] {
         if (!thisPtr) {
            return;
         }
         thisPtr->dealerAuthAddress_ = address;
         thisPtr->dealerVerifStateChanged(state);
      });
   });
   addrVerificator_->SetBSAddressList(authAddrMgr_->GetBSAddresses());

   settlementId_ = BinaryData::CreateFromHex(quote_.settlementId);
   userKey_ = BinaryData::CreateFromHex(quote_.requestorAuthPublicKey);
   dealerAuthKey_ = BinaryData::CreateFromHex(quote_.dealerAuthPublicKey);

   const auto priWallet = walletsMgr_->getPrimaryWallet();
   if (!priWallet) {
      logger_->error("[{}] missing primary wallet", __func__);
      return;
   }

   const auto group = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(priWallet->getGroup(bs::hd::BlockSettle_Settlement));
   if (!group) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find settlement group");
      return;
   }

   auto settlLeaf = group->getLeaf(authAddr_);
   if (!settlLeaf) {
      SPDLOG_LOGGER_ERROR(logger_, "can't find settlement leaf for auth address '{}'", authAddr_.display());
      return;
   }

   settlLeaf->setSettlementID(settlementId_, [thisPtr](bool success) {
      if (!thisPtr) {
         return;
      }

      if (!success) {
         SPDLOG_LOGGER_ERROR(thisPtr->logger_, "can't find settlement leaf for auth address '{}'", thisPtr->authAddr_.display());
         return;
      }

      thisPtr->activateProceed();
   });
}

void ReqXBTSettlementContainer::deactivate()
{
   stopTimer();
   if (monitor_) {
      monitor_->stop();
   }
}

bs::sync::PasswordDialogData ReqXBTSettlementContainer::toPasswordDialogData() const
{
   bs::sync::PasswordDialogData dialogData = SettlementContainer::toPasswordDialogData();
   dialogData.setValue(keys::Market, "XBT");
   dialogData.setValue(keys::AutoSignCategory, static_cast<int>(bs::signer::AutoSignCategory::SettlementRequestor));

   // rfq details
   QString qtyProd = UiUtils::XbtCurrency;
   QString fxProd = QString::fromStdString(fxProduct());

   dialogData.setValue(keys::Title, tr("Settlement Pay-In"));
   dialogData.setValue(keys::Price, UiUtils::displayPriceXBT(price()));
   dialogData.setValue(keys::FxProduct, fxProd);



   bool isFxProd = (quote_.product != bs::network::XbtCurrency);

   if (isFxProd) {
      dialogData.setValue(keys::Quantity, tr("%1 %2")
                    .arg(UiUtils::displayAmountForProduct(quantity(), fxProd, bs::network::Asset::Type::SpotXBT))
                    .arg(fxProd));

      dialogData.setValue(keys::TotalValue, tr("%1 XBT")
                    .arg(UiUtils::displayAmount(quantity() / price())));
   }
   else {
      dialogData.setValue(keys::Quantity, tr("%1 XBT")
                    .arg(UiUtils::displayAmount(amount())));

      dialogData.setValue(keys::TotalValue, tr("%1 %2")
                    .arg(UiUtils::displayAmountForProduct(amount() * price(), fxProd, bs::network::Asset::Type::SpotXBT))
                    .arg(fxProd));
   }

   // settlement details
   dialogData.setValue(keys::SettlementId, settlementId_.toHexStr());
   dialogData.setValue(keys::SettlementAddress, settlAddr_.display());

   dialogData.setValue(keys::RequesterAuthAddress, authAddr_.display());
   dialogData.setValue(keys::RequesterAuthAddressVerified, true);

   dialogData.setValue(keys::ResponderAuthAddress, bs::Address::fromPubKey(dealerAuthKey_).display());
   dialogData.setValue(keys::ResponderAuthAddressVerified, false);


   // tx details
   dialogData.setValue(keys::TxInputProduct, UiUtils::XbtCurrency);
   dialogData.setValue(keys::TotalSpentVisible, true);

   return dialogData;
}

void ReqXBTSettlementContainer::dealerVerifStateChanged(AddressVerificationState state)
{
   dealerVerifState_ = state;

   bs::sync::PasswordDialogData pd;
   pd.setValue(keys::ResponderAuthAddress, dealerAuthAddress_.display());
   pd.setValue(keys::ResponderAuthAddressVerified, state == AddressVerificationState::Verified);
   pd.setValue(keys::SigningAllowed, state == AddressVerificationState::Verified);
   signContainer_->updateDialogData(pd);
}

void ReqXBTSettlementContainer::activateProceed()
{
   const auto &cbSettlAddr = [this](const bs::Address &addr) {
      settlAddr_ = addr;
      const auto &buyAuthKey = clientSells_ ? dealerAuthKey_ : userKey_;
      const auto &sellAuthKey = clientSells_ ? userKey_ : dealerAuthKey_;

      monitor_ = std::make_shared<bs::SettlementMonitorCb>(armory_, logger_
         , settlAddr_, buyAuthKey, sellAuthKey, [this] {
         monitor_->start([this](int, const BinaryData &) { onPayInZCDetected(); }
         , [this](int confNum, bs::PayoutSigner::Type signedBy) { onPayoutZCDetected(confNum, signedBy); }
         , [](bs::PayoutSigner::Type) {});
      });

      recvAddr_ = transactionData_->GetFallbackRecvAddress();

      const auto recipient = transactionData_->RegisterNewRecipient();
      transactionData_->UpdateRecipientAmount(recipient, amount_, transactionData_->maxSpendAmount());
      transactionData_->UpdateRecipientAddress(recipient, settlAddr_);

      const auto dealerAddrSW = bs::Address::fromPubKey(dealerAuthKey_, AddressEntryType_P2WPKH);
      addrVerificator_->addAddress(dealerAddrSW);
      addrVerificator_->startAddressVerification();

      const auto list = authAddrMgr_->GetVerifiedAddressList();
      const auto userAddress = bs::Address::fromPubKey(userKey_, AddressEntryType_P2WPKH);
      userKeyOk_ = (std::find(list.begin(), list.end(), userAddress) != list.end());
      if (!userKeyOk_) {
         logger_->warn("[ReqXBTSettlementContainer::activate] userAddr {} not found in verified addrs list[{}]"
            , userAddress.display(), list.size());
         return;
      }

      if (clientSells_) {
         if (!transactionData_->IsTransactionValid()) {
            userKeyOk_ = false;
            logger_->error("[ReqXBTSettlementContainer::activate] transaction data is invalid");
            emit error(tr("Transaction data is invalid - sending of pay-in is prohibited"));
            return;
         }
      }

      fee_ = transactionData_->GetTransactionSummary().totalFee;

      startSigning();
   };

   const auto priWallet = walletsMgr_->getPrimaryWallet();
   priWallet->getSettlementPayinAddress(settlementId_, dealerAuthKey_, cbSettlAddr, !clientSells_);
}

void ReqXBTSettlementContainer::onTXSigned(unsigned int id, BinaryData signedTX
   , bs::error::ErrorCode errCode, std::string errTxt)
{
   if (payinSignId_ && (payinSignId_ == id)) {
      payinSignId_ = 0;
      if ((errCode != bs::error::ErrorCode::NoError) || signedTX.isNull()) {
         emit error(tr("Failed to create Pay-In TX - re-type password and try again"));
         logger_->error("[ReqXBTSettlementContainer::onTXSigned] Failed to create pay-in TX: {} ({})"
            , (int)errCode, errTxt);
         emit retry();
         return;
      }
      stopTimer();
      emit stop();
      payinData_ = signedTX;
      payoutSignId_ = createPayoutTx(Tx(payinData_).getThisHash(), amount_, recvAddr_);
   }
   else if (payoutSignId_ && (payoutSignId_ == id)) {
      payoutSignId_ = 0;
      if ((errCode != bs::error::ErrorCode::NoError) || signedTX.isNull()) {
         logger_->warn("[ReqXBTSettlementContainer::onTXSigned] Pay-Out sign failure: {} ({})"
            , (int)errCode, errTxt);
         emit error(tr("Pay-Out signing failed: %1").arg(QString::fromStdString(errTxt)));
         emit retry();
         return;
      }
      payoutData_ = signedTX;
      if (!clientSells_) {
         transactionData_->getWallet()->setTransactionComment(payoutData_, comment_);
//         walletsMgr_->getSettlementWallet()->setTransactionComment(payoutData_, comment_); //TODO: later
      }

      emit info(tr("Waiting for Order verification"));

      waitForPayout_ = waitForPayin_ = true;
      emit acceptQuote(rfq_.requestId, payoutData_.toHexStr());
      startTimer(kWaitTimeoutInSec);
   }
}

void ReqXBTSettlementContainer::payoutOnCancel()
{
   utxoAdapter_->unreserve(id());

   if (!armory_->broadcastZC(payoutData_)) {
      logger_->error("[ReqXBTSettlementContainer::payoutOnCancel] failed to broadcast payout");
      return;
   }
   const std::string revokeComment = "Revoke for " + comment_;
   transactionData_->getWallet()->setTransactionComment(payoutData_, revokeComment);
//   walletsMgr_->getSettlementWallet()->setTransactionComment(payoutData_, revokeComment);  //TODO: later
}

void ReqXBTSettlementContainer::detectDealerTxs()
{
   if (clientSells_)  return;
   if (!waitForPayin_ && !waitForPayout_) {
      emit info(tr("Both pay-in and pay-out transactions were detected!"));
      logger_->debug("[ReqXBTSettlementContainer::detectDealerTxs] Both pay-in and pay-out transactions were detected on requester side");
      deactivate();
      emit settlementAccepted();
   }
}

void ReqXBTSettlementContainer::onPayInZCDetected()
{
   if (waitForPayin_ && !settlementId_.isNull()) {
      waitForPayin_ = false;

      if (clientSells_) {
         waitForPayout_ = true;
         startTimer(kWaitTimeoutInSec);
         emit info(tr("Waiting for dealer's pay-out in blockchain..."));
      }
      else {
         detectDealerTxs();
      }
   }
}

void ReqXBTSettlementContainer::onPayoutZCDetected(int confNum, bs::PayoutSigner::Type signedBy)
{
   Q_UNUSED(confNum);
   logger_->debug("[ReqXBTSettlementContainer::onPayoutZCDetected] signedBy={}"
      , bs::PayoutSigner::toString(signedBy));
   if (waitForPayout_) {
      waitForPayout_ = false;
   }

   if (!settlementId_.isNull()) {
      if (clientSells_) {
         deactivate();
         emit settlementAccepted();
      }
      else {
         detectDealerTxs();
      }
   }
}

void ReqXBTSettlementContainer::OrderReceived()
{
   if (clientSells_) {
      try {
         if (!armory_->broadcastZC(payinData_)) {
            throw std::runtime_error("Failed to bradcast transaction");
         }
         transactionData_->getWallet()->setTransactionComment(payinData_, comment_);
//         walletsMgr_->getSettlementWallet()->setTransactionComment(payinData_, comment_);  //TODO: later

         waitForPayin_ = true;
         logger_->debug("[XBTSettlementTransactionWidget::OrderReceived] Pay-In broadcasted");
         emit info(tr("Waiting for own pay-in in blockchain..."));
      }
      catch (const std::exception &e) {
         logger_->error("[XBTSettlementTransactionWidget::OrderReceived] Pay-In failed: {}", e.what());
         emit error(tr("Sending of Pay-In failed: %1").arg(QString::fromStdString(e.what())));
      }
   }
   else {
      emit info(tr("Waiting for dealer to broadcast both TXes to blockchain"));
   }
}

void ReqXBTSettlementContainer::onWalletInfo(unsigned int reqId, const bs::hd::WalletInfo &walletInfo)
{
   if (infoReqId_ && (reqId == infoReqId_)) {
      infoReqId_ = 0;
      walletInfo_.setEncKeys(walletInfo.encKeys());
      walletInfo_.setEncTypes(walletInfo.encTypes());
      walletInfo_.setKeyRank(walletInfo.keyRank());
   }
   if (infoReqIdAuth_ && (reqId == infoReqIdAuth_)) {
      infoReqIdAuth_ = 0;
      walletInfoAuth_.setEncKeys(walletInfo.encKeys());
      walletInfoAuth_.setEncTypes(walletInfo.encTypes());
      walletInfoAuth_.setKeyRank(walletInfo.keyRank());
      emit authWalletInfoReceived();
   }
}
