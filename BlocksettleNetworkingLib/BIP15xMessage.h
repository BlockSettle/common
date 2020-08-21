/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BIP15X_MESSAGE_H
#define BIP15X_MESSAGE_H

// Messages used for BIP15x transport

#include <spdlog/spdlog.h>
#include "BinaryData.h"
#include "BIP150_151.h"

// The message format is as follows:
//
// Packet length (4 bytes)
// Message type (1 byte)
// Remaining data - Depends.
//
// REMAINING DATA - Single packet
// Message ID (4 bytes - Not in BIP 150/151 handshake packets)
// Payload  (N bytes)
//
// Note that fragments need not be reassembled before decrypting them. It is
// important to note the packet number and parse out the decrypted fragments in
// order. Otherwise, the fragments aren't special in terms of handling. That
// said, to make things easier, it's best to run a map of them through
// BIP15XMessageCodec, which can output single messages for fragmented and
// non-fragmented.

namespace bs {
   namespace network {
      namespace bip15x {

         enum class MsgType : uint8_t {
            Undefined = 0,
            SinglePacket = 1,
            AEAD_Threshold = 10,
            AEAD_Setup = 11,
            AEAD_PresentPubkey = 12,
            AEAD_EncInit = 14,
            AEAD_EncAck = 15,
            AEAD_Rekey = 16,
            AuthThreshold = 20,
            AuthChallenge = 21,
            AuthReply = 22,
            AuthPropose = 23,
         };

         constexpr unsigned int AEAD_REKEY_INTERVAL_SECS = 600;

         // A class used to represent messages on the wire that need to be created.
         class MessageBuilder
         {
         public:
            // Constructs plain packet
            MessageBuilder(const uint8_t *data, uint32_t dataSize, MsgType type);

            // Shotcuts for the first ctor
            MessageBuilder(const std::vector<uint8_t>& data, MsgType type);
            MessageBuilder(const BinaryDataRef& data, MsgType type);
            MessageBuilder(const std::string& data, MsgType type);

            // Constructs plain packet without data
            MessageBuilder(MsgType type);

            // Encrypts plain packet. If conn is not set this is NOOP.
            MessageBuilder &encryptIfNeeded(BIP151Connection *conn);

            // Returns packet that is ready for send
            BinaryData build() const;

         private:
            void construct(const uint8_t *data, uint32_t dataSize, MsgType type);

         private:
            BinaryData packet_;
         };

         // A class used to represent messages on the wire that need to be decrypted.
         class Message
         {
         public:
            // Parse and return immutable message.
            // Message might be invalid if parsing failed (check with isValid)
            // Does not copy underlying raw data, make sure packet is live long enough.
            static Message parse(const BinaryDataRef& packet);

            // Validate if packet is valid before use
            bool isValid() const { return type_ != MsgType::Undefined; }

            // Packet's type (SinglePacket, Heartbeat etc)
            MsgType getType() const { return type_; }

            // Packet's payload
            BinaryDataRef getData() const { return data_; }

         private:
            Message() = default;

         private:
            BinaryDataRef data_;
            MsgType type_{ MsgType::Undefined };
         };

      }  // namespace bip15x
   }  // namespace network
}  // namespace bs

#endif // BIP15X_MESSAGE_H
