#include "openSMT/App.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "openSMT/config/ConfigLoader.hpp"
#include "openSMT/control/AppControlMessage.hpp"
#include "openSMT/control/DeviceDriverMessage.hpp"
#include "openSMT/modules/CommunicationMonitorModule.hpp"
#include "openSMT/modules/DeviceDriverRuntimeModule.hpp"
#include "openSMT/modules/PlacerModule.hpp"

namespace opensmt {

namespace {

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

bool parseFloatField(const nlohmann::json& json, const char* key, float& outValue)
{
    if (!json.contains(key) || !json[key].is_number()) {
        return false;
    }

    outValue = json[key].get<float>();
    return true;
}

bool readStringArrayField(const nlohmann::json& json, const char* key, std::vector<std::string>& outValues)
{
    if (!json.contains(key) || !json[key].is_array()) {
        return false;
    }

    outValues.clear();
    for (const auto& entry : json[key]) {
        if (!entry.is_string()) {
            return false;
        }
        outValues.push_back(entry.get<std::string>());
    }

    return true;
}

} // namespace

bool App::run(const std::string& rootConfigPath)
{
    stopRequested_ = false;

    config::ConfigLoader configLoader;
    std::string errorMessage;

    if (!configLoader.load(rootConfigPath, appConfig_, errorMessage)) {
        std::fprintf(stderr, "Config load failed: %s\n", errorMessage.c_str());
        return false;
    }

    initializePositionStore(appConfig_);
    initializeActorStore(appConfig_);

    if (!startCommunication(appConfig_)) {
        std::fprintf(stderr, "Communication startup failed\n");
        return false;
    }

    if (!registerControlEndpoint()) {
        std::fprintf(stderr, "Failed to register appmain control endpoint\n");
        bus_.stop();
        return false;
    }

    if (!startModules(appConfig_)) {
        std::fprintf(stderr, "Module startup failed\n");
        stopModules();
        bus_.stop();
        return false;
    }

    std::fprintf(stderr, "Application started with %zu module(s). Waiting for stop message to appmain.\n", modules_.size());

    {
        std::unique_lock<std::mutex> lock(stopMutex_);
        stopCv_.wait(lock, [this] { return stopRequested_; });
    }

    std::fprintf(stderr, "Stop message received by appmain. Shutting down.\n");
    stopModules();
    bus_.stop();
    return true;
}

bool App::registerControlEndpoint()
{
    if (!bus_.addMonitor([this](const comm::Frame& frame) { trackFrameForPositionStore(frame); })) {
        return false;
    }

    return bus_.subscribe(control::kAppMainModuleName, [this](const comm::Frame& frame) {
        if (frame.payloadType == control::kAppStopRequestPayloadType) {
            control::AppControlMessage controlMessage;
            if (!control::parsePayloadJson(frame.payloadJson, controlMessage)) {
                std::fprintf(stderr, "appmain received invalid stop payload JSON\n");
                return;
            }

            if (controlMessage.command != control::AppControlCommand::Stop) {
                return;
            }

            std::fprintf(stderr, "appmain accepted stop request from '%s'\n", frame.sourceModule.c_str());
            requestStop();
            return;
        }

        if (frame.payloadType != control::kAppPositionQueryPayloadType) {
            return;
        }

        control::AppControlMessage controlMessage;
        if (!control::parsePayloadJson(frame.payloadJson, controlMessage) ||
            controlMessage.command != control::AppControlCommand::GetPositions) {
            std::fprintf(stderr, "appmain received invalid position query payload JSON\n");
            return;
        }

        if (!replyWithPositions(frame)) {
            std::fprintf(stderr, "appmain failed to send position reply to '%s'\n", frame.sourceModule.c_str());
        }
    });
}

void App::initializePositionStore(const config::AppConfig& appConfig)
{
    std::lock_guard<std::mutex> lock(positionsMutex_);
    axisPositions_ = {};

    std::fprintf(
        stderr,
        "appmain position store initialized (X/Y/Z1..Z4/R1..R4) for project '%s'\n",
        appConfig.projectName.c_str());
}

void App::initializeActorStore(const config::AppConfig& appConfig)
{
    std::lock_guard<std::mutex> lock(actorsMutex_);
    actorValues_.clear();
    for (const auto& actor : appConfig.actors) {
        actorValues_[actor.id] = actor.offValue;
    }
}

void App::trackFrameForPositionStore(const comm::Frame& frame)
{
    if (frame.payloadType == control::kDeviceCommandPayloadType) {
        updateAxisPositionFromCommand(frame);
        updateActorValueFromCommand(frame);
        return;
    }

    if (frame.payloadType == control::kDeviceReplyPayloadType) {
        updateAxisPositionFromReply(frame);
    }
}

const config::ActorConfig* App::findActorByDestinationAndIndex(const std::string& destinationModule, int index) const
{
    for (const auto& actor : appConfig_.actors) {
        if (actor.deviceDriverId == destinationModule && actor.index == index) {
            return &actor;
        }
    }

    return nullptr;
}

void App::updateActorValueFromCommand(const comm::Frame& frame)
{
    nlohmann::json payloadJson;
    try {
        payloadJson = nlohmann::json::parse(frame.payloadJson);
    } catch (...) {
        return;
    }

    if (!payloadJson.contains("action") || !payloadJson["action"].is_string()) {
        return;
    }

    const std::string action = payloadJson["action"].get<std::string>();
    if (action != "operateActorAnalog" && action != "operateActorDigital") {
        return;
    }

    if (!payloadJson.contains("channel") || !payloadJson["channel"].is_number_integer()) {
        return;
    }

    const int channel = payloadJson["channel"].get<int>();
    int actorValue = 0;

    if (action == "operateActorAnalog") {
        if (!payloadJson.contains("value") || !payloadJson["value"].is_number()) {
            return;
        }
        actorValue = static_cast<int>(payloadJson["value"].get<float>());
    } else {
        if (!payloadJson.contains("state") || !payloadJson["state"].is_boolean()) {
            return;
        }
        actorValue = payloadJson["state"].get<bool>() ? 1 : 0;
    }

    const config::ActorConfig* actorConfig = findActorByDestinationAndIndex(frame.destinationModule, channel);
    if (actorConfig == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(actorsMutex_);
    actorValues_[actorConfig->id] = actorValue;
}

bool App::extractMarlinValue(const std::string& line, const char* label, float& outValue) const
{
    const std::string needle = std::string(label) + ":";
    const std::size_t position = line.find(needle);
    if (position == std::string::npos) {
        return false;
    }

    const char* valueStart = line.c_str() + position + needle.size();
    char* valueEnd = nullptr;
    const float value = std::strtof(valueStart, &valueEnd);
    if (valueEnd == valueStart) {
        return false;
    }

    outValue = value;
    return true;
}

void App::applyMarlinHomingReport(const std::string& boardId, const std::string& reportLine)
{
    float xValue = 0.0f;
    float yValue = 0.0f;
    float aValue = 0.0f;
    float bValue = 0.0f;

    if (!extractMarlinValue(reportLine, "X", xValue) ||
        !extractMarlinValue(reportLine, "Y", yValue)) {
        return;
    }

    const bool hasA = extractMarlinValue(reportLine, "A", aValue);
    const bool hasB = extractMarlinValue(reportLine, "B", bValue);

    std::lock_guard<std::mutex> lock(positionsMutex_);

    if (boardId == "moveXY") {
        axisPositions_.x = xValue;
        axisPositions_.y = yValue;
        return;
    }

    if (boardId == "moveZ1" || boardId == "moveZ2") {
        axisPositions_.z1 = xValue;
        axisPositions_.z2 = yValue;
        if (hasA) {
            axisPositions_.r1 = aValue;
        }
        if (hasB) {
            axisPositions_.r2 = bValue;
        }
        return;
    }

    if (boardId == "moveZ3" || boardId == "moveZ4") {
        axisPositions_.z3 = xValue;
        axisPositions_.z4 = yValue;
        if (hasA) {
            axisPositions_.r3 = aValue;
        }
        if (hasB) {
            axisPositions_.r4 = bValue;
        }
    }
}

void App::updateAxisPositionFromReply(const comm::Frame& frame)
{
    nlohmann::json replyJson;
    try {
        replyJson = nlohmann::json::parse(frame.payloadJson);
    } catch (...) {
        return;
    }

    if (!replyJson.contains("action") || !replyJson["action"].is_string()) {
        return;
    }

    if (!replyJson.contains("result") || !replyJson["result"].is_object()) {
        return;
    }

    const std::string action = replyJson["action"].get<std::string>();
    if (action != "homeXYAB" && action != "homeZ") {
        return;
    }

    const nlohmann::json& resultJson = replyJson["result"];
    if (!resultJson.contains("deviceResponse") || !resultJson["deviceResponse"].is_object()) {
        return;
    }

    const nlohmann::json& deviceResponse = resultJson["deviceResponse"];
    std::vector<std::string> lines;
    if (!readStringArrayField(deviceResponse, "lines", lines)) {
        return;
    }

    std::string reportLine;
    for (const auto& line : lines) {
        if (line.find("X:") != std::string::npos && line.find("Y:") != std::string::npos) {
            reportLine = line;
        }
    }

    if (reportLine.empty()) {
        return;
    }

    applyMarlinHomingReport(frame.sourceModule, reportLine);
}

void App::updateAxisPositionFromCommand(const comm::Frame& frame)
{
    nlohmann::json payloadJson;
    try {
        payloadJson = nlohmann::json::parse(frame.payloadJson);
    } catch (...) {
        return;
    }

    if (!payloadJson.contains("action") || !payloadJson["action"].is_string()) {
        return;
    }

    const std::string action = payloadJson["action"].get<std::string>();

    std::lock_guard<std::mutex> lock(positionsMutex_);
    if (action == "moveXY") {
        float value = 0.0f;
        if (parseFloatField(payloadJson, "xPos", value)) {
            axisPositions_.x = value;
        }
        if (parseFloatField(payloadJson, "yPos", value)) {
            axisPositions_.y = value;
        }
        return;
    }

    if (action == "moveZ") {
        float zPos = 0.0f;
        if (!parseFloatField(payloadJson, "zPos", zPos)) {
            return;
        }

        if (frame.destinationModule == "moveZ1") {
            axisPositions_.z1 = zPos;
        } else if (frame.destinationModule == "moveZ2") {
            axisPositions_.z2 = zPos;
        } else if (frame.destinationModule == "moveZ3") {
            axisPositions_.z3 = zPos;
        } else if (frame.destinationModule == "moveZ4") {
            axisPositions_.z4 = zPos;
        }
        return;
    }

    if (action == "rotate") {
        float angle = 0.0f;
        const bool hasAngle = parseFloatField(payloadJson, "angle", angle);
        const bool hasRelative = parseFloatField(payloadJson, "relativeRotation", angle);
        if (!hasAngle && !hasRelative) {
            return;
        }

        auto applyRotation = [&](float& axisValue) {
            axisValue = hasAngle ? angle : (axisValue + angle);
        };

        if (frame.destinationModule == "rotR1") {
            applyRotation(axisPositions_.r1);
        } else if (frame.destinationModule == "rotR2") {
            applyRotation(axisPositions_.r2);
        } else if (frame.destinationModule == "rotR3") {
            applyRotation(axisPositions_.r3);
        } else if (frame.destinationModule == "rotR4") {
            applyRotation(axisPositions_.r4);
        }
        return;
    }

    if (action == "homeXYAB") {
        axisPositions_.x = 0.0f;
        axisPositions_.y = 0.0f;
        return;
    }

    if (action == "homeZ") {
        if (frame.destinationModule == "moveZ1") {
            axisPositions_.z1 = 0.0f;
        } else if (frame.destinationModule == "moveZ2") {
            axisPositions_.z2 = 0.0f;
        } else if (frame.destinationModule == "moveZ3") {
            axisPositions_.z3 = 0.0f;
        } else if (frame.destinationModule == "moveZ4") {
            axisPositions_.z4 = 0.0f;
        }
    }
}

bool App::replyWithPositions(const comm::Frame& queryFrame)
{
    control::AppPositionReplyMessage replyMessage;
    {
        std::lock_guard<std::mutex> lock(positionsMutex_);
        replyMessage.axis.x = axisPositions_.x;
        replyMessage.axis.y = axisPositions_.y;
        replyMessage.axis.z1 = axisPositions_.z1;
        replyMessage.axis.z2 = axisPositions_.z2;
        replyMessage.axis.z3 = axisPositions_.z3;
        replyMessage.axis.z4 = axisPositions_.z4;
        replyMessage.axis.r1 = axisPositions_.r1;
        replyMessage.axis.r2 = axisPositions_.r2;
        replyMessage.axis.r3 = axisPositions_.r3;
        replyMessage.axis.r4 = axisPositions_.r4;
    }

    replyMessage.reference.posCalib = {
        appConfig_.referencePositions.posCalib.x,
        appConfig_.referencePositions.posCalib.y,
        appConfig_.referencePositions.posCalib.z};
    replyMessage.reference.posCalibSec = {
        appConfig_.referencePositions.posCalibSec.x,
        appConfig_.referencePositions.posCalibSec.y,
        appConfig_.referencePositions.posCalibSec.z};
    replyMessage.reference.posPark = {
        appConfig_.referencePositions.posPark.x,
        appConfig_.referencePositions.posPark.y,
        appConfig_.referencePositions.posPark.z};
    replyMessage.reference.posCamBot = {
        appConfig_.referencePositions.posCamBot.x,
        appConfig_.referencePositions.posCamBot.y,
        appConfig_.referencePositions.posCamBot.z};
    replyMessage.reference.posDiscard = {
        appConfig_.referencePositions.posDiscard.x,
        appConfig_.referencePositions.posDiscard.y,
        appConfig_.referencePositions.posDiscard.z};
    replyMessage.reference.posChange = {
        appConfig_.referencePositions.posChange.x,
        appConfig_.referencePositions.posChange.y,
        appConfig_.referencePositions.posChange.z};

    {
        std::lock_guard<std::mutex> lock(actorsMutex_);
        for (const auto& actor : actorValues_) {
            replyMessage.actors.push_back(actor);
        }
    }

    comm::Frame replyFrame;
    replyFrame.timestampEpochMs = nowEpochMs();
    replyFrame.destinationModule = queryFrame.sourceModule;
    replyFrame.sourceModule = control::kAppMainModuleName;
    replyFrame.payloadType = control::kAppPositionReplyPayloadType;
    replyFrame.payloadJson = control::serializePositionReplyPayloadJson(replyMessage);

    if (!queryFrame.senderIp.empty() && queryFrame.senderPort != 0) {
        const int socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socketFd < 0) {
            return false;
        }

        sockaddr_in targetAddress;
        std::memset(&targetAddress, 0, sizeof(targetAddress));
        targetAddress.sin_family = AF_INET;
        targetAddress.sin_port = htons(queryFrame.senderPort);

        if (::inet_pton(AF_INET, queryFrame.senderIp.c_str(), &targetAddress.sin_addr) != 1) {
            ::close(socketFd);
            return false;
        }

        nlohmann::json frameJson;
        frameJson["timestampEpochMs"] = replyFrame.timestampEpochMs;
        frameJson["destinationModule"] = replyFrame.destinationModule;
        frameJson["sourceModule"] = replyFrame.sourceModule;
        frameJson["payloadType"] = replyFrame.payloadType;
        frameJson["payloadJson"] = replyFrame.payloadJson;

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

    return bus_.publish(replyFrame);
}

void App::requestStop()
{
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
        stopRequested_ = true;
    }
    stopCv_.notify_all();
}

bool App::startCommunication(const config::AppConfig& appConfig)
{
    if (!bus_.start(appConfig.communication.listenIp, appConfig.communication.listenPort)) {
        return false;
    }

    for (const auto& route : appConfig.communication.routes) {
        bus_.addRoute(route.first, route.second);
    }

    std::fprintf(
        stderr,
        "Communication started on %s:%u\n",
        appConfig.communication.listenIp.c_str(),
        appConfig.communication.listenPort);

    return true;
}

bool App::startModules(const config::AppConfig& appConfig)
{
    modules::ModuleContext context{bus_};

    for (const auto& moduleConfig : appConfig.modules) {
        if (!moduleConfig.enabled) {
            continue;
        }

        std::unique_ptr<modules::IModule> module = createModule(moduleConfig, appConfig);
        if (!module) {
            std::fprintf(stderr, "Unknown module type '%s' for id '%s'\n", moduleConfig.type.c_str(), moduleConfig.id.c_str());
            return false;
        }

        if (!module->start(context)) {
            std::fprintf(stderr, "Failed to start module '%s'\n", moduleConfig.id.c_str());
            return false;
        }

        modules_.push_back(std::move(module));
    }

    return true;
}

void App::stopModules()
{
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
        (*it)->stop();
    }

    modules_.clear();
}

std::unique_ptr<modules::IModule> App::createModule(const config::ModuleConfig& moduleConfig, const config::AppConfig& appConfig)
{
    if (moduleConfig.type == "communication-monitor") {
        return std::make_unique<modules::CommunicationMonitorModule>(moduleConfig.id);
    }

    if (moduleConfig.type == "device-driver-runtime") {
        return std::make_unique<modules::DeviceDriverRuntimeModule>(moduleConfig.id, appConfig);
    }

    if (moduleConfig.type == "placer") {
        return std::make_unique<modules::PlacerModule>(moduleConfig.id);
    }

    return nullptr;
}

} // namespace opensmt
