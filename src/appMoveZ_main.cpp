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

constexpr float kDefaultSpeed = 5000.0f;

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

bool parseInt(const char* text, int& outValue)
{
    if (text == nullptr) {
        return false;
    }

    char* endPtr = nullptr;
    const long value = std::strtol(text, &endPtr, 10);
    if (endPtr == text || (endPtr != nullptr && *endPtr != '\0')) {
        return false;
    }

    outValue = static_cast<int>(value);
    return true;
}

bool destinationForAxis(int axisNo, std::string& outDestination)
{
    if (axisNo < 1 || axisNo > 4) {
        return false;
    }

    outDestination = "moveZ" + std::to_string(axisNo);
    return true;
}

bool sendMoveZ(
    const std::string& ip,
    std::uint16_t port,
    int axisNo,
    float zPos,
    float speed)
{
    std::string destinationModule;
    if (!destinationForAxis(axisNo, destinationModule)) {
        std::fprintf(stderr, "appMoveZ: axisno must be in range 1..4\n");
        return false;
    }

    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appMoveZ: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appMoveZ: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    nlohmann::json payloadJson;
    payloadJson["action"] = "moveZ";
    payloadJson["zPos"] = zPos;
    payloadJson["speed"] = speed;
    payloadJson["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = destinationModule;
    frameJson["sourceModule"] = "appMoveZ";
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
        std::fprintf(stderr, "appMoveZ: send failed\n");
        return false;
    }

    std::fprintf(stderr, "appMoveZ: sent moveZ axis=%d z=%.3f speed=%.3f\n", axisNo, zPos, speed);
    return true;
}

void printUsage()
{
    std::fprintf(stderr, "Usage: ./build/appMoveZ -a <axisno> -z <z-position> [-f <speed>]\n");
}

} // namespace

int main(int argc, char** argv)
{
    int axisNo = 0;
    float zPos = 0.0f;
    float speed = kDefaultSpeed;

    bool hasAxis = false;
    bool hasZ = false;

    int index = 1;
    while (index < argc) {
        const std::string option = argv[index];
        if (option == "-a") {
            if (index + 1 >= argc || !parseInt(argv[index + 1], axisNo)) {
                printUsage();
                return 1;
            }
            hasAxis = true;
            index += 2;
            continue;
        }

        if (option == "-z") {
            if (index + 1 >= argc || !parseFloat(argv[index + 1], zPos)) {
                printUsage();
                return 1;
            }
            hasZ = true;
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

    if (!hasAxis || !hasZ) {
        std::fprintf(stderr, "appMoveZ: both -a and -z are required\n");
        printUsage();
        return 1;
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;
    if (!configLoader.load("config/config.json", appConfig, errorMessage)) {
        std::fprintf(stderr, "appMoveZ: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    if (!sendMoveZ(appConfig.communication.listenIp, appConfig.communication.listenPort, axisNo, zPos, speed)) {
        return 1;
    }

    return 0;
}
