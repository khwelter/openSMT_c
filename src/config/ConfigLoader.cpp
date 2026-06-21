#include "openSMT/config/ConfigLoader.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace opensmt {
namespace config {

namespace {

bool loadJsonFile(const std::string& filePath, nlohmann::json& outJson, std::string& outError)
{
    std::ifstream input(filePath);
    if (!input.is_open()) {
        outError = "Unable to open config file: " + filePath;
        return false;
    }

    try {
        input >> outJson;
    } catch (const std::exception& ex) {
        outError = "Invalid JSON in file: " + filePath + " error: " + ex.what();
        return false;
    }

    return true;
}

std::string directoryOf(const std::string& filePath)
{
    const std::size_t pos = filePath.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    return filePath.substr(0, pos);
}

std::string joinPath(const std::string& baseDir, const std::string& relativePath)
{
    if (relativePath.empty()) {
        return baseDir;
    }

    if (!relativePath.empty() && relativePath.front() == '/') {
        return relativePath;
    }

    if (baseDir.empty() || baseDir == ".") {
        return relativePath;
    }

    return baseDir + "/" + relativePath;
}

bool expandIncludes(const std::string& filePath, nlohmann::json& ioJson, std::string& outError)
{
    if (!ioJson.is_object()) {
        return true;
    }

    if (!ioJson.contains("includes")) {
        return true;
    }

    if (!ioJson["includes"].is_object()) {
        outError = "'includes' must be an object in file: " + filePath;
        return false;
    }

    const std::string baseDir = directoryOf(filePath);

    for (auto it = ioJson["includes"].begin(); it != ioJson["includes"].end(); ++it) {
        if (!it.value().is_string()) {
            outError = "Include path for key '" + it.key() + "' must be a string in file: " + filePath;
            return false;
        }

        const std::string childPath = joinPath(baseDir, it.value().get<std::string>());
        nlohmann::json childJson;
        if (!loadJsonFile(childPath, childJson, outError)) {
            return false;
        }

        if (!expandIncludes(childPath, childJson, outError)) {
            return false;
        }

        ioJson[it.key()] = childJson;
    }

    ioJson.erase("includes");
    return true;
}

bool readRequiredString(
    const nlohmann::json& json,
    const char* key,
    std::string& outValue,
    std::string& outError)
{
    if (!json.contains(key) || !json[key].is_string()) {
        outError = std::string("Missing or invalid string key: ") + key;
        return false;
    }

    outValue = json[key].get<std::string>();
    return true;
}

bool readRequiredInt(
    const nlohmann::json& json,
    const char* key,
    int& outValue,
    std::string& outError)
{
    if (!json.contains(key) || !json[key].is_number_integer()) {
        outError = std::string("Missing or invalid integer key: ") + key;
        return false;
    }

    outValue = json[key].get<int>();
    return true;
}

bool readOptionalFloat(
    const nlohmann::json& json,
    const char* key,
    float& outValue,
    std::string& outError)
{
    if (!json.contains(key)) {
        return true;
    }

    if (!json[key].is_number()) {
        outError = std::string("Missing or invalid float key: ") + key;
        return false;
    }

    outValue = json[key].get<float>();
    return true;
}

bool readOptionalPosition3(
    const nlohmann::json& json,
    const char* key,
    Position3Config& outValue,
    std::string& outError)
{
    if (!json.contains(key)) {
        return true;
    }

    if (!json[key].is_object()) {
        outError = std::string("Missing or invalid object key: ") + key;
        return false;
    }

    const nlohmann::json& positionJson = json[key];
    if (!positionJson.contains("x") || !positionJson["x"].is_number() ||
        !positionJson.contains("y") || !positionJson["y"].is_number() ||
        !positionJson.contains("z") || !positionJson["z"].is_number()) {
        outError = std::string("Position key '") + key + "' requires numeric x, y, z";
        return false;
    }

    outValue.x = positionJson["x"].get<float>();
    outValue.y = positionJson["y"].get<float>();
    outValue.z = positionJson["z"].get<float>();
    return true;
}

bool readOptionalPosition3WithAliases(
    const nlohmann::json& json,
    const std::vector<std::string>& keys,
    Position3Config& outValue,
    std::string& outError)
{
    for (const auto& key : keys) {
        if (json.contains(key)) {
            return readOptionalPosition3(json, key.c_str(), outValue, outError);
        }
    }

    return true;
}

} // namespace

bool ConfigLoader::load(const std::string& rootConfigPath, AppConfig& outConfig, std::string& outError) const
{
    nlohmann::json rootJson;
    if (!loadJsonFile(rootConfigPath, rootJson, outError)) {
        return false;
    }

    if (!expandIncludes(rootConfigPath, rootJson, outError)) {
        return false;
    }

    if (!rootJson.contains("project") || !rootJson["project"].is_object()) {
        outError = "Missing 'project' object in expanded config";
        return false;
    }

    if (!readRequiredString(rootJson["project"], "projectName", outConfig.projectName, outError)) {
        return false;
    }

    outConfig.referencePositions.posCalib = {0.0f, 0.0f, 0.0f};
    outConfig.referencePositions.posCalibSec = {0.0f, 0.0f, 0.0f};
    outConfig.referencePositions.posPark = {0.0f, 0.0f, 0.0f};
    outConfig.referencePositions.posCamBot = {0.0f, 0.0f, 0.0f};
    outConfig.referencePositions.posDiscard = {0.0f, 0.0f, 0.0f};
    outConfig.referencePositions.posChange = {0.0f, 0.0f, 0.0f};
    outConfig.motion.moveXYSlackThresholdMm = 1.0f;
    outConfig.motion.moveXYSlackCompensationMm = 1.0f;

    const nlohmann::json& projectJson = rootJson["project"];
    if (!readOptionalPosition3WithAliases(
            projectJson,
            {"posCalib", "posCalib(rationPos)", "posCalibRotationPos", "posCalib(rotationPos)"},
            outConfig.referencePositions.posCalib,
            outError) ||
        !readOptionalPosition3WithAliases(
            projectJson,
            {"posCalibSec", "posCalib(ration)Sec", "posCalibRotationSec", "posCalib(rotation)Sec"},
            outConfig.referencePositions.posCalibSec,
            outError) ||
        !readOptionalPosition3(projectJson, "posPark", outConfig.referencePositions.posPark, outError) ||
        !readOptionalPosition3(projectJson, "posCamBot", outConfig.referencePositions.posCamBot, outError) ||
        !readOptionalPosition3(projectJson, "posDiscard", outConfig.referencePositions.posDiscard, outError) ||
        !readOptionalPosition3(projectJson, "posChange", outConfig.referencePositions.posChange, outError) ||
        !readOptionalFloat(projectJson, "moveXYSlackThresholdMm", outConfig.motion.moveXYSlackThresholdMm, outError) ||
        !readOptionalFloat(projectJson, "moveXYSlackCompensationMm", outConfig.motion.moveXYSlackCompensationMm, outError)) {
        return false;
    }

    if (outConfig.motion.moveXYSlackThresholdMm < 0.0f || outConfig.motion.moveXYSlackCompensationMm < 0.0f) {
        outError = "moveXYSlackThresholdMm and moveXYSlackCompensationMm must be >= 0";
        return false;
    }

    if (!rootJson.contains("runtime") || !rootJson["runtime"].is_object()) {
        outError = "Missing 'runtime' object in expanded config";
        return false;
    }

    const nlohmann::json& runtimeJson = rootJson["runtime"];
    if (!runtimeJson.contains("communication") || !runtimeJson["communication"].is_object()) {
        outError = "Missing 'runtime.communication' object";
        return false;
    }

    const nlohmann::json& communicationJson = runtimeJson["communication"];
    if (!readRequiredString(communicationJson, "listenIp", outConfig.communication.listenIp, outError)) {
        return false;
    }

    if (!communicationJson.contains("listenPort") || !communicationJson["listenPort"].is_number_unsigned()) {
        outError = "Missing or invalid unsigned key: runtime.communication.listenPort";
        return false;
    }

    outConfig.communication.listenPort = communicationJson["listenPort"].get<std::uint16_t>();
    outConfig.communication.routes.clear();

    if (communicationJson.contains("routes")) {
        if (!communicationJson["routes"].is_array()) {
            outError = "runtime.communication.routes must be an array";
            return false;
        }

        for (const auto& routeJson : communicationJson["routes"]) {
            if (!routeJson.is_object()) {
                outError = "Each route entry must be an object";
                return false;
            }

            std::string destination;
            std::string ip;
            if (!readRequiredString(routeJson, "destinationModule", destination, outError) ||
                !readRequiredString(routeJson, "ip", ip, outError)) {
                return false;
            }

            if (!routeJson.contains("port") || !routeJson["port"].is_number_unsigned()) {
                outError = "Each route entry requires unsigned key: port";
                return false;
            }

            comm::Endpoint endpoint;
            endpoint.ip = ip;
            endpoint.port = routeJson["port"].get<std::uint16_t>();
            outConfig.communication.routes.push_back({destination, endpoint});
        }
    }

    outConfig.hardwareDrivers.clear();
    if (!runtimeJson.contains("hardwareDrivers") || !runtimeJson["hardwareDrivers"].is_array()) {
        outError = "Missing 'runtime.hardwareDrivers' array";
        return false;
    }

    for (const auto& hardwareJson : runtimeJson["hardwareDrivers"]) {
        if (!hardwareJson.is_object()) {
            outError = "Each hardware driver entry must be an object";
            return false;
        }

        HardwareDriverConfig hardwareConfig;
        if (!readRequiredString(hardwareJson, "id", hardwareConfig.id, outError) ||
            !readRequiredString(hardwareJson, "driverType", hardwareConfig.driverType, outError)) {
            return false;
        }

        hardwareConfig.enabled = true;
        if (hardwareJson.contains("enabled")) {
            if (!hardwareJson["enabled"].is_boolean()) {
                outError = "hardwareDrivers[].enabled must be boolean";
                return false;
            }
            hardwareConfig.enabled = hardwareJson["enabled"].get<bool>();
        }

        hardwareConfig.portName = hardwareConfig.id;
        if (hardwareJson.contains("portName")) {
            if (!hardwareJson["portName"].is_string()) {
                outError = "hardwareDrivers[].portName must be string";
                return false;
            }
            hardwareConfig.portName = hardwareJson["portName"].get<std::string>();
        }

        hardwareConfig.baudRate = 115200;
        if (hardwareJson.contains("baudRate")) {
            if (!hardwareJson["baudRate"].is_number_integer()) {
                outError = "hardwareDrivers[].baudRate must be integer";
                return false;
            }
            hardwareConfig.baudRate = hardwareJson["baudRate"].get<int>();
        }

        hardwareConfig.dataBits = 8;
        if (hardwareJson.contains("dataBits")) {
            if (!hardwareJson["dataBits"].is_number_integer()) {
                outError = "hardwareDrivers[].dataBits must be integer";
                return false;
            }
            hardwareConfig.dataBits = hardwareJson["dataBits"].get<int>();
        }

        hardwareConfig.parity = "N";
        if (hardwareJson.contains("parity")) {
            if (!hardwareJson["parity"].is_string()) {
                outError = "hardwareDrivers[].parity must be string";
                return false;
            }
            hardwareConfig.parity = hardwareJson["parity"].get<std::string>();
        }

        hardwareConfig.stopBits = 1;
        if (hardwareJson.contains("stopBits")) {
            if (!hardwareJson["stopBits"].is_number_integer()) {
                outError = "hardwareDrivers[].stopBits must be integer";
                return false;
            }
            hardwareConfig.stopBits = hardwareJson["stopBits"].get<int>();
        }

        hardwareConfig.serialProtocol = "marlin-gcode";
        if (hardwareJson.contains("serialProtocol")) {
            if (!hardwareJson["serialProtocol"].is_string()) {
                outError = "hardwareDrivers[].serialProtocol must be string";
                return false;
            }
            hardwareConfig.serialProtocol = hardwareJson["serialProtocol"].get<std::string>();
        }

        hardwareConfig.defaultRotateSpeed = 1200;
        if (hardwareJson.contains("defaultRotateSpeed")) {
            if (!hardwareJson["defaultRotateSpeed"].is_number_integer()) {
                outError = "hardwareDrivers[].defaultRotateSpeed must be integer";
                return false;
            }
            hardwareConfig.defaultRotateSpeed = hardwareJson["defaultRotateSpeed"].get<int>();
        }

        hardwareConfig.serialResponseTimeoutMs = 30000;
        if (hardwareJson.contains("serialResponseTimeoutMs")) {
            if (!hardwareJson["serialResponseTimeoutMs"].is_number_integer()) {
                outError = "hardwareDrivers[].serialResponseTimeoutMs must be integer";
                return false;
            }
            hardwareConfig.serialResponseTimeoutMs = hardwareJson["serialResponseTimeoutMs"].get<int>();
        }

        outConfig.hardwareDrivers.push_back(hardwareConfig);
    }

    outConfig.deviceDrivers.clear();
    if (!runtimeJson.contains("deviceDrivers") || !runtimeJson["deviceDrivers"].is_array()) {
        outError = "Missing 'runtime.deviceDrivers' array";
        return false;
    }

    for (const auto& deviceJson : runtimeJson["deviceDrivers"]) {
        if (!deviceJson.is_object()) {
            outError = "Each device driver entry must be an object";
            return false;
        }

        DeviceDriverConfig deviceConfig;
        if (!readRequiredString(deviceJson, "id", deviceConfig.id, outError) ||
            !readRequiredString(deviceJson, "hardware", deviceConfig.hardwareId, outError)) {
            return false;
        }

        deviceConfig.enabled = true;
        if (deviceJson.contains("enabled")) {
            if (!deviceJson["enabled"].is_boolean()) {
                outError = "deviceDrivers[].enabled must be boolean";
                return false;
            }
            deviceConfig.enabled = deviceJson["enabled"].get<bool>();
        }

        outConfig.deviceDrivers.push_back(deviceConfig);
    }

    outConfig.actors.clear();
    if (runtimeJson.contains("actors")) {
        if (!runtimeJson["actors"].is_array()) {
            outError = "runtime.actors must be an array";
            return false;
        }

        auto resolveDeviceDriverForHardware = [&](const std::string& hardwareId, std::string& outDeviceDriverId) -> bool {
            for (const auto& device : outConfig.deviceDrivers) {
                if (device.enabled && device.hardwareId == hardwareId) {
                    outDeviceDriverId = device.id;
                    return true;
                }
            }
            return false;
        };

        for (const auto& actorJson : runtimeJson["actors"]) {
            if (!actorJson.is_object()) {
                outError = "Each actor entry must be an object";
                return false;
            }

            ActorConfig actorConfig;
            if (!readRequiredString(actorJson, "id", actorConfig.id, outError) ||
                !readRequiredString(actorJson, "driver", actorConfig.hardwareDriverId, outError) ||
                !readRequiredInt(actorJson, "index", actorConfig.index, outError) ||
                !readRequiredInt(actorJson, "minValue", actorConfig.minValue, outError) ||
                !readRequiredInt(actorJson, "maxValue", actorConfig.maxValue, outError)) {
                return false;
            }

            if (actorConfig.index < 0 || actorConfig.index > 255) {
                outError = "runtime.actors[].index must be in range 0..255";
                return false;
            }

            if (actorConfig.minValue < 0 || actorConfig.maxValue > 255 ||
                actorConfig.minValue > actorConfig.maxValue) {
                outError = "runtime.actors[] minValue/maxValue must satisfy 0 <= minValue <= maxValue <= 255";
                return false;
            }

            actorConfig.offValue = actorConfig.minValue;
            actorConfig.onValue = actorConfig.maxValue;

            if (actorJson.contains("offValue")) {
                if (!actorJson["offValue"].is_number_integer()) {
                    outError = "runtime.actors[].offValue must be integer";
                    return false;
                }
                actorConfig.offValue = actorJson["offValue"].get<int>();
            }

            if (actorJson.contains("onValue")) {
                if (!actorJson["onValue"].is_number_integer()) {
                    outError = "runtime.actors[].onValue must be integer";
                    return false;
                }
                actorConfig.onValue = actorJson["onValue"].get<int>();
            }

            if (actorConfig.offValue < 0 || actorConfig.offValue > 255 ||
                actorConfig.onValue < 0 || actorConfig.onValue > 255) {
                outError = "runtime.actors[].offValue/onValue must be in range 0..255";
                return false;
            }

            if (actorJson.contains("allowedValues")) {
                if (!actorJson["allowedValues"].is_array()) {
                    outError = "runtime.actors[].allowedValues must be an array";
                    return false;
                }

                for (const auto& value : actorJson["allowedValues"]) {
                    if (!value.is_number_integer()) {
                        outError = "runtime.actors[].allowedValues entries must be integers";
                        return false;
                    }

                    const int intValue = value.get<int>();
                    if (intValue < 0 || intValue > 255) {
                        outError = "runtime.actors[].allowedValues entries must be in range 0..255";
                        return false;
                    }

                    actorConfig.allowedValues.push_back(intValue);
                }
            }

            if (actorJson.contains("deviceDriver")) {
                if (!actorJson["deviceDriver"].is_string()) {
                    outError = "runtime.actors[].deviceDriver must be string";
                    return false;
                }
                actorConfig.deviceDriverId = actorJson["deviceDriver"].get<std::string>();
            } else if (!resolveDeviceDriverForHardware(actorConfig.hardwareDriverId, actorConfig.deviceDriverId)) {
                outError = "runtime.actors[] could not resolve deviceDriver for driver='" + actorConfig.hardwareDriverId + "'";
                return false;
            }

            outConfig.actors.push_back(actorConfig);
        }
    }

    outConfig.modules.clear();
    if (!runtimeJson.contains("modules") || !runtimeJson["modules"].is_array()) {
        outError = "Missing 'runtime.modules' array";
        return false;
    }

    for (const auto& moduleJson : runtimeJson["modules"]) {
        if (!moduleJson.is_object()) {
            outError = "Each module entry must be an object";
            return false;
        }

        ModuleConfig moduleConfig;
        if (!readRequiredString(moduleJson, "id", moduleConfig.id, outError) ||
            !readRequiredString(moduleJson, "type", moduleConfig.type, outError)) {
            return false;
        }

        moduleConfig.enabled = true;
        if (moduleJson.contains("enabled")) {
            if (!moduleJson["enabled"].is_boolean()) {
                outError = "Module 'enabled' must be boolean";
                return false;
            }
            moduleConfig.enabled = moduleJson["enabled"].get<bool>();
        }

        outConfig.modules.push_back(moduleConfig);
    }

    return true;
}

} // namespace config
} // namespace opensmt
