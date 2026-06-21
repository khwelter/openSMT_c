#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include <nlohmann/json.hpp>

#include "openSMT/config/AppConfig.hpp"
#include "openSMT/config/ConfigLoader.hpp"
#include "openSMT/control/DeviceDriverMessage.hpp"

namespace {


    
std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

bool sendCommand(
    const std::string& ip,
    std::uint16_t port,
    const std::string& destinationModule,
    const std::string& sourceModule,
    const std::string& action,
    const nlohmann::json& payloadJson)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appHomeZ: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appHomeZ: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    nlohmann::json payload = payloadJson;
    payload["action"] = action;
    payload["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = destinationModule;
    frameJson["sourceModule"] = sourceModule;
    frameJson["payloadType"] = opensmt::control::kDeviceCommandPayloadType;
    frameJson["payloadJson"] = payload.dump();

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
        std::fprintf(stderr, "appHomeZ: send failed for module '%s'\n", destinationModule.c_str());
        return false;
    }

    std::fprintf(stderr, "appHomeZ: sent %s to module '%s' (%s:%u)\n", action.c_str(), destinationModule.c_str(), ip.c_str(), port);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string configPath = "config/config.json";
    if (argc > 1 && argv[1] != nullptr) {
        configPath = argv[1];
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;

    if (!configLoader.load(configPath, appConfig, errorMessage)) {
        std::fprintf(stderr, "appHomeZ: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    const std::vector<std::string> destinations = {"moveZ1", "moveZ2", "moveZ3", "moveZ4"};
    bool allOk = true;
    for (const auto& destination : destinations) {
        if (!sendCommand(
                appConfig.communication.listenIp,
                appConfig.communication.listenPort,
                destination,
                "appHomeZ",
                "homeZ",
                nlohmann::json::object())) {
            allOk = false;
        }
    }

    return allOk ? 0 : 1;
}
