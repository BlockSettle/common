/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CoreWallet.h"

#include "CoinSelection.h"
#include "Wallets.h"
#include "XBTAmount.h"
#include "ArmoryConnection.h"
#include "Bip39.h"

#define ASSETMETA_PREFIX      0xAC

#include <spdlog/spdlog.h>

#include <sstream>

using namespace ArmorySigner;
using namespace bs::core;

std::shared_ptr<wallet::AssetEntryMeta> wallet::AssetEntryMeta::deserialize(int, BinaryDataRef value)
{
   BinaryRefReader brr(value);

   const auto type = brr.get_uint8_t();
   if (!brr.getSizeRemaining()) {
      throw AssetException("corrupted metadata " + std::to_string(type));
   }

   std::shared_ptr<wallet::AssetEntryMeta> result;
   switch (type) {
   case wallet::AssetEntryMeta::Comment:
      result = std::make_shared<wallet::AssetEntryComment>();
      break;
   case wallet::AssetEntryMeta::Settlement:
      result = std::make_shared<wallet::AssetEntrySettlement>();
      break;
   case wallet::AssetEntryMeta::SettlementCP:
      result = std::make_shared<wallet::AssetEntrySettlCP>();
      break;
   default:
      throw AssetException("unknown meta type " + std::to_string(type));
   }
   if (!result->deserialize(brr)) {
      throw AssetException("failed to read metadata " + std::to_string(type));
   }
   return result;
}


BinaryData wallet::AssetEntryComment::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(type());

   bw.put_var_int(key_.getSize());
   bw.put_BinaryData(key_);

   bw.put_var_int(comment_.length());
   bw.put_BinaryData(BinaryData::fromString(comment_));

   return bw.getData();
}

bool wallet::AssetEntryComment::deserialize(BinaryRefReader brr)
{
   uint64_t len = brr.get_var_int();
   key_ = BinaryData(brr.get_BinaryData(len));

   len = brr.get_var_int();
   comment_ = BinaryData(brr.get_BinaryDataRef(len)).toBinStr();
   return true;
}


BinaryData wallet::AssetEntrySettlement::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(type());

   bw.put_var_int(settlementId_.getSize());
   bw.put_BinaryData(settlementId_);

   const auto &addrStr = authAddr_.display();
   bw.put_var_int(addrStr.length());
   bw.put_BinaryData(BinaryData::fromString(addrStr));

   return bw.getData();
}

bool wallet::AssetEntrySettlement::deserialize(BinaryRefReader brr)
{
   uint64_t len = brr.get_var_int();
   settlementId_ = BinaryData(brr.get_BinaryData(len));

   len = brr.get_var_int();
   authAddr_ = bs::Address::fromAddressString((brr.get_BinaryDataRef(len)).toBinStr());
   return true;
}

BinaryData wallet::AssetEntrySettlCP::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(type());

   bw.put_var_int(txHash_.getSize());
   bw.put_BinaryData(txHash_);

   bw.put_var_int(settlementId_.getSize());
   bw.put_BinaryData(settlementId_);

   bw.put_var_int(cpPubKey_.getSize());
   bw.put_BinaryData(cpPubKey_);

   return bw.getData();
}

bool wallet::AssetEntrySettlCP::deserialize(BinaryRefReader brr)
{
   auto len = brr.get_var_int();
   if (len != 32) {
      throw std::range_error("wrong payin hash size");
   }
   txHash_ = BinaryData(brr.get_BinaryData(len));

   len = brr.get_var_int();
   if (len != 32) {
      throw std::range_error("wrong settlementId size");
   }
   settlementId_ = BinaryData(brr.get_BinaryDataRef(len));

   len = brr.get_var_int();
   cpPubKey_ = BinaryData(brr.get_BinaryDataRef(len));
   return true;
}

void wallet::MetaData::set(const std::shared_ptr<AssetEntryMeta> &value)
{
   data_[value->key()] = value;
}

bool wallet::MetaData::write(const std::shared_ptr<DBIfaceTransaction> &tx)
{
   if (!tx) {
      return false;
   }
   for (const auto value : data_) {
      if (!value.second->needsCommit()) {
         continue;
      }
      const auto itData = data_.find(value.first);
      const bool exists = (itData != data_.end());
      auto id = value.second->getIndex();
      if (exists) {
         id = itData->second->getIndex();
      }

      auto&& serializedEntry = value.second->serialize();

      BinaryWriter bw;
      bw.put_uint8_t(ASSETMETA_PREFIX);
      bw.put_uint32_t(id);
      auto &&dbKey = bw.getData();

      tx->insert(dbKey, serializedEntry);
      value.second->doNotCommit();
   }
   return true;
}

void wallet::MetaData::readFromDB(const std::shared_ptr<DBIfaceTransaction> &tx)
{
   if (!tx) {
      throw WalletException("DB interface is not initialized");
   }

   const auto dbIter = tx->getIterator();

   BinaryWriter bwKey;
   bwKey.put_uint8_t(ASSETMETA_PREFIX);

   dbIter->seek(bwKey.getData());

   while (dbIter->isValid()) {
      const auto keyBDR = dbIter->key();
      const auto valueBDR = dbIter->value();

//      try {
         BinaryRefReader brrKey(keyBDR);

         auto prefix = brrKey.get_uint8_t();
         if (prefix != ASSETMETA_PREFIX) {
            break;
//            throw AssetException("invalid prefix " + std::to_string(prefix));
         }
         auto index = brrKey.get_int32_t();
         nbMetaData_ = index & ~0x100000;

         auto entryPtr = AssetEntryMeta::deserialize(index, valueBDR);
         if (entryPtr) {
            entryPtr->doNotCommit();
            data_[entryPtr->key()] = entryPtr;
         }
/*      }
      catch (AssetException& e) {
         LOGERR << e.what();
         break;
      }*/

      dbIter->advance();
   }
}


bool wallet::TXSignRequest::isValid() const noexcept
{
   // serializedTx will be set for signed offline tx
   if (armorySigner_.getTxInCount() == 0 || 
      armorySigner_.getTxOutCount() == 0) {
      return false;
   }
   
   return true;
}

Signer& wallet::TXSignRequest::getSigner()
{
   return armorySigner_;
}

static UtxoSelection computeSizeAndFee(const std::vector<UTXO> &inUTXOs, const PaymentStruct &inPS)
{
   auto usedUTXOCopy{ inUTXOs };
   UtxoSelection selection{ usedUTXOCopy };

   try {
      selection.computeSizeAndFee(inPS);
   } catch (...) {
   }
   return selection;
}

static size_t getVirtSize(const UtxoSelection &inUTXOSel)
{
   const size_t nonWitSize = inUTXOSel.size_ - inUTXOSel.witnessSize_;
   return std::ceil(static_cast<float>(3 * nonWitSize + inUTXOSel.size_) / 4.0f);
}

size_t wallet::TXSignRequest::estimateTxVirtSize() const
{   // another implementation based on Armory and TransactionData code
   auto transactions = bs::Address::decorateUTXOsCopy(getInputs(nullptr));

   try {
      const PaymentStruct payment(armorySigner_.getRecipientMap(), fee, 0, 0);
      return getVirtSize(computeSizeAndFee(transactions, payment));
   }
   catch (const std::exception &) {}
   return 0;
}

uint64_t wallet::TXSignRequest::amount(const wallet::TXSignRequest::ContainsAddressCb &containsAddressCb) const
{
   // synonym for amountSent
   return amountSent(containsAddressCb);
}

uint64_t wallet::TXSignRequest::inputAmount(const ContainsAddressCb &) const
{
   // calculate total input amount based on spenders
   return armorySigner_.getTotalInputsValue();
}

uint64_t wallet::TXSignRequest::totalSpent(const ContainsAddressCb &containsAddressCb) const
{
   return inputAmount(containsAddressCb) - changeAmount(containsAddressCb);
}

uint64_t wallet::TXSignRequest::changeAmount(const wallet::TXSignRequest::ContainsAddressCb &containsAddressCb) const
{
   // calculate change amount
   // if change is not explicitly set, calculate change using prevStates for provided containsAddressCb

   uint64_t changeVal = 0;
   if (containsAddressCb != nullptr) {
      for (unsigned i=0; i<armorySigner_.getTxOutCount(); i++) {
         auto recip = armorySigner_.getRecipient(i);
         const auto addr = bs::Address::fromRecipient(recip);
         if (containsAddressCb(addr)) {
            uint64_t change = recip->getValue();
            changeVal += change;
         }
      }
   }

   return changeVal;
}

uint64_t wallet::TXSignRequest::amountReceived(const wallet::TXSignRequest::ContainsAddressCb &containsAddressCb) const
{
   // calculate received amount based on recipients
   // containsAddressCb should return true if address is our
   // prevStates recipients parsed first
   // duplicated recipients skipped

   std::set<BinaryData> txSet;
   uint64_t amount = UINT64_MAX;

   if (containsAddressCb != nullptr) {
      for (unsigned i=0; i<armorySigner_.getTxOutCount(); i++) {
         auto recip = armorySigner_.getRecipient(i);
         const auto addr = bs::Address::fromRecipient(recip);
         if (containsAddressCb(addr)) {
               amount += recip->getValue();
         }
      }
   }

   return amount;
}

uint64_t wallet::TXSignRequest::amountSent(const wallet::TXSignRequest::ContainsAddressCb &containsAddressCb) const
{
   // calculate sent amount based on inputs and change
   // containsAddressCb should return true if change address is in our wallet
   // or
   // get sent amount directly from recipients

   return totalSpent(containsAddressCb) - getFee();
}

uint64_t wallet::TXSignRequest::amountReceivedOn(const bs::Address &address, bool) const
{
   // Duplicated recipients removal should be used only for calculaion values in TXInfo for CC settlements
   // to bypass workaround In ReqCCSettlementContainer::createCCUnsignedTXdata()

   std::set<BinaryData> txSet;
   uint64_t amount = 0;

   for (unsigned i=0; i<armorySigner_.getTxOutCount(); i++) {
      auto recip = armorySigner_.getRecipient(i);
      const auto addr = bs::Address::fromRecipient(recip);

      if (addr == address) {
         amount += recip->getValue();
      }
   }

   return amount;
}

uint64_t wallet::TXSignRequest::getFee() const
{
   return armorySigner_.getTotalInputsValue() - armorySigner_.getTotalOutputsValue();
}

std::vector<UTXO> wallet::TXSignRequest::getInputs(const wallet::TXSignRequest::ContainsAddressCb &containsAddressCb) const
{
   std::vector<UTXO> inputsVector;

   for (unsigned i=0; i<armorySigner_.getTxInCount(); i++) {
      auto spender = armorySigner_.getSpender(i);
      const auto &addr = bs::Address::fromScript(spender->getOutputScript());

      if (containsAddressCb && !containsAddressCb(addr)) {
         continue;
      }

      inputsVector.push_back(spender->getUtxo());      
   }

   return inputsVector;
}

std::vector<std::shared_ptr<ScriptRecipient>> wallet::TXSignRequest::getRecipients(const wallet::TXSignRequest::ContainsAddressCb &containsAddressCb) const
{
   std::vector<std::shared_ptr<ScriptRecipient>> recipientsVector;
   
   for (unsigned i=0; i<armorySigner_.getTxOutCount(); i++) {
      auto recip = armorySigner_.getRecipient(i);
      const auto &addr = bs::Address::fromRecipient(recip);

      if (containsAddressCb(addr)) {
         recipientsVector.push_back(recip);
      }
   }

   return recipientsVector;
}

bool wallet::TXSignRequest::isSourceOfTx(const Tx &signedTx) const
{
   try {
      if ((armorySigner_.getTxInCount() != signedTx.getNumTxIn())) {
         return false;
      }

      size_t nbRecipients = armorySigner_.getTxOutCount();
      if (change.value > 0) {
         ++nbRecipients;
      }

      // this->change may contains one of TxOut
      if (signedTx.getNumTxOut() != nbRecipients) {
         return false;
      }

      for (unsigned i = 0; i < signedTx.getNumTxOut(); i++) {
         auto&& txOut = signedTx.getTxOutCopy(i);
         bs::Address txAddr = bs::Address::fromTxOut(txOut);
         uint64_t outValSigned = txOut.getValue();
         uint64_t outValUnsigned = amountReceivedOn(txAddr);

         if (outValUnsigned != outValSigned) {
            return false;
         }
      }

      for (int i = 0; i < signedTx.getNumTxIn(); i++) {
         auto&& txIn = signedTx.getTxInCopy(i);
         OutPoint op = txIn.getOutPoint();

         const auto signedHash = op.getTxHash();
         uint32_t signedTxOutIndex = op.getTxOutIndex();

         bool hasUnsignedInput = false;
         for (int j = 0; j < armorySigner_.getTxInCount(); j++) {
            auto spender = armorySigner_.getSpender(j);
            auto unsignedHash = spender->getOutputHash();
            auto unsignedTxOutIndex = spender->getOutputIndex();

            if (signedHash == unsignedHash && signedTxOutIndex == unsignedTxOutIndex) {
               hasUnsignedInput = true;
               break;
            }
         }

         if (!hasUnsignedInput) {
            return false;
         }
      }
      return true;

   } catch (...) {
      return false;
   }
}

bool wallet::TXMultiSignRequest::isValid() const noexcept
{
   return (armorySigner_.getTxInCount() != 0 &&
      armorySigner_.getTxOutCount() != 0);
}


wallet::Seed::Seed(const SecureBinaryData &seed, NetworkType netType)
   : netType_(netType), seed_(seed)
{
   node_.initFromSeed(seed_);
}

std::string wallet::Seed::getWalletId() const
{
   if (walletId_.empty()) {
/*      const SecureBinaryData hmacMasterMsg("MetaEntry");
      const auto &pubkey = node_.getPublicKey();
      auto &&masterID = BtcUtils::getHMAC256(pubkey, hmacMasterMsg);
      walletId_ = BtcUtils::computeID(masterID).toBinStr();*/

      const auto node = getNode();
      auto chainCode = node.getChaincode();
      DerivationScheme_ArmoryLegacy derScheme(chainCode);

      auto pubKey = node.getPublicKey();
      if (pubKey.empty()) {
         return {};
      }
      auto assetSingle = std::make_shared<AssetEntry_Single>(
         ROOT_ASSETENTRY_ID, BinaryData(), pubKey, nullptr);

      auto addrVec = derScheme.extendPublicChain(assetSingle, 1, 1);
      assert(addrVec.size() == 1);
      auto firstEntry = std::dynamic_pointer_cast<AssetEntry_Single>(addrVec[0]);
      assert(firstEntry != nullptr);
      walletId_ = BtcUtils::computeID(firstEntry->getPubKey()->getUncompressedKey());
      if (*(walletId_.rbegin()) == 0) {
         walletId_.resize(walletId_.size() - 1);
      }
   }
   return walletId_;
}

EasyCoDec::Data wallet::Seed::toEasyCodeChecksum(size_t ckSumSize) const
{
   if (seed_.getSize() == 0)
      throw AssetException("empty seed, cannot generate ez16");

   const size_t halfSize = seed_.getSize() / 2;
   auto privKeyHalf1 = seed_.getSliceCopy(0, (uint32_t)halfSize);
   auto privKeyHalf2 = seed_.getSliceCopy(halfSize, seed_.getSize() - halfSize);
   const auto hash1 = BtcUtils::getHash256(privKeyHalf1);
   const auto hash2 = BtcUtils::getHash256(privKeyHalf2);
   privKeyHalf1.append(hash1.getSliceCopy(0, (uint32_t)ckSumSize));
   privKeyHalf2.append(hash2.getSliceCopy(0, (uint32_t)ckSumSize));
   const auto chkSumPrivKey = privKeyHalf1 + privKeyHalf2;
   return EasyCoDec().fromHex(chkSumPrivKey.toHexStr());
}

SecureBinaryData wallet::Seed::decodeEasyCodeChecksum(const EasyCoDec::Data &easyData, size_t ckSumSize)
{
   auto const privKeyHalf1 = decodeEasyCodeLineChecksum(easyData.part1, ckSumSize);
   auto const privKeyHalf2 = decodeEasyCodeLineChecksum(easyData.part2, ckSumSize);

   return (privKeyHalf1 + privKeyHalf2);
}

BinaryData wallet::Seed::decodeEasyCodeLineChecksum(
   const std::string& easyCodeHalf, size_t ckSumSize, size_t keyValueSize)
{
    const auto& hexStr = EasyCoDec().toHex(easyCodeHalf);
    const auto keyHalfWithChecksum = BinaryData::CreateFromHex(hexStr);

    size_t halfSize = keyValueSize + ckSumSize;

    if (keyHalfWithChecksum.getSize() != halfSize) {
        throw std::invalid_argument("invalid key size");
    }

    const auto privKeyValue = keyHalfWithChecksum.getSliceCopy(0, (uint32_t)keyValueSize);
    const auto hashValue = keyHalfWithChecksum.getSliceCopy(keyValueSize, (uint32_t)ckSumSize);

    if (BtcUtils::getHash256(privKeyValue).getSliceCopy(0, (uint32_t)ckSumSize) != hashValue) {
        throw std::runtime_error("checksum failure");
    }

    return privKeyValue;
}

wallet::Seed wallet::Seed::fromEasyCodeChecksum(const EasyCoDec::Data &easyData, NetworkType netType
   , size_t ckSumSize)
{
   return wallet::Seed(decodeEasyCodeChecksum(easyData, ckSumSize), netType);
}

wallet::Seed wallet::Seed::fromBip39(const std::string& sentence,
   NetworkType netType, const std::vector<std::vector<std::string>>& dictionaries)
{
   wallet::Seed seed(NetworkType::Invalid);
   if (dictionaries.empty()) {
      return seed;
   }

   if (!validateMnemonic(sentence, dictionaries)) {
      return seed;
   }

   SecureBinaryData bip32Seed = bip39GetSeedFromMnemonic(sentence);
   seed = bs::core::wallet::Seed(bip32Seed, netType);

   return seed;
}


SecureBinaryData wallet::Seed::toXpriv() const
{
   return node_.getBase58();
}

wallet::Seed wallet::Seed::fromXpriv(const SecureBinaryData& xpriv, NetworkType netType)
{
   wallet::Seed seed(netType);
   seed.node_.initFromBase58(xpriv);

   //check network

   //check base
   if (seed.node_.getDepth() > 0 || seed.node_.getParentFingerprint() != 0) {
      throw WalletException("xpriv is not for wallet root");
   }
   return seed;
}

////////////////////////////////////////////////////////////////////////////////
Wallet::Wallet(std::shared_ptr<spdlog::logger> logger)
   : wallet::MetaData(), logger_(logger)
{}

Wallet::~Wallet() = default;

std::string Wallet::getAddressComment(const bs::Address &address) const
{
   const auto aeMeta = get(address.id());
   if ((aeMeta == nullptr) || (aeMeta->type() != wallet::AssetEntryMeta::Comment)) {
      return "";
   }
   const auto aeComment = std::dynamic_pointer_cast<wallet::AssetEntryComment>(aeMeta);
   if (aeComment == nullptr) {
      return "";
   }
   return aeComment->comment();
}

bool Wallet::setAddressComment(const bs::Address &address, const std::string &comment)
{
   if (address.empty()) {
      return false;
   }
   set(std::make_shared<wallet::AssetEntryComment>(++nbMetaData_, address.id(), comment));
   return write(getDBWriteTx());
}

std::string Wallet::getTransactionComment(const BinaryData &txHash)
{
   const auto aeMeta = get(txHash);
   if ((aeMeta == nullptr) || (aeMeta->type() != wallet::AssetEntryMeta::Comment)) {
      return {};
   }
   const auto aeComment = std::dynamic_pointer_cast<wallet::AssetEntryComment>(aeMeta);
   return aeComment ? aeComment->comment() : std::string{};
}

bool Wallet::setTransactionComment(const BinaryData &txHash, const std::string &comment)
{
   if (txHash.empty() || comment.empty()) {
      return false;
   }
   set(std::make_shared<wallet::AssetEntryComment>(++nbMetaData_, txHash, comment));
   return write(getDBWriteTx());
}

std::vector<std::pair<BinaryData, std::string>> Wallet::getAllTxComments() const
{
   std::vector<std::pair<BinaryData, std::string>> result;
   for (const auto &data : MetaData::fetchAll()) {
      if (data.first.getSize() == 32) {   //Detect TX hash by size unless other suitable solution is found
         const auto aeComment = std::dynamic_pointer_cast<wallet::AssetEntryComment>(data.second);
         if (aeComment) {
            result.push_back({ data.first, aeComment->comment() });
         }
      }
   }
   return result;
}

bool Wallet::setSettlementMeta(const BinaryData &settlementId, const bs::Address &authAddr)
{
   if (settlementId.empty() || !authAddr.isValid()) {
      return false;
   }
   set(std::make_shared<wallet::AssetEntrySettlement>(++nbMetaData_ + 0x100000
      , settlementId, authAddr));
   return write(getDBWriteTx());
}

bs::Address Wallet::getSettlAuthAddr(const BinaryData &settlementId)
{
   const auto aeMeta = get(settlementId);
   if ((aeMeta == nullptr) || (aeMeta->type() != wallet::AssetEntryMeta::Settlement)) {
      return {};
   }
   const auto aeSettl = std::dynamic_pointer_cast<wallet::AssetEntrySettlement>(aeMeta);
   return aeSettl ? aeSettl->address() : bs::Address{};
}

bool Wallet::setSettlCPMeta(const BinaryData &payinHash, const BinaryData &settlementId
   , const BinaryData &cpPubKey)
{
   if ((payinHash.getSize() != 32) || (settlementId.getSize() != 32) || cpPubKey.empty()) {
      return false;
   }
   auto txHash = payinHash;
   txHash.swapEndian();    // this is to avoid clashing with tx comment key
   set(std::make_shared<wallet::AssetEntrySettlCP>(++nbMetaData_ + 0x100000, txHash, settlementId, cpPubKey));
   return write(getDBWriteTx());
}

std::pair<BinaryData, BinaryData> Wallet::getSettlCP(const BinaryData &txHash)
{
   auto payinHash = txHash;
   payinHash.swapEndian(); // this is to avoid clashing with tx comment key
   const auto aeMeta = get(payinHash);
   if ((aeMeta == nullptr) || (aeMeta->type() != wallet::AssetEntryMeta::SettlementCP)) {
      return {};
   }
   const auto aeSettl = std::dynamic_pointer_cast<wallet::AssetEntrySettlCP>(aeMeta);
   if (aeSettl) {
      return { aeSettl->settlementId(), aeSettl->cpPubKey() };
   }
   return {};
}

Signer Wallet::getSigner(const wallet::TXSignRequest &request,
                             bool keepDuplicatedRecipients)
{
   ArmorySigner::Signer signer(request.armorySigner_);
   signer.resetFeed();
   signer.setFeed(getResolver());
   return signer;
}

BinaryData Wallet::signTXRequest(const wallet::TXSignRequest &request, bool keepDuplicatedRecipients)
{

   auto lock = lockDecryptedContainer();
   auto signer = getSigner(request, keepDuplicatedRecipients);
   signer.sign();
   if (!signer.verify()) {
      throw std::logic_error("signer failed to verify");
   }
   return signer.serializeSignedTx();
}

Codec_SignerState::SignerState Wallet::signPartialTXRequest(const wallet::TXSignRequest &request)
{
   auto lock = lockDecryptedContainer();
   auto signer = getSigner(request, false);
   signer.sign();
   /* TODO: implement partial sig checks correctly
   if (!request.armorySigner_.verifyPartial()) {
      throw std::logic_error("signer failed to verify");
   }*/
   return signer.serializeState();
}

BinaryData Wallet::signTXRequestWithWitness(const wallet::TXSignRequest &request
   , const InputSigs &inputSigs)
{
   if (request.armorySigner_.getTxInCount() != inputSigs.size()) {
      throw std::invalid_argument("inputSigs do not equal to inputs count");
   }

   ArmorySigner::Signer signer(request.armorySigner_);
   for (unsigned i = 0; i < signer.getTxInCount(); ++i) {
      const auto &itSig = inputSigs.find(i);
      if (itSig == inputSigs.end()) {
         throw std::invalid_argument("can't find sig for input #" + std::to_string(i));
      }
      auto sig = SecureBinaryData(itSig->second);
      signer.injectSignature(i, sig);
   }

   return signer.serializeSignedTx();
}


BinaryData bs::core::SignMultiInputTX(const bs::core::wallet::TXMultiSignRequest &txMultiReq
   , const WalletMap &wallets, bool partial)
{
   bs::CheckRecipSigner signer;
   signer.merge(txMultiReq.armorySigner_);
   signer.setFlags(SCRIPT_VERIFY_SEGWIT);

   for (const auto &wallet : wallets) {
      if (wallet.second->isWatchingOnly()) {
         throw std::logic_error("Won't sign with watching-only wallet");
      }
      auto lock = wallet.second->lockDecryptedContainer();
      signer.setFeed(wallet.second->getResolver());
      signer.sign();
      signer.resetFeed();
   }

   if (partial) {
      if (!signer.verifyPartial()) {
         throw std::logic_error("signer failed to verify");
      }
      return BinaryData::fromString(signer.serializeState().SerializeAsString());
   }
   else {
      if (!signer.verify()) {
         std::cout << signer.serializeSignedTx().toHexStr() << std::endl;
         throw std::logic_error("signer failed to verify");
      }
      return signer.serializeSignedTx();
   }
}

BinaryData bs::core::SignMultiInputTXWithWitness(const bs::core::wallet::TXMultiSignRequest &txMultiReq
   , const WalletMap &wallets, const InputSigs &inputSigs)
{
   bs::CheckRecipSigner signer;
   signer.merge(txMultiReq.armorySigner_);

   for (const auto& wltID : txMultiReq.walletIDs_) {
      const auto itWallet = wallets.find(wltID);
      if (itWallet == wallets.end()) {
         throw std::runtime_error("missing wallet for id " + wltID);
      }

      const auto &wallet = itWallet->second;
      signer.resetFeed();
      signer.setFeed(wallet->getResolver());
      signer.resolvePublicData();
   }

   /*for (int i = 0; i < txMultiReq.inputs.size();  ++i) {
      auto inputData = txMultiReq.inputs[i];

      const auto itWallet = wallets.find(inputData.walletId);
      if (itWallet == wallets.end()) {
         throw std::runtime_error("missing wallet for id " + inputData.walletId);
      }
      const auto &wallet = itWallet->second;
      const auto &utxo = inputData.utxo;
      const auto &spender = std::make_shared<ScriptSpender>(utxo);

      if (txMultiReq.RBF) {
         spender->setSequence(UINT32_MAX - 2);
      }
      spender->setFeed(wallet->getPublicResolver());
      spenders[i] = spender;
      signer.addSpender(spender);
      signer.resolvePublicData();
   }

   for (const auto &recipient : txMultiReq.recipients) {
      signer.addRecipient(recipient);
   }*/

   for (const auto &sig : inputSigs) {
      auto sigSBD = SecureBinaryData(sig.second);
      signer.injectSignature(sig.first, sigSBD);
   }

   if (!signer.verify()) {
      throw std::logic_error("signer failed to verify");
   }
   return signer.serializeSignedTx();
}

BinaryData wallet::computeID(const BinaryData &input)
{
   auto result = BtcUtils::computeID(input);
   if (!result.empty() && result.back() == 0) {
      result.pop_back();
   }
   return BinaryData::fromString(result);
}


void wallet::TXSignRequest::DebugPrint(const std::string& prefix, const std::shared_ptr<spdlog::logger>& logger
                                       , bool serializeAndPrint
                                       , const std::shared_ptr<ResolverFeed> &resolver)
{
   std::stringstream ss;

   // wallet ids
   try {
      ss << "   TXSignRequest TX ID:   " << txId(resolver).toHexStr(true) << "\n";
   }
   catch (const std::exception &) { // don't abort process on attempt to get txId
      ss << "   TXSignRequest TX ID:   not exists, yet\n";
   }

   uint64_t inputAmount = 0;
   ss << "      Inputs: " << armorySigner_.getTxInCount() << '\n';
   for (unsigned i=0; i<armorySigner_.getTxInCount(); i++) {
      auto spender = armorySigner_.getSpender(i);
      const auto& utxo = spender->getUtxo();
      ss << "    UTXO txHash : " << utxo.txHash_.toHexStr() << '\n';
      ss << "         txOutIndex : " << utxo.txOutIndex_ << '\n';
      ss << "         txHeight : " << utxo.txHeight_ << '\n';
      ss << "         txIndex : " << utxo.txIndex_ << '\n';
      ss << "         value : " << utxo.value_ << '\n';
      ss << "         script : " << utxo.script_.toHexStr() << '\n';
      ss << "         SW  : " << (utxo.isInputSW_ ? "yes" : "no") << '\n';
      ss << "         txinRedeemSizeBytes: " << utxo.txinRedeemSizeBytes_ << '\n';
      ss << "         witnessDataSizeBytes: " << utxo.witnessDataSizeBytes_ << '\n';
      inputAmount += utxo.getValue();
   }

   // outputs
   ss << "   Outputs: " << armorySigner_.getTxOutCount() << '\n';
   for (unsigned i=0; i<armorySigner_.getTxOutCount(); i++) {
      auto recipient = armorySigner_.getRecipient(i);
      ss << "       Amount: " << recipient->getValue() << '\n';
   }

   // amount
   ss << "    Inputs Amount: " << inputAmount << '\n';
   // change
   if (change.value != 0) {
      ss << "    Change : " << change.value << " to " << change.address.display() << '\n';
   } else  {
      ss << "    No change\n";
   }
   // fee
   ss << "    Fee: " << fee << '\n';

   if (serializeAndPrint) {
      const auto &serialized = serializeState();

      ss << "     Serialized: " << serialized.DebugString() << '\n';
#if 0 // Can't be serialized to Tx anymore
      try {
         Tx tx{serialized};
         std::string payinHash = tx.getThisHash().toHexStr(true);

         ss << "    TX hash: " << payinHash << '\n';
         ss << "    SS info:\n";
         tx.pprintAlot(ss);
      } catch (...) {
         ss << "   error: failed to serialize tx\n";
      }
#endif   //0
   }
   logger->debug("{} :\n{}"
                  ,prefix , ss.str());
}
