#include "OfflineSigner.h"
#include "Address.h"
#include "signer.pb.h"

#include <QFile>

using namespace Blocksettle;

std::vector<bs::core::wallet::TXSignRequest> bs::core::wallet::ParseOfflineTXFile(const std::string &data)
{
   Storage::Signer::File fileContainer;
   if (!fileContainer.ParseFromString(data)) {
      return {};
   }
   std::vector<bs::core::wallet::TXSignRequest> result;
   for (int i = 0; i < fileContainer.payload_size(); i++) {
      const auto container = fileContainer.payload(i);
      bs::core::wallet::TXSignRequest txReq;
      if (container.type() == Storage::Signer::RequestFileType) {
         Storage::Signer::TXRequest tx;
         if (!tx.ParseFromString(container.data())) {
            continue;
         }
         for (int i = 0; i < tx.walletid_size(); ++i) {
            txReq.walletIds.push_back(tx.walletid(i));
         }
         txReq.fee = tx.fee();
         txReq.RBF = tx.rbf();
         txReq.comment = tx.comment();

         for (int i = 0; i < tx.inputs_size(); i++) {
            UTXO utxo;
            utxo.unserialize(tx.inputs(i).utxo());
            if (utxo.isInitialized()) {
               txReq.inputs.push_back(utxo);
            }
         }

         for (int i = 0; i < tx.recipients_size(); i++) {
            BinaryData serialized = tx.recipients(i);
            const auto recip = ScriptRecipient::deserialize(serialized);
            txReq.recipients.push_back(recip);
         }

         if (tx.has_change()) {
            txReq.change.value = tx.change().value();
            txReq.change.address = tx.change().address().address();
            txReq.change.index = tx.change().address().index();
         }
      }
      else if (container.type() == Storage::Signer::SignedTXFileType) {
         Storage::Signer::SignedTX tx;
         if (!tx.ParseFromString(container.data())) {
            continue;
         }
         txReq.prevStates.push_back(tx.transaction());
         txReq.comment = tx.comment();
      }
      else {
         continue;
      }
      result.push_back(txReq);
   }
   return result;
}

bs::error::ErrorCode bs::core::wallet::ExportTxToFile(const bs::core::wallet::TXSignRequest &txSignReq, const QString &fileNamePath)
{
   if (!txSignReq.isValid()) {
      return bs::error::ErrorCode::TxInvalidRequest;
   }

   Blocksettle::Storage::Signer::TXRequest request;
   for (const auto &walletId : txSignReq.walletIds) {
      request.add_walletid(walletId);
   }

   for (const auto &utxo : txSignReq.inputs) {
      auto input = request.add_inputs();
      input->set_utxo(utxo.serialize().toBinStr());
      const auto addr = bs::Address::fromUTXO(utxo);
      input->mutable_address()->set_address(addr.display());
   }

   for (const auto &recip : txSignReq.recipients) {
      request.add_recipients(recip->getSerializedScript().toBinStr());
   }

   if (txSignReq.fee) {
      request.set_fee(txSignReq.fee);
   }
   if (txSignReq.RBF) {
      request.set_rbf(true);
   }

   if (txSignReq.change.value) {
      auto change = request.mutable_change();
      change->mutable_address()->set_address(txSignReq.change.address.display());
      change->mutable_address()->set_index(txSignReq.change.index);
      change->set_value(txSignReq.change.value);
   }

   if (!txSignReq.comment.empty()) {
      request.set_comment(txSignReq.comment);
   }

   Blocksettle::Storage::Signer::File fileContainer;
   auto container = fileContainer.add_payload();
   container->set_type(Blocksettle::Storage::Signer::RequestFileType);
   container->set_data(request.SerializeAsString());

   QFile f(fileNamePath);

   if (!f.open(QIODevice::WriteOnly)) {
      return bs::error::ErrorCode::TxFailedToOpenRequestFile;
   }

   const auto data = QByteArray::fromStdString(fileContainer.SerializeAsString());
   if (f.write(data) == data.size()) {
      return bs::error::ErrorCode::NoError;
   }
   else {
      return bs::error::ErrorCode::TxFailedToWriteRequestFile;
   }
}
