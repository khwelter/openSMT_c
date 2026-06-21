#include "openSMT/hw/MarlinSerialProtocolAdapter.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace opensmt {
namespace hw {

namespace {

int toFeedRate(float speed)
{
    const int value = static_cast<int>(std::lround(speed));
    return value > 0 ? value : 1;
}

std::string toFloatText(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

bool readFloat(const nlohmann::json& payload, const char* key, float& outValue)
{
    if (!payload.contains(key) || !payload[key].is_number()) {
        return false;
    }

    outValue = payload[key].get<float>();
    return true;
}

bool readInt(const nlohmann::json& payload, const char* key, int& outValue)
{
    if (!payload.contains(key) || !payload[key].is_number()) {
        return false;
    }

    outValue = static_cast<int>(std::lround(payload[key].get<float>()));
    return true;
}

bool axisLetterForZDriver(const std::string& deviceDriverId, char& outAxisLetter)
{
    if (deviceDriverId == "moveZ1" || deviceDriverId == "moveZ3") {
        outAxisLetter = 'X';
        return true;
    }

    if (deviceDriverId == "moveZ2" || deviceDriverId == "moveZ4") {
        outAxisLetter = 'Y';
        return true;
    }

    return false;
}

bool axisLetterForRotationDriver(const std::string& deviceDriverId, char& outAxisLetter)
{
    if (deviceDriverId == "rotR1" || deviceDriverId == "rotR3") {
        outAxisLetter = 'A';
        return true;
    }

    if (deviceDriverId == "rotR2" || deviceDriverId == "rotR4") {
        outAxisLetter = 'B';
        return true;
    }

    return false;
}

bool disableAllCommandForDriver(const std::string& deviceDriverId, std::string& outCommand)
{
    if (deviceDriverId == "moveXY") {
        outCommand = "M18 X Y";
        return true;
    }

    if (deviceDriverId == "moveZ1" || deviceDriverId == "moveZ2" ||
        deviceDriverId == "rotR1" || deviceDriverId == "rotR2") {
        outCommand = "M18 X Y A B";
        return true;
    }

    if (deviceDriverId == "moveZ3" || deviceDriverId == "moveZ4" ||
        deviceDriverId == "rotR3" || deviceDriverId == "rotR4") {
        outCommand = "M18 X Y A B";
        return true;
    }

    return false;
}

} // namespace

MarlinSerialProtocolAdapter::MarlinSerialProtocolAdapter(int defaultRotateSpeed)
    : defaultRotateSpeed_(defaultRotateSpeed > 0 ? defaultRotateSpeed : 1200)
{
}

const std::string& MarlinSerialProtocolAdapter::id() const
{
    static const std::string kId = "marlin-gcode";
    return kId;
}

SerialCommandBuildResult MarlinSerialProtocolAdapter::buildCommand(
    const std::string& deviceDriverId,
    const std::string& action,
    const std::string& payloadJson) const
{
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadJson);
    } catch (...) {
        return {false, "", "invalid JSON payload"};
    }

    if (action == "homeXYAB") {
        return {true, "G28 X Y A B", ""};
    }

    if (action == "homeZ") {
        char axisLetter = 0;
        if (!axisLetterForZDriver(deviceDriverId, axisLetter)) {
            return {false, "", "homeZ requires moveZ1..moveZ4 destination"};
        }

        std::string command = "G28 ";
        command.push_back(axisLetter);
        return {true, command, ""};
    }

    if (action == "moveXY") {
        const bool hasX = payload.contains("xPos") && payload["xPos"].is_number();
        const bool hasY = payload.contains("yPos") && payload["yPos"].is_number();
        float xPos = 0.0f;
        float yPos = 0.0f;
        float speed = 0.0f;
        if ((!hasX && !hasY) || !readFloat(payload, "speed", speed)) {
            return {false, "", "moveXY requires speed and at least one of xPos/yPos"};
        }

        if (hasX && !readFloat(payload, "xPos", xPos)) {
            return {false, "", "moveXY has invalid xPos"};
        }

        if (hasY && !readFloat(payload, "yPos", yPos)) {
            return {false, "", "moveXY has invalid yPos"};
        }

        std::ostringstream command;
        command << "G0";
        if (hasX) {
            command << " X" << toFloatText(xPos);
        }
        if (hasY) {
            command << " Y" << toFloatText(yPos);
        }
        command << " F" << toFeedRate(speed);
        return {true, command.str(), ""};
    }

    if (action == "rotate") {
        float angle = 0.0f;
        if (payload.contains("angle") && payload["angle"].is_number()) {
            angle = payload["angle"].get<float>();
        } else if (!readFloat(payload, "relativeRotation", angle)) {
            return {false, "", "rotate requires angle or relativeRotation"};
        }

        char axisLetter = 0;
        if (!axisLetterForRotationDriver(deviceDriverId, axisLetter)) {
            return {false, "", "rotate requires rotR1..rotR4 destination"};
        }

        int feedRate = defaultRotateSpeed_;
        if (payload.contains("speed") && payload["speed"].is_number()) {
            feedRate = toFeedRate(payload["speed"].get<float>());
        }

        std::ostringstream command;
        command << "G0 " << axisLetter << toFloatText(angle)
                << " F" << feedRate;
        return {true, command.str(), ""};
    }

    if (action == "moveZ") {
        float zPos = 0.0f;
        float speed = 0.0f;
        if (!readFloat(payload, "zPos", zPos) || !readFloat(payload, "speed", speed)) {
            return {false, "", "moveZ requires zPos, speed"};
        }

        char axisLetter = 0;
        if (!axisLetterForZDriver(deviceDriverId, axisLetter)) {
            return {false, "", "moveZ requires moveZ1..moveZ4 destination"};
        }

        std::ostringstream command;
        command << "G0 " << axisLetter << toFloatText(zPos)
                << " F" << toFeedRate(speed);
        return {true, command.str(), ""};
    }

    if (action == "disableAllSteppers") {
        std::string command;
        if (!disableAllCommandForDriver(deviceDriverId, command)) {
            return {false, "", "disableAllSteppers requires XY/Z/R motion driver destination"};
        }

        return {true, command, ""};
    }

    if (action == "operateActorDigital") {
        int channel = 0;
        if (!readInt(payload, "channel", channel) ||
            !payload.contains("state") || !payload["state"].is_boolean()) {
            return {false, "", "operateActorDigital requires channel, state"};
        }

        const int value = payload["state"].get<bool>() ? 1 : 0;
        std::ostringstream command;
        command << "M106 P" << channel << " S" << value;
        return {true, command.str(), ""};
    }

    if (action == "operateActorAnalog") {
        int channel = 0;
        int value = 0;
        if (!readInt(payload, "channel", channel) || !readInt(payload, "value", value)) {
            return {false, "", "operateActorAnalog requires channel, value"};
        }

        if (channel < 0 || channel > 255 || value < 0 || value > 255) {
            return {false, "", "operateActorAnalog channel/value must be in range 0..255"};
        }

        std::ostringstream command;
        command << "M106 P" << channel << " S" << value;
        return {true, command.str(), ""};
    }

    return {false, "", "action not supported by Marlin adapter: " + action};
}

} // namespace hw
} // namespace opensmt
