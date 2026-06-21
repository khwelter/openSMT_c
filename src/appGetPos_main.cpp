#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
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

namespace {

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
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
    queryMessage.reason = "appGetPos read";

    nlohmann::json queryFrameJson;
    queryFrameJson["timestampEpochMs"] = nowEpochMs();
    queryFrameJson["destinationModule"] = opensmt::control::kAppMainModuleName;
    queryFrameJson["sourceModule"] = "appGetPos";
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

void printPosition3(const char* name, const opensmt::control::AppPosition3& pos)
{
    std::printf("%s: X=%.3f Y=%.3f Z=%.3f\n", name, pos.x, pos.y, pos.z);
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
        std::fprintf(stderr, "appGetPos: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    opensmt::control::AppPositionReplyMessage current;
    if (!queryPositions(appConfig.communication.listenIp, appConfig.communication.listenPort, current, errorMessage)) {
        std::fprintf(stderr, "appGetPos: unable to query appmain global positions: %s\n", errorMessage.c_str());
        return 1;
    }

    std::printf("Global axis positions\n");
    std::printf("X=%.3f Y=%.3f Z1=%.3f Z2=%.3f Z3=%.3f Z4=%.3f R1=%.3f R2=%.3f R3=%.3f R4=%.3f\n",
        current.axis.x,
        current.axis.y,
        current.axis.z1,
        current.axis.z2,
        current.axis.z3,
        current.axis.z4,
        current.axis.r1,
        current.axis.r2,
        current.axis.r3,
        current.axis.r4);

    std::printf("Reference positions\n");
    printPosition3("posCalib", current.reference.posCalib);
    printPosition3("posCalibSec", current.reference.posCalibSec);
    printPosition3("posPark", current.reference.posPark);
    printPosition3("posCamBot", current.reference.posCamBot);
    printPosition3("posDiscard", current.reference.posDiscard);
    printPosition3("posChange", current.reference.posChange);

    std::printf("Actor values\n");
    for (const auto& actor : current.actors) {
        std::printf("%s=%d\n", actor.first.c_str(), actor.second);
    }

    return 0;
}
