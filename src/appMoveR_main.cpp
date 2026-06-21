#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "openSMT/config/AppConfig.hpp"
#include "openSMT/config/ConfigLoader.hpp"
#include "openSMT/control/AppControlMessage.hpp"
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

struct DeltaRequest {
    bool hasX = false;
    bool hasY = false;
    bool hasZ1 = false;
    bool hasZ2 = false;
    bool hasZ3 = false;
    bool hasZ4 = false;
    bool hasR1 = false;
    bool hasR2 = false;
    bool hasR3 = false;
    bool hasR4 = false;

    float x = 0.0f;
    float y = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;
    float z3 = 0.0f;
    float z4 = 0.0f;
    float r1 = 0.0f;
    float r2 = 0.0f;
    float r3 = 0.0f;
    float r4 = 0.0f;
};

bool sendFrame(const std::string& ip, std::uint16_t port, const nlohmann::json& frameJson)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        ::close(socketFd);
        return false;
    }

    const std::string wireData = frameJson.dump();
    const auto bytesSent = ::sendto(
        socketFd,
        wireData.c_str(),
        wireData.size(),
        0,
        reinterpret_cast<sockaddr*>(&targetAddress),
        sizeof(targetAddress));

    ::close(socketFd);
    return bytesSent >= 0;
}

bool sendDeviceCommand(
    const std::string& ip,
    std::uint16_t port,
    const std::string& destinationModule,
    const std::string& sourceModule,
    const std::string& action,
    const nlohmann::json& payloadJson)
{
    nlohmann::json payload = payloadJson;
    payload["action"] = action;
    payload["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = destinationModule;
    frameJson["sourceModule"] = sourceModule;
    frameJson["payloadType"] = opensmt::control::kDeviceCommandPayloadType;
    frameJson["payloadJson"] = payload.dump();

    return sendFrame(ip, port, frameJson);
}

bool queryPositions(
    const std::string& ip,
    std::uint16_t port,
    opensmt::control::AppPositionReplyMessage& outReply,
    std::string& outError)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        outError = "socket creation failed";
        return false;
    }

    timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ::close(socketFd);
        outError = "failed to configure receive timeout";
        return false;
    }

    sockaddr_in localAddress;
    std::memset(&localAddress, 0, sizeof(localAddress));
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddress.sin_port = htons(0);

    if (::bind(socketFd, reinterpret_cast<sockaddr*>(&localAddress), sizeof(localAddress)) != 0) {
        ::close(socketFd);
        outError = "bind failed";
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        ::close(socketFd);
        outError = "invalid target ip";
        return false;
    }

    opensmt::control::AppControlMessage queryMessage;
    queryMessage.command = opensmt::control::AppControlCommand::GetPositions;
    queryMessage.reason = "appMoveR relative move";

    nlohmann::json queryFrameJson;
    queryFrameJson["timestampEpochMs"] = nowEpochMs();
    queryFrameJson["destinationModule"] = opensmt::control::kAppMainModuleName;
    queryFrameJson["sourceModule"] = "appMoveR";
    queryFrameJson["payloadType"] = opensmt::control::kAppPositionQueryPayloadType;
    queryFrameJson["payloadJson"] = opensmt::control::serializePayloadJson(queryMessage);

    const std::string wireData = queryFrameJson.dump();
    const auto bytesSent = ::sendto(
        socketFd,
        wireData.c_str(),
        wireData.size(),
        0,
        reinterpret_cast<sockaddr*>(&targetAddress),
        sizeof(targetAddress));

    if (bytesSent < 0) {
        ::close(socketFd);
        outError = "send query failed";
        return false;
    }

    char buffer[8192];
    std::memset(buffer, 0, sizeof(buffer));
    sockaddr_in senderAddress;
    socklen_t senderSize = sizeof(senderAddress);
    const auto bytesRead = ::recvfrom(
        socketFd,
        buffer,
        sizeof(buffer) - 1,
        0,
        reinterpret_cast<sockaddr*>(&senderAddress),
        &senderSize);

    ::close(socketFd);

    if (bytesRead <= 0) {
        outError = "timeout waiting for appmain position reply";
        return false;
    }

    nlohmann::json replyFrameJson;
    try {
        replyFrameJson = nlohmann::json::parse(std::string(buffer, static_cast<std::size_t>(bytesRead)));
    } catch (...) {
        outError = "invalid JSON position reply frame";
        return false;
    }

    if (!replyFrameJson.contains("payloadType") || !replyFrameJson["payloadType"].is_string() ||
        !replyFrameJson.contains("payloadJson") || !replyFrameJson["payloadJson"].is_string()) {
        outError = "position reply frame missing payload fields";
        return false;
    }

    if (replyFrameJson["payloadType"].get<std::string>() != opensmt::control::kAppPositionReplyPayloadType) {
        outError = "unexpected payload type in position reply";
        return false;
    }

    if (!opensmt::control::parsePositionReplyPayloadJson(replyFrameJson["payloadJson"].get<std::string>(), outReply)) {
        outError = "invalid position reply payload";
        return false;
    }

    return true;
}

void printUsage()
{
    std::fprintf(
        stderr,
        "Usage: ./build/appMoveR [-x <dx>] [-y <dy>] [-z1 <dz>] [-z2 <dz>] [-z3 <dz>] [-z4 <dz>] "
        "[-r1 <dr>] [-r2 <dr>] [-r3 <dr>] [-r4 <dr>] [-f <speed>]\n");
}

} // namespace

int main(int argc, char** argv)
{
    DeltaRequest delta;
    float speed = kDefaultSpeed;

    int index = 1;
    while (index < argc) {
        const std::string option = argv[index];

        auto readOptionFloat = [&](bool& hasValue, float& value) {
            if (index + 1 >= argc || !parseFloat(argv[index + 1], value)) {
                return false;
            }
            hasValue = true;
            index += 2;
            return true;
        };

        if (option == "-x") {
            if (!readOptionFloat(delta.hasX, delta.x)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-y") {
            if (!readOptionFloat(delta.hasY, delta.y)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-z1") {
            if (!readOptionFloat(delta.hasZ1, delta.z1)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-z2") {
            if (!readOptionFloat(delta.hasZ2, delta.z2)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-z3") {
            if (!readOptionFloat(delta.hasZ3, delta.z3)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-z4") {
            if (!readOptionFloat(delta.hasZ4, delta.z4)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-r1") {
            if (!readOptionFloat(delta.hasR1, delta.r1)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-r2") {
            if (!readOptionFloat(delta.hasR2, delta.r2)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-r3") {
            if (!readOptionFloat(delta.hasR3, delta.r3)) {
                printUsage();
                return 1;
            }
            continue;
        }
        if (option == "-r4") {
            if (!readOptionFloat(delta.hasR4, delta.r4)) {
                printUsage();
                return 1;
            }
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

    if (!delta.hasX && !delta.hasY && !delta.hasZ1 && !delta.hasZ2 && !delta.hasZ3 && !delta.hasZ4 &&
        !delta.hasR1 && !delta.hasR2 && !delta.hasR3 && !delta.hasR4) {
        std::fprintf(stderr, "appMoveR: provide at least one relative axis argument\n");
        printUsage();
        return 1;
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;

    if (!configLoader.load("config/config.json", appConfig, errorMessage)) {
        std::fprintf(stderr, "appMoveR: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    opensmt::control::AppPositionReplyMessage current;
    if (!queryPositions(appConfig.communication.listenIp, appConfig.communication.listenPort, current, errorMessage)) {
        std::fprintf(stderr, "appMoveR: unable to query global position store from appmain: %s\n", errorMessage.c_str());
        return 1;
    }

    bool allOk = true;

    if (delta.hasX || delta.hasY) {
        nlohmann::json payload = nlohmann::json::object();
        if (delta.hasX) {
            payload["xPos"] = current.axis.x + delta.x;
        }
        if (delta.hasY) {
            payload["yPos"] = current.axis.y + delta.y;
        }
        payload["speed"] = speed;

        if (!sendDeviceCommand(
                appConfig.communication.listenIp,
                appConfig.communication.listenPort,
                "moveXY",
                "appMoveR",
                "moveXY",
                payload)) {
            std::fprintf(stderr, "appMoveR: failed to send moveXY command\n");
            allOk = false;
        }
    }

    const auto sendMoveZ = [&](const char* destination, bool hasDelta, float currentPos, float deltaPos) {
        if (!hasDelta) {
            return;
        }

        nlohmann::json payload;
        payload["zPos"] = currentPos + deltaPos;
        payload["speed"] = speed;
        if (!sendDeviceCommand(
                appConfig.communication.listenIp,
                appConfig.communication.listenPort,
                destination,
                "appMoveR",
                "moveZ",
                payload)) {
            std::fprintf(stderr, "appMoveR: failed to send moveZ command to '%s'\n", destination);
            allOk = false;
        }
    };

    sendMoveZ("moveZ1", delta.hasZ1, current.axis.z1, delta.z1);
    sendMoveZ("moveZ2", delta.hasZ2, current.axis.z2, delta.z2);
    sendMoveZ("moveZ3", delta.hasZ3, current.axis.z3, delta.z3);
    sendMoveZ("moveZ4", delta.hasZ4, current.axis.z4, delta.z4);

    const auto sendRotate = [&](const char* destination, bool hasDelta, float currentPos, float deltaPos) {
        if (!hasDelta) {
            return;
        }

        nlohmann::json payload;
        payload["angle"] = currentPos + deltaPos;
        payload["speed"] = speed;
        if (!sendDeviceCommand(
                appConfig.communication.listenIp,
                appConfig.communication.listenPort,
                destination,
                "appMoveR",
                "rotate",
                payload)) {
            std::fprintf(stderr, "appMoveR: failed to send rotate command to '%s'\n", destination);
            allOk = false;
        }
    };

    sendRotate("rotR1", delta.hasR1, current.axis.r1, delta.r1);
    sendRotate("rotR2", delta.hasR2, current.axis.r2, delta.r2);
    sendRotate("rotR3", delta.hasR3, current.axis.r3, delta.r3);
    sendRotate("rotR4", delta.hasR4, current.axis.r4, delta.r4);

    return allOk ? 0 : 1;
}
