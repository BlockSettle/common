/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BIP15xMessage.h"

using namespace bs::network::bip15x;

void MessageBuilder::construct(const uint8_t *data, uint32_t dataSize, MsgType type)
{
   BinaryWriter writer;
   // Store packet length, will be used in chacha20poly1305_get_length later
   writer.put_uint32_t(0);
   writer.put_uint8_t(static_cast<uint8_t>(type));
   writer.put_BinaryData(data, dataSize);
   packet_ = writer.getData();
   uint32_t packetSize = uint32_t(packet_.getSize() - sizeof(uint32_t));
   std::memcpy(packet_.getPtr(), &packetSize, sizeof(packetSize));
}

MessageBuilder::MessageBuilder(const uint8_t *data, uint32_t dataSize, MsgType type)
{
   construct(data, dataSize, type);
}

MessageBuilder::MessageBuilder(const std::vector<uint8_t> &data, MsgType type)
{
   construct(data.data(), data.size(), type);
}

MessageBuilder::MessageBuilder(const BinaryDataRef &data, MsgType type)
{
   construct(data.getPtr(), data.getSize(), type);
}

MessageBuilder::MessageBuilder(const std::string &data, MsgType type)
{
   construct(reinterpret_cast<const uint8_t*>(data.data()), data.size(), type);
}

MessageBuilder::MessageBuilder(MsgType type)
{
   construct(nullptr, 0, type);
}

MessageBuilder &MessageBuilder::encryptIfNeeded(BIP151Connection *conn)
{
   if (!conn) {
      return *this;
   }

   size_t plainTextLen = packet_.getSize();
   size_t cipherTextLen = plainTextLen + POLY1305MACLEN;
   BinaryData packetEnc(cipherTextLen);

   int rc = conn->assemblePacket(packet_.getPtr(), plainTextLen, packetEnc.getPtr(), cipherTextLen);
   if (rc != 0) {
      //failed to encrypt, abort
      throw std::runtime_error("failed to encrypt packet, aborting");
   }
   packet_ = std::move(packetEnc);

   return *this;
}

BinaryData MessageBuilder::build() const
{
   return packet_;
}

Message Message::parse(const BinaryDataRef &packet)
{
   try {
      BinaryRefReader reader(packet);
      uint32_t packetLen = reader.get_uint32_t();
      if (packetLen != reader.getSizeRemaining()) {
         return {};
      }
      const auto type = static_cast<MsgType>(reader.get_uint8_t());
      switch (type)
      {
      case MsgType::SinglePacket:
      case MsgType::AEAD_Setup:
      case MsgType::AEAD_PresentPubkey:
      case MsgType::AEAD_EncInit:
      case MsgType::AEAD_EncAck:
      case MsgType::AEAD_Rekey:
      case MsgType::AuthChallenge:
      case MsgType::AuthReply:
      case MsgType::AuthPropose:
         break;

      default:
         return {};
      }

      Message result;
      result.type_ = type;
      result.data_ = reader.get_BinaryDataRef(static_cast<uint32_t>(reader.getSizeRemaining()));
      return result;
   } catch (...) {
      return {};
   }
}
