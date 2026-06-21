#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "openSMT/config/AppConfig.hpp"
#include "openSMT/config/ConfigLoader.hpp"
#include "openSMT/control/AppControlMessage.hpp"

namespace {

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

bool sendStopFrame(const std::string& ip, std::uint16_t port, const std::string& reason)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appterm: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appterm: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    opensmt::control::AppControlMessage controlMessage;
    controlMessage.command = opensmt::control::AppControlCommand::Stop;
    controlMessage.reason = reason;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = opensmt::control::kAppMainModuleName;
    frameJson["sourceModule"] = opensmt::control::kAppTermModuleName;
    frameJson["payloadType"] = opensmt::control::kAppStopRequestPayloadType;
    frameJson["payloadJson"] = opensmt::control::serializePayloadJson(controlMessage);

    const std::string wireData = frameJson.dump();
    const auto bytesSent = ::sendto(
        socketFd,
        wireData.c_str(),
        wireData.size(),
        0,
        reinterpret_cast<sockaddr*>(&targetAddress),
        sizeof(targetAddress));

    ::close(socketFd);

    if (bytesSent < 0) {
        std::fprintf(stderr, "appterm: send failed\n");
        return false;
    }

    std::fprintf(stderr, "appterm: sent stop request to appmain (%s:%u)\n", ip.c_str(), port);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string configPath = "config/config.json";
    if (argc > 1 && argv[1] != nullptr) {
        configPath = argv[1];
    }

    std::string reason = "operator-request";
    if (argc > 2 && argv[2] != nullptr) {
        reason = argv[2];
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;

    if (!configLoader.load(configPath, appConfig, errorMessage)) {
        std::fprintf(stderr, "appterm: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    if (!sendStopFrame(appConfig.communication.listenIp, appConfig.communication.listenPort, reason)) {
        return 1;
    }

    return 0;
}
