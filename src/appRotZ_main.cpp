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

    outDestination = "rotR" + std::to_string(axisNo);
    return true;
}

bool sendRotate(
    const std::string& ip,
    std::uint16_t port,
    int axisNo,
    float angle,
    float speed)
{
    std::string destinationModule;
    if (!destinationForAxis(axisNo, destinationModule)) {
        std::fprintf(stderr, "appRotZ: axisno must be in range 1..4\n");
        return false;
    }

    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appRotZ: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appRotZ: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    nlohmann::json payloadJson;
    payloadJson["action"] = "rotate";
    payloadJson["angle"] = angle;
    payloadJson["speed"] = speed;
    payloadJson["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = destinationModule;
    frameJson["sourceModule"] = "appRotZ";
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
        std::fprintf(stderr, "appRotZ: send failed\n");
        return false;
    }

    std::fprintf(stderr, "appRotZ: sent rotate axis=%d angle=%.3f speed=%.3f\n", axisNo, angle, speed);
    return true;
}

void printUsage()
{
    std::fprintf(stderr, "Usage: ./build/appRotZ -a <axisno> -d <angle-0..360> [-f <speed>]\n");
}

} // namespace

int main(int argc, char** argv)
{
    int axisNo = 0;
    float angle = 0.0f;
    float speed = kDefaultSpeed;

    bool hasAxis = false;
    bool hasAngle = false;

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

        if (option == "-d") {
            if (index + 1 >= argc || !parseFloat(argv[index + 1], angle)) {
                printUsage();
                return 1;
            }
            hasAngle = true;
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

    if (!hasAxis || !hasAngle) {
        std::fprintf(stderr, "appRotZ: both -a and -d are required\n");
        printUsage();
        return 1;
    }

    if (angle < 0.0f || angle > 360.0f) {
        std::fprintf(stderr, "appRotZ: angle must be in range 0..360\n");
        return 1;
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;
    if (!configLoader.load("config/config.json", appConfig, errorMessage)) {
        std::fprintf(stderr, "appRotZ: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    if (!sendRotate(appConfig.communication.listenIp, appConfig.communication.listenPort, axisNo, angle, speed)) {
        return 1;
    }

    return 0;
}
