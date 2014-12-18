/*  Copyright 2014 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_ROUTING_MESSAGE_HEADER_H_
#define MAIDSAFE_ROUTING_MESSAGE_HEADER_H_

#include <cstdint>
#include <tuple>

#include "maidsafe/common/config.h"

#include "maidsafe/routing/types.h"
#include "maidsafe/routing/utils.h"

namespace maidsafe {

namespace routing {

struct MessageHeader {
  MessageHeader() = default;

  MessageHeader(const MessageHeader&) = delete;

  MessageHeader(MessageHeader&& other) MAIDSAFE_NOEXCEPT
      : destination(std::move(other.destination)),
        source(std::move(other.source)),
        message_id(std::move(other.message_id)),
        checksum_index(std::move(other.checksum_index)),
        checksums(std::move(other.checksums)) {}

  MessageHeader(DestinationAddress destination_in, SourceAddress source_in, MessageId message_id_in,
                uint8_t checksum_index_in, Checksums checksums_in)
      : destination(std::move(destination_in)),
        source(std::move(source_in)),
        message_id(std::move(message_id_in)),
        checksum_index(std::move(checksum_index_in)),
        checksums(std::move(checksums_in)) {}

  MessageHeader(DestinationAddress destination_in, SourceAddress source_in, MessageId message_id_in)
      : destination(std::move(destination_in)),
        source(std::move(source_in)),
        message_id(std::move(message_id_in)),
        checksum_index(0),
        checksums() {}

  ~MessageHeader() = default;

  MessageHeader& operator=(const MessageHeader&) = delete;

  MessageHeader& operator=(MessageHeader&& other) MAIDSAFE_NOEXCEPT {
    destination = std::move(other.destination);
    source = std::move(other.source);
    message_id = std::move(other.message_id);
    checksum_index = std::move(other.checksum_index);
    checksums = std::move(other.checksums);
    return *this;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(destination.data, source.data, message_id.data, checksum_index, checksums);
  }

  DestinationAddress destination;
  SourceAddress source;
  MessageId message_id;
  uint8_t checksum_index;
  Checksums checksums;
};

inline bool operator==(const MessageHeader& lhs, const MessageHeader& rhs) {
  return std::tie(lhs.message_id, lhs.destination, lhs.source) ==
         std::tie(rhs.message_id, rhs.destination, rhs.source);
}

}  // namespace routing

}  // namespace maidsafe

#endif  // MAIDSAFE_ROUTING_MESSAGE_HEADER_H_
