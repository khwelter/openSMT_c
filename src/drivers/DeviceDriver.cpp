#include "openSMT/drivers/DeviceDriver.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "openSMT/control/DeviceDriverMessage.hpp"

namespace opensmt {
namespace drivers {

namespace {

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

bool readRequiredFloat(const nlohmann::json& json, const char* key, float& outValue)
{
    if (!json.contains(key) || !json[key].is_number()) {
        return false;
    }
    outValue = json[key].get<float>();
    return true;
}

bool readRequiredInt(const nlohmann::json& json, const char* key, int& outValue)
{
    if (!json.contains(key) || !json[key].is_number_integer()) {
        return false;
    }
    outValue = json[key].get<int>();
    return true;
}

} // namespace

DeviceDriver::DeviceDriver(
        std::string moduleId,
        std::shared_ptr<hw::IHardwareDriver> hardwareDriver,
        float moveXYSlackThresholdMm,
        float moveXYSlackCompensationMm)
    : moduleId_(std::move(moduleId)),
      hardwareDriver_(std::move(hardwareDriver)),
      bus_(nullptr),
    started_(false),
    hasTrackedXY_(false),
    trackedX_(0.0f),
            trackedY_(0.0f),
            moveXYSlackThresholdMm_(moveXYSlackThresholdMm),
            moveXYSlackCompensationMm_(moveXYSlackCompensationMm)
{
}

const std::string& DeviceDriver::id() const
{
    return moduleId_;
}

bool DeviceDriver::start(comm::MessageBus& bus)
{
    if (started_) {
        return true;
    }

    if (!hardwareDriver_) {
        std::fprintf(stderr, "[driver][%s] no hardware assigned\n", moduleId_.c_str());
        return false;
    }

    bus_ = &bus;
    bus_->subscribe(moduleId_, [this](const comm::Frame& frame) {
        onFrame(frame);
    });

    started_ = true;
    std::fprintf(stderr, "[driver][%s] started on hardware=%s\n", moduleId_.c_str(), hardwareDriver_->id().c_str());
    return true;
}

void DeviceDriver::stop()
{
    if (!started_) {
        return;
    }

    started_ = false;
    bus_ = nullptr;
    std::fprintf(stderr, "[driver][%s] stopped\n", moduleId_.c_str());
}

hw::IHardwareDriver& DeviceDriver::hardware()
{
    return *hardwareDriver_;
}

bool DeviceDriver::tryExtractAxisValue(const std::string& line, const char* axisLabel, float& outValue) const
{
    const std::string key = std::string(axisLabel) + ":";
    const std::size_t pos = line.find(key);
    if (pos == std::string::npos) {
        return false;
    }

    const char* begin = line.c_str() + pos + key.size();
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end == begin) {
        return false;
    }

    outValue = value;
    return true;
}

void DeviceDriver::updateTrackedXYFromHomeResponse(const std::string& hardwarePayloadJson)
{
    nlohmann::json resultJson;
    try {
        resultJson = nlohmann::json::parse(hardwarePayloadJson);
    } catch (...) {
        return;
    }

    if (!resultJson.contains("deviceResponse") || !resultJson["deviceResponse"].is_object()) {
        return;
    }

    const auto& deviceResponse = resultJson["deviceResponse"];
    if (!deviceResponse.contains("lines") || !deviceResponse["lines"].is_array()) {
        return;
    }

    for (const auto& lineJson : deviceResponse["lines"]) {
        if (!lineJson.is_string()) {
            continue;
        }

        const std::string line = lineJson.get<std::string>();
        float x = 0.0f;
        float y = 0.0f;
        if (!tryExtractAxisValue(line, "X", x) || !tryExtractAxisValue(line, "Y", y)) {
            continue;
        }

        trackedX_ = x;
        trackedY_ = y;
        hasTrackedXY_ = true;
        return;
    }
}

bool DeviceDriver::executeMoveXYCommand(const nlohmann::json& commandJson, nlohmann::json& outResultJson, std::string& outError)
{
    const bool hasX = commandJson.contains("xPos") && commandJson["xPos"].is_number();
    const bool hasY = commandJson.contains("yPos") && commandJson["yPos"].is_number();

    float speed = 0.0f;
    if ((!hasX && !hasY) || !readRequiredFloat(commandJson, "speed", speed)) {
        outError = "moveXY requires speed and at least one of xPos/yPos";
        return false;
    }

    float targetX = 0.0f;
    float targetY = 0.0f;
    if (hasX && !readRequiredFloat(commandJson, "xPos", targetX)) {
        outError = "moveXY has invalid xPos";
        return false;
    }

    if (hasY && !readRequiredFloat(commandJson, "yPos", targetY)) {
        outError = "moveXY has invalid yPos";
        return false;
    }

    auto executeDirect = [&](bool sendX, float xPos, bool sendY, float yPos, nlohmann::json* outJson) {
        nlohmann::json payload;
        if (sendX) {
            payload["xPos"] = xPos;
        }
        if (sendY) {
            payload["yPos"] = yPos;
        }
        payload["speed"] = speed;

        const hw::HardwareCommand directCommand{moduleId_, "moveXY", payload.dump()};
        const hw::HardwareResponse response = hardware().execute(directCommand);
        if (!response.ok) {
            outError = response.error.empty() ? "moveXY rejected by driver" : response.error;
            return false;
        }

        if (outJson != nullptr) {
            try {
                *outJson = nlohmann::json::parse(response.payloadJson);
            } catch (...) {
                (*outJson)["rawResponse"] = response.payloadJson;
            }
        }

        return true;
    };

    if (hasTrackedXY_) {
        const bool canCompensate = moveXYSlackThresholdMm_ > 0.0f && moveXYSlackCompensationMm_ > 0.0f;
        const bool compensateX =
            canCompensate && hasX && targetX < trackedX_ && std::fabs(targetX - trackedX_) > moveXYSlackThresholdMm_;
        const bool compensateY =
            canCompensate && hasY && targetY < trackedY_ && std::fabs(targetY - trackedY_) > moveXYSlackThresholdMm_;

        if (compensateX || compensateY) {
            const float preX = compensateX ? (targetX - moveXYSlackCompensationMm_) : targetX;
            const float preY = compensateY ? (targetY - moveXYSlackCompensationMm_) : targetY;

            if (!executeDirect(hasX, preX, hasY, preY, nullptr)) {
                return false;
            }
        }
    }

    if (!executeDirect(hasX, targetX, hasY, targetY, &outResultJson)) {
        return false;
    }

    if (hasTrackedXY_) {
        if (hasX) {
            trackedX_ = targetX;
        }
        if (hasY) {
            trackedY_ = targetY;
        }
    } else if (hasX && hasY) {
        trackedX_ = targetX;
        trackedY_ = targetY;
        hasTrackedXY_ = true;
    }

    return true;
}

void DeviceDriver::onFrame(const comm::Frame& frame)
{
    if (!started_ || !bus_) {
        return;
    }

    if (frame.destinationModule != moduleId_) {
        return;
    }

    if (frame.payloadType != control::kDeviceCommandPayloadType) {
        return;
    }

    nlohmann::json commandJson;
    try {
        commandJson = nlohmann::json::parse(frame.payloadJson);
    } catch (...) {
        sendReply(*bus_, frame, "unknown", false, "{}", "invalid payload JSON", control::kReplyModeSource);
        return;
    }

    if (!commandJson.contains("action") || !commandJson["action"].is_string()) {
        sendReply(*bus_, frame, "unknown", false, "{}", "missing action", control::kReplyModeSource);
        return;
    }

    const std::string action = commandJson["action"].get<std::string>();
    std::string replyMode = control::kReplyModeSource;
    if (commandJson.contains("replyMode") && commandJson["replyMode"].is_string()) {
        replyMode = commandJson["replyMode"].get<std::string>();
    }

    bool ok = false;
    std::string error;
    nlohmann::json resultJson;

    if (action == "moveXY") {
        ok = executeMoveXYCommand(commandJson, resultJson, error);
    } else if (action == "homeXYAB") {
        const hw::HardwareCommand homeCommand{moduleId_, "homeXYAB", "{}"};
        const hw::HardwareResponse response = hardware().execute(homeCommand);
        ok = response.ok;
        if (response.ok) {
            try {
                resultJson = nlohmann::json::parse(response.payloadJson);
            } catch (...) {
                resultJson["rawResponse"] = response.payloadJson;
            }
            updateTrackedXYFromHomeResponse(response.payloadJson);
        } else {
            error = response.error.empty() ? "homeXYAB rejected by driver" : response.error;
        }
    } else if (action == "homeZ") {
        const hw::HardwareCommand homeCommand{moduleId_, "homeZ", "{}"};
        const hw::HardwareResponse response = hardware().execute(homeCommand);
        ok = response.ok;
        if (response.ok) {
            try {
                resultJson = nlohmann::json::parse(response.payloadJson);
            } catch (...) {
                resultJson["rawResponse"] = response.payloadJson;
            }
        } else {
            error = response.error.empty() ? "homeZ rejected by driver" : response.error;
        }
    } else if (action == "moveZ") {
        float zPos = 0.0f;
        float speed = 0.0f;
        if (!readRequiredFloat(commandJson, "zPos", zPos) || !readRequiredFloat(commandJson, "speed", speed)) {
            error = "moveZ requires zPos, speed";
        } else {
            ok = moveZ(zPos, speed);
            if (!ok) {
                error = "moveZ rejected by driver";
            }
        }
    } else if (action == "rotate") {
        float relativeRotation = 0.0f;
        float speed = 0.0f;
        if (commandJson.contains("angle") && commandJson["angle"].is_number()) {
            relativeRotation = commandJson["angle"].get<float>();
        } else if (!readRequiredFloat(commandJson, "relativeRotation", relativeRotation)) {
            error = "rotate requires angle or relativeRotation";
        }

        if (error.empty() && !readRequiredFloat(commandJson, "speed", speed)) {
            error = "rotate requires speed";
        }

        if (error.empty()) {
            ok = rotate(relativeRotation, speed);
            if (!ok) {
                error = "rotate rejected by driver";
            }
        }
    } else if (action == "disableAllSteppers") {
        ok = disableAllSteppers();
        if (!ok) {
            error = "disableAllSteppers rejected by driver";
        }
    } else if (action == "operateActorDigital") {
        int channel = 0;
        if (!readRequiredInt(commandJson, "channel", channel) ||
            !commandJson.contains("state") ||
            !commandJson["state"].is_boolean()) {
            error = "operateActorDigital requires channel, state";
        } else {
            const bool state = commandJson["state"].get<bool>();
            ok = operateActor(channel, state);
            if (!ok) {
                error = "operateActorDigital rejected by driver";
            }
        }
    } else if (action == "operateActorAnalog") {
        int channel = 0;
        float value = 0.0f;
        if (!readRequiredInt(commandJson, "channel", channel) || !readRequiredFloat(commandJson, "value", value)) {
            error = "operateActorAnalog requires channel, value";
        } else {
            ok = operateActor(channel, value);
            if (!ok) {
                error = "operateActorAnalog rejected by driver";
            }
        }
    } else if (action == "readSensorDigital") {
        int channel = 0;
        bool sensorValue = false;
        if (!readRequiredInt(commandJson, "channel", channel)) {
            error = "readSensorDigital requires channel";
        } else {
            ok = readSensorDigital(channel, sensorValue);
            if (ok) {
                resultJson["value"] = sensorValue;
            } else {
                error = "readSensorDigital failed";
            }
        }
    } else if (action == "readSensorAnalog") {
        int channel = 0;
        float sensorValue = 0.0f;
        if (!readRequiredInt(commandJson, "channel", channel)) {
            error = "readSensorAnalog requires channel";
        } else {
            ok = readSensorAnalog(channel, sensorValue);
            if (ok) {
                resultJson["value"] = sensorValue;
            } else {
                error = "readSensorAnalog failed";
            }
        }
    } else if (action == "readVersionNumber") {
        const std::string version = readVersionNumber();
        ok = !version.empty();
        if (ok) {
            resultJson["value"] = version;
        } else {
            error = "readVersionNumber failed";
        }
    } else {
        error = "unknown action: " + action;
    }

    sendReply(*bus_, frame, action, ok, resultJson.dump(), error, replyMode);
}

bool DeviceDriver::buildReplyDestination(const comm::Frame& requestFrame, const std::string& replyMode, std::string& outDestination) const
{
    if (replyMode == control::kReplyModeBroadcast) {
        outDestination = comm::kBroadcastDestination;
        return true;
    }

    if (replyMode == control::kReplyModeSource) {
        outDestination = requestFrame.sourceModule;
        return true;
    }

    return false;
}

void DeviceDriver::sendReply(
    comm::MessageBus& bus,
    const comm::Frame& requestFrame,
    const std::string& action,
    bool ok,
    const std::string& resultJson,
    const std::string& error,
    const std::string& replyMode)
{
    std::string destinationModule;
    if (!buildReplyDestination(requestFrame, replyMode, destinationModule)) {
        destinationModule = requestFrame.sourceModule;
    }

    nlohmann::json payloadJson;
    payloadJson["action"] = action;
    payloadJson["ok"] = ok;
    payloadJson["result"] = nlohmann::json::parse(resultJson);
    if (!error.empty()) {
        payloadJson["error"] = error;
    }

    comm::Frame replyFrame;
    replyFrame.timestampEpochMs = nowEpochMs();
    replyFrame.destinationModule = destinationModule;
    replyFrame.sourceModule = moduleId_;
    replyFrame.payloadType = control::kDeviceReplyPayloadType;
    replyFrame.payloadJson = payloadJson.dump();

    bus.publish(replyFrame);
}

} // namespace drivers
} // namespace opensmt
