#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "openSMT/comm/MessageBus.hpp"

namespace opensmt {
namespace config {

struct Position3Config {
    float x;
    float y;
    float z;
};

struct AppReferencePositionsConfig {
    Position3Config posCalib;
    Position3Config posCalibSec;
    Position3Config posPark;
    Position3Config posCamBot;
    Position3Config posDiscard;
    Position3Config posChange;
};

struct MotionConfig {
    float moveXYSlackThresholdMm;
    float moveXYSlackCompensationMm;
};

struct ModuleConfig {
    std::string id;
    std::string type;
    bool enabled;
};

struct CommunicationConfig {
    std::string listenIp;
    std::uint16_t listenPort;
    std::vector<std::pair<std::string, comm::Endpoint>> routes;
};

struct HardwareDriverConfig {
    std::string id;
    std::string driverType;
    bool enabled;

    std::string portName;
    int baudRate;
    int dataBits;
    std::string parity;
    int stopBits;
    std::string serialProtocol;
    int defaultRotateSpeed;
    int serialResponseTimeoutMs;
};

struct DeviceDriverConfig {
    std::string id;
    std::string hardwareId;
    bool enabled;
};

struct ActorConfig {
    std::string id;
    std::string hardwareDriverId;
    std::string deviceDriverId;
    int index;
    int minValue;
    int maxValue;
    int offValue;
    int onValue;
    std::vector<int> allowedValues;
};

struct AppConfig {
    std::string projectName;
    AppReferencePositionsConfig referencePositions;
    MotionConfig motion;
    CommunicationConfig communication;
    std::vector<HardwareDriverConfig> hardwareDrivers;
    std::vector<DeviceDriverConfig> deviceDrivers;
    std::vector<ActorConfig> actors;
    std::vector<ModuleConfig> modules;
};

} // namespace config
} // namespace opensmt
