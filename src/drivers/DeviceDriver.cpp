#include "openSMT/drivers/DeviceDriver.hpp"

#include <chrono>
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

DeviceDriver::DeviceDriver(std::string moduleId, std::shared_ptr<hw::IHardwareDriver> hardwareDriver)
    : moduleId_(std::move(moduleId)),
      hardwareDriver_(std::move(hardwareDriver)),
      bus_(nullptr),
      started_(false)
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
        const bool hasX = commandJson.contains("xPos") && commandJson["xPos"].is_number();
        const bool hasY = commandJson.contains("yPos") && commandJson["yPos"].is_number();
        float speed = 0.0f;
        if ((!hasX && !hasY) || !readRequiredFloat(commandJson, "speed", speed)) {
            error = "moveXY requires speed and at least one of xPos/yPos";
        } else {
            if (hasX && hasY) {
                float xPos = 0.0f;
                float yPos = 0.0f;
                if (!readRequiredFloat(commandJson, "xPos", xPos) || !readRequiredFloat(commandJson, "yPos", yPos)) {
                    error = "moveXY has invalid xPos/yPos";
                    sendReply(*bus_, frame, action, false, resultJson.dump(), error, replyMode);
                    return;
                }
                ok = moveXY(xPos, yPos, speed);
            } else {
                const hw::HardwareCommand directCommand{moduleId_, "moveXY", commandJson.dump()};
                const hw::HardwareResponse response = hardware().execute(directCommand);
                ok = response.ok;
                if (response.ok) {
                    try {
                        resultJson = nlohmann::json::parse(response.payloadJson);
                    } catch (...) {
                        resultJson["rawResponse"] = response.payloadJson;
                    }
                } else {
                    error = response.error;
                }
            }

            if (!ok) {
                if (error.empty()) {
                    error = "moveXY rejected by driver";
                }
            }
        }
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
