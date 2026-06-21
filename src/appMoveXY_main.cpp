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
    bool hasX,
    float xPos,
    bool hasY,
    float yPos,
    float speed)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appMoveXY: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appMoveXY: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    nlohmann::json payloadJson;
    payloadJson["action"] = "moveXY";
    if (hasX) {
        payloadJson["xPos"] = xPos;
    }
    if (hasY) {
        payloadJson["yPos"] = yPos;
    }
    payloadJson["speed"] = speed;
    payloadJson["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = "moveXY";
    frameJson["sourceModule"] = "appMoveXY";
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
        std::fprintf(stderr, "appMoveXY: send failed\n");
        return false;
    }

    if (hasX && hasY) {
        std::fprintf(stderr, "appMoveXY: sent moveXY x=%.3f y=%.3f speed=%.3f\n", xPos, yPos, speed);
    } else if (hasX) {
        std::fprintf(stderr, "appMoveXY: sent moveXY x=%.3f speed=%.3f\n", xPos, speed);
    } else {
        std::fprintf(stderr, "appMoveXY: sent moveXY y=%.3f speed=%.3f\n", yPos, speed);
    }
    return true;
}

void printUsage()
{
    std::fprintf(stderr, "Usage: ./build/appMoveXY [-x <x-position>] [-y <y-position>] [-f <speed>]\n");
}

} // namespace

int main(int argc, char** argv)
{
    float xPos = 0.0f;
    float yPos = 0.0f;
    float speed = kDefaultSpeed;

    bool hasX = false;
    bool hasY = false;

    int index = 1;
    while (index < argc) {
        const std::string option = argv[index];
        if (option == "-x") {
            if (index + 1 >= argc || !parseFloat(argv[index + 1], xPos)) {
                printUsage();
                return 1;
            }
            hasX = true;
            index += 2;
            continue;
        }

        if (option == "-y") {
            if (index + 1 >= argc || !parseFloat(argv[index + 1], yPos)) {
                printUsage();
                return 1;
            }
            hasY = true;
            index += 2;
            continue;
        }

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

    if (!hasX && !hasY) {
        std::fprintf(stderr, "appMoveXY: provide at least -x or -y\n");
        printUsage();
        return 1;
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;
    if (!configLoader.load("config/config.json", appConfig, errorMessage)) {
        std::fprintf(stderr, "appMoveXY: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    if (!sendMoveXY(appConfig.communication.listenIp, appConfig.communication.listenPort, hasX, xPos, hasY, yPos, speed)) {
        return 1;
    }

    return 0;
}
