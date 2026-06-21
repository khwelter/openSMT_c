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

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
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

bool sendActorCommand(
    const std::string& ip,
    std::uint16_t port,
    const std::string& destinationModule,
    int actorIndex,
    int actorValue)
{
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        std::fprintf(stderr, "appActor: socket creation failed\n");
        return false;
    }

    sockaddr_in targetAddress;
    std::memset(&targetAddress, 0, sizeof(targetAddress));
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &targetAddress.sin_addr) != 1) {
        std::fprintf(stderr, "appActor: invalid target IP '%s'\n", ip.c_str());
        ::close(socketFd);
        return false;
    }

    nlohmann::json payloadJson;
    payloadJson["action"] = "operateActorAnalog";
    payloadJson["channel"] = actorIndex;
    payloadJson["value"] = actorValue;
    payloadJson["replyMode"] = opensmt::control::kReplyModeSource;

    nlohmann::json frameJson;
    frameJson["timestampEpochMs"] = nowEpochMs();
    frameJson["destinationModule"] = destinationModule;
    frameJson["sourceModule"] = "appActor";
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
        std::fprintf(stderr, "appActor: send failed\n");
        return false;
    }

    return true;
}

const opensmt::config::ActorConfig* findActorConfig(const opensmt::config::AppConfig& appConfig, const std::string& actorId)
{
    for (const auto& actor : appConfig.actors) {
        if (actor.id == actorId) {
            return &actor;
        }
    }

    return nullptr;
}

bool isAllowedValue(const opensmt::config::ActorConfig& actorConfig, int value)
{
    if (!actorConfig.allowedValues.empty()) {
        for (const int allowedValue : actorConfig.allowedValues) {
            if (allowedValue == value) {
                return true;
            }
        }
        return false;
    }

    return value >= actorConfig.minValue && value <= actorConfig.maxValue;
}

bool resolveActorValue(const opensmt::config::ActorConfig& actorConfig, const std::string& textValue, int& outValue)
{
    if (textValue == "on") {
        outValue = actorConfig.onValue;
        return true;
    }

    if (textValue == "off") {
        outValue = actorConfig.offValue;
        return true;
    }

    int numericValue = 0;
    if (!parseInt(textValue.c_str(), numericValue)) {
        return false;
    }

    outValue = numericValue;
    return true;
}

void printUsage()
{
    std::fprintf(stderr, "Usage: ./build/appActor -a <actorId> -v <value|on|off>\n");
    std::fprintf(stderr, "   or: ./build/appActor <actorId> <value|on|off>\n");
    std::fprintf(stderr, "   or: ./build/appActor -?\n");
}

void printActors(const opensmt::config::AppConfig& appConfig)
{
    std::fprintf(stdout, "Available actors\n");
    for (const auto& actor : appConfig.actors) {
        if (!actor.allowedValues.empty()) {
            std::string valuesText;
            for (std::size_t i = 0; i < actor.allowedValues.size(); ++i) {
                if (i > 0) {
                    valuesText += ", ";
                }
                valuesText += std::to_string(actor.allowedValues[i]);
                if (actor.allowedValues[i] == actor.offValue) {
                    valuesText += "(off)";
                } else if (actor.allowedValues[i] == actor.onValue) {
                    valuesText += "(on)";
                }
            }
            std::fprintf(stdout, "  %-12s  driver=%-10s  index=%d  values=[%s]\n",
                actor.id.c_str(),
                actor.hardwareDriverId.c_str(),
                actor.index,
                valuesText.c_str());
        } else {
            std::fprintf(stdout, "  %-12s  driver=%-10s  index=%d  range=%d..%d  off=%d  on=%d\n",
                actor.id.c_str(),
                actor.hardwareDriverId.c_str(),
                actor.index,
                actor.minValue,
                actor.maxValue,
                actor.offValue,
                actor.onValue);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string actorId;
    std::string valueText;
    bool listActors = false;

    if (argc == 2 && argv[1] != nullptr && std::string(argv[1]) == "-?") {
        listActors = true;
    } else if (argc == 3 && argv[1] != nullptr && argv[2] != nullptr) {
        actorId = argv[1];
        valueText = argv[2];
    } else {
        int index = 1;
        while (index < argc) {
            const std::string option = argv[index];
            if (option == "-?") {
                listActors = true;
                break;
            }
            if (option == "-a") {
                if (index + 1 >= argc || argv[index + 1] == nullptr) {
                    printUsage();
                    return 1;
                }
                actorId = argv[index + 1];
                index += 2;
                continue;
            }

            if (option == "-v") {
                if (index + 1 >= argc || argv[index + 1] == nullptr) {
                    printUsage();
                    return 1;
                }
                valueText = argv[index + 1];
                index += 2;
                continue;
            }

            printUsage();
            return 1;
        }
    }

    opensmt::config::ConfigLoader configLoader;
    opensmt::config::AppConfig appConfig;
    std::string errorMessage;
    if (!configLoader.load("config/config.json", appConfig, errorMessage)) {
        std::fprintf(stderr, "appActor: config load failed: %s\n", errorMessage.c_str());
        return 1;
    }

    if (listActors) {
        printActors(appConfig);
        return 0;
    }

    if (actorId.empty() || valueText.empty()) {
        printUsage();
        return 1;
    }

    const opensmt::config::ActorConfig* actorConfig = findActorConfig(appConfig, actorId);
    if (actorConfig == nullptr) {
        std::fprintf(stderr, "appActor: unknown actor '%s'\n", actorId.c_str());
        return 1;
    }

    int actorValue = 0;
    if (!resolveActorValue(*actorConfig, valueText, actorValue)) {
        std::fprintf(stderr, "appActor: invalid value '%s' for actor '%s'\n", valueText.c_str(), actorId.c_str());
        return 1;
    }

    if (!isAllowedValue(*actorConfig, actorValue)) {
        std::fprintf(
            stderr,
            "appActor: value %d is not allowed for actor '%s' (range %d..%d)\n",
            actorValue,
            actorId.c_str(),
            actorConfig->minValue,
            actorConfig->maxValue);
        return 1;
    }

    if (!sendActorCommand(
            appConfig.communication.listenIp,
            appConfig.communication.listenPort,
            actorConfig->deviceDriverId,
            actorConfig->index,
            actorValue)) {
        return 1;
    }

    std::fprintf(
        stderr,
        "appActor: actor=%s driver=%s deviceDriver=%s index=%d value=%d\n",
        actorConfig->id.c_str(),
        actorConfig->hardwareDriverId.c_str(),
        actorConfig->deviceDriverId.c_str(),
        actorConfig->index,
        actorValue);

    return 0;
}
