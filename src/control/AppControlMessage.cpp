#include "openSMT/control/AppControlMessage.hpp"

#include <nlohmann/json.hpp>

namespace opensmt {
namespace control {

std::string toCommandString(AppControlCommand command)
{
    if (command == AppControlCommand::Stop) {
        return "stop";
    }

    if (command == AppControlCommand::GetPositions) {
        return "get-positions";
    }

    return "unknown";
}

bool commandFromString(const std::string& value, AppControlCommand& outCommand)
{
    if (value == "stop") {
        outCommand = AppControlCommand::Stop;
        return true;
    }

    if (value == "get-positions") {
        outCommand = AppControlCommand::GetPositions;
        return true;
    }

    return false;
}

std::string serializePayloadJson(const AppControlMessage& message)
{
    nlohmann::json payloadJson;
    payloadJson["command"] = toCommandString(message.command);
    payloadJson["reason"] = message.reason;
    return payloadJson.dump();
}

bool parsePayloadJson(const std::string& payloadJsonText, AppControlMessage& outMessage)
{
    nlohmann::json payloadJson;
    try {
        payloadJson = nlohmann::json::parse(payloadJsonText);
    } catch (...) {
        return false;
    }

    if (!payloadJson.contains("command") || !payloadJson["command"].is_string()) {
        return false;
    }

    AppControlCommand command;
    if (!commandFromString(payloadJson["command"].get<std::string>(), command)) {
        return false;
    }

    std::string reason;
    if (payloadJson.contains("reason") && payloadJson["reason"].is_string()) {
        reason = payloadJson["reason"].get<std::string>();
    }

    outMessage.command = command;
    outMessage.reason = reason;
    return true;
}

std::string serializePositionReplyPayloadJson(const AppPositionReplyMessage& message)
{
    nlohmann::json payloadJson;

    payloadJson["axis"] = {
        {"x", message.axis.x},
        {"y", message.axis.y},
        {"z1", message.axis.z1},
        {"z2", message.axis.z2},
        {"z3", message.axis.z3},
        {"z4", message.axis.z4},
        {"r1", message.axis.r1},
        {"r2", message.axis.r2},
        {"r3", message.axis.r3},
        {"r4", message.axis.r4}};

    payloadJson["reference"] = {
           {"posCalib", {
               {"x", message.reference.posCalib.x},
               {"y", message.reference.posCalib.y},
               {"z", message.reference.posCalib.z}}},
           {"posCalibSec", {
               {"x", message.reference.posCalibSec.x},
               {"y", message.reference.posCalibSec.y},
               {"z", message.reference.posCalibSec.z}}},
        {"posPark", {
             {"x", message.reference.posPark.x},
             {"y", message.reference.posPark.y},
             {"z", message.reference.posPark.z}}},
           {"posCamBot", {
               {"x", message.reference.posCamBot.x},
               {"y", message.reference.posCamBot.y},
               {"z", message.reference.posCamBot.z}}},
        {"posDiscard", {
             {"x", message.reference.posDiscard.x},
             {"y", message.reference.posDiscard.y},
             {"z", message.reference.posDiscard.z}}},
        {"posChange", {
             {"x", message.reference.posChange.x},
             {"y", message.reference.posChange.y},
             {"z", message.reference.posChange.z}}}};

    payloadJson["actors"] = nlohmann::json::array();
    for (const auto& actor : message.actors) {
        payloadJson["actors"].push_back({{"id", actor.first}, {"value", actor.second}});
    }

    return payloadJson.dump();
}

bool parsePositionReplyPayloadJson(const std::string& payloadJsonText, AppPositionReplyMessage& outMessage)
{
    nlohmann::json payloadJson;
    try {
        payloadJson = nlohmann::json::parse(payloadJsonText);
    } catch (...) {
        return false;
    }

    if (!payloadJson.contains("axis") || !payloadJson["axis"].is_object() ||
        !payloadJson.contains("reference") || !payloadJson["reference"].is_object()) {
        return false;
    }

    const auto& axis = payloadJson["axis"];
    if (!axis.contains("x") || !axis["x"].is_number() ||
        !axis.contains("y") || !axis["y"].is_number() ||
        !axis.contains("z1") || !axis["z1"].is_number() ||
        !axis.contains("z2") || !axis["z2"].is_number() ||
        !axis.contains("z3") || !axis["z3"].is_number() ||
        !axis.contains("z4") || !axis["z4"].is_number() ||
        !axis.contains("r1") || !axis["r1"].is_number() ||
        !axis.contains("r2") || !axis["r2"].is_number() ||
        !axis.contains("r3") || !axis["r3"].is_number() ||
        !axis.contains("r4") || !axis["r4"].is_number()) {
        return false;
    }

    outMessage.axis.x = axis["x"].get<float>();
    outMessage.axis.y = axis["y"].get<float>();
    outMessage.axis.z1 = axis["z1"].get<float>();
    outMessage.axis.z2 = axis["z2"].get<float>();
    outMessage.axis.z3 = axis["z3"].get<float>();
    outMessage.axis.z4 = axis["z4"].get<float>();
    outMessage.axis.r1 = axis["r1"].get<float>();
    outMessage.axis.r2 = axis["r2"].get<float>();
    outMessage.axis.r3 = axis["r3"].get<float>();
    outMessage.axis.r4 = axis["r4"].get<float>();

    const auto& reference = payloadJson["reference"];

    const auto readPosition3 = [](const nlohmann::json& object, const char* key, AppPosition3& outPosition) {
        if (!object.contains(key) || !object[key].is_object()) {
            return false;
        }

        const auto& pos = object[key];
        if (!pos.contains("x") || !pos["x"].is_number() ||
            !pos.contains("y") || !pos["y"].is_number() ||
            !pos.contains("z") || !pos["z"].is_number()) {
            return false;
        }

        outPosition.x = pos["x"].get<float>();
        outPosition.y = pos["y"].get<float>();
        outPosition.z = pos["z"].get<float>();
        return true;
    };

    if (!readPosition3(reference, "posCalib", outMessage.reference.posCalib) ||
        !readPosition3(reference, "posCalibSec", outMessage.reference.posCalibSec) ||
        !readPosition3(reference, "posPark", outMessage.reference.posPark) ||
        !readPosition3(reference, "posCamBot", outMessage.reference.posCamBot) ||
        !readPosition3(reference, "posDiscard", outMessage.reference.posDiscard) ||
        !readPosition3(reference, "posChange", outMessage.reference.posChange)) {
        return false;
    }

    outMessage.actors.clear();
    if (payloadJson.contains("actors")) {
        if (!payloadJson["actors"].is_array()) {
            return false;
        }

        for (const auto& actorJson : payloadJson["actors"]) {
            if (!actorJson.is_object() ||
                !actorJson.contains("id") || !actorJson["id"].is_string() ||
                !actorJson.contains("value") || !actorJson["value"].is_number_integer()) {
                return false;
            }

            outMessage.actors.push_back({
                actorJson["id"].get<std::string>(),
                actorJson["value"].get<int>()});
        }
    }

    return true;
}

} // namespace control
} // namespace opensmt
