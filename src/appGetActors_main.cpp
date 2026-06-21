#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
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

bool queryActorState(
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
    queryMessage.reason = "appGetActors read";

    nlohmann::json queryFrameJson;
    queryFrameJson["timestampEpochMs"] = nowEpochMs();
    queryFrameJson["destinationModule"] = opensmt::control::kAppMainModuleName;
    queryFrameJson["sourceModule"] = "appGetActors";
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
        outError = "timeout waiting for appmain actor reply";
        return false;
    }

    nlohmann::json replyFrameJson;
    try {
        replyFrameJson = nlohmann::json::parse(std::string(buffer, static_cast<std::size_t>(bytesRead)));
    } catch (...) {
        outError = "invalid JSON actor reply frame";
        return false;
    }

    if (!replyFrameJson.contains("payloadType") || !replyFrameJson["payloadType"].is_string() ||
        !replyFrameJson.contains("payloadJson") || !replyFrameJson["payloadJson"].is_string()) {
        outError = "actor reply frame missing payload fields";
        return false;
    }

    if (replyFrameJson["payloadType"].get<std::string>() != opensmt::control::kAppPositionReplyPayloadType) {
        outError = "unexpected payload type in actor reply";
        return false;
    }

    if (!opensmt::control::parsePositionReplyPayloadJson(replyFrameJson["payloadJson"].get<std::string>(), outReply)) {
        outError = "invalid actor reply payload";
        return false;
    }

    return true;
}

const char* binaryStateText(const opensmt::config::ActorConfig& actor, int value)
{
    if (!actor.allowedValues.empty() && actor.allowedValues.size() <= 2) {
        if (value == actor.offValue) {
            return "off";
        }
        if (value == actor.onValue) {
            return "on";
        }
        return "unknown";
    }

    if (actor.minValue == 0 && actor.maxValue == 1) {
        if (value == 0) {
            return "off";
        }
        if (value == 1) {
            return "on";
        }
    }

    return nullptr;
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
        std::fprintf(stderr, "appGetActors: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    opensmt::control::AppPositionReplyMessage current;
    if (!queryActorState(appConfig.communication.listenIp, appConfig.communication.listenPort, current, errorMessage)) {
        std::fprintf(stderr, "appGetActors: unable to query appmain actor state: %s\n", errorMessage.c_str());
        return 1;
    }

    std::map<std::string, int> currentValues;
    for (const auto& actor : current.actors) {
        currentValues[actor.first] = actor.second;
    }

    std::printf("Actor values\n");
    for (const auto& actorConfig : appConfig.actors) {
        const auto it = currentValues.find(actorConfig.id);
        if (it == currentValues.end()) {
            std::printf("%s=<missing>\n", actorConfig.id.c_str());
            continue;
        }

        const int value = it->second;
        const char* state = binaryStateText(actorConfig, value);
        if (state != nullptr) {
            std::printf("%s=%d state=%s\n", actorConfig.id.c_str(), value, state);
        } else {
            std::printf("%s=%d\n", actorConfig.id.c_str(), value);
        }
    }

    return 0;
}
