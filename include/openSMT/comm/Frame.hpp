#pragma once

#include <cstdint>
#include <string>

namespace opensmt {
namespace comm {

constexpr const char* kBroadcastDestination = "*";

struct Frame {
    std::uint64_t timestampEpochMs;
    std::string destinationModule;
    std::string sourceModule;
    std::string payloadType;
    std::string payloadJson;

    // Filled by MessageBus receiver for UDP-originated frames.
    std::string senderIp;
    std::uint16_t senderPort = 0;
};

} // namespace comm
} // namespace opensmt
