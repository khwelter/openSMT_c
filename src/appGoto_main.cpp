#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "openSMT/config/AppConfig.hpp"
#include "openSMT/config/ConfigLoader.hpp"
#include "openSMT/control/DeviceDriverMessage.hpp"

namespace {

constexpr float kDefaultSpeed = 25000.0f;

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

bool parseFloat(const char* text, float& outValue)
{
    if (text == nullptr) {
        return false;
    }

    char* endPtr = nullptr;
    const float value = std::strtof(text, &endPtr);
    if (endPtr == text || (endPtr != nullptr && *endPtr != '\0')) {
        return false;
    }

    outValue = value;
    return true;
}

bool sendMoveXY(
    const std::string& ip,
    std::uint16_t port,
    float xPos,
    float yPos,
    float speed)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appGoto: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appGoto: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    nlohmann::json payloadJson;
    payloadJson["action"] = "moveXY";
    payloadJson["xPos"] = xPos;
    payloadJson["yPos"] = yPos;
    payloadJson["speed"] = speed;
    payloadJson["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = "moveXY";
    frameJson["sourceModule"] = "appGoto";
    frameJson["payloadType"] = opensmt::control::kDeviceCommandPayloadType;
    frameJson["payloadJson"] = payloadJson.dump();

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
        std::fprintf(stderr, "appGoto: send failed\n");
        return false;
    }

    return true;
}

bool resolvePosition(
    const opensmt::config::AppReferencePositionsConfig& positions,
    const std::string& name,
    opensmt::config::Position3Config& outPosition)
{
    if (name == "posCalib") {
        outPosition = positions.posCalib;
        return true;
    }

    if (name == "posCalibSec") {
        outPosition = positions.posCalibSec;
        return true;
    }

    if (name == "posPark") {
        outPosition = positions.posPark;
        return true;
    }

    if (name == "posCamBot") {
        outPosition = positions.posCamBot;
        return true;
    }

    if (name == "posDiscard") {
        outPosition = positions.posDiscard;
        return true;
    }

    if (name == "posChange") {
        outPosition = positions.posChange;
        return true;
    }

    return false;
}

void printUsage()
{
    std::fprintf(stderr, "Usage: ./build/appGoto <positionName> [-f <speed>]\n");
    std::fprintf(stderr, "Known positionName values: posCalib, posCalibSec, posPark, posCamBot, posDiscard, posChange\n");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argv[1] == nullptr) {
        printUsage();
        return 1;
    }

    const std::string positionName = argv[1];
    float speed = kDefaultSpeed;

    int index = 2;
    while (index < argc) {
        const std::string option = argv[index];
        if (option == "-f") {
            if (index + 1 >= argc || !parseFloat(argv[index + 1], speed)) {
                printUsage();
                return 1;
            }
            index += 2;
            continue;
        }

        printUsage();
        return 1;
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;
    if (!configLoader.load("config/config.json", appConfig, errorMessage)) {
        std::fprintf(stderr, "appGoto: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    opensmt::config::Position3Config targetPosition;
    if (!resolvePosition(appConfig.referencePositions, positionName, targetPosition)) {
        std::fprintf(stderr, "appGoto: unknown positionName '%s'\n", positionName.c_str());
        printUsage();
        return 1;
    }

    if (!sendMoveXY(
            appConfig.communication.listenIp,
            appConfig.communication.listenPort,
            targetPosition.x,
            targetPosition.y,
            speed)) {
        return 1;
    }

    std::fprintf(
        stderr,
        "appGoto: moved to %s (x=%.3f, y=%.3f, speed=%.3f)\n",
        positionName.c_str(),
        targetPosition.x,
        targetPosition.y,
        speed);

    return 0;
}
