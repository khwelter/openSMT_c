#include "openSMT/drivers/GenericDeviceDriver.hpp"

#include <cstdio>

#include <nlohmann/json.hpp>

namespace opensmt {
namespace drivers {

GenericDeviceDriver::GenericDeviceDriver(
    std::string moduleId,
    std::shared_ptr<hw::IHardwareDriver> hardwareDriver,
    float moveXYSlackThresholdMm,
    float moveXYSlackCompensationMm)
    : DeviceDriver(
          std::move(moduleId),
          std::move(hardwareDriver),
          moveXYSlackThresholdMm,
          moveXYSlackCompensationMm)
{
}

bool GenericDeviceDriver::moveXY(float xPos, float yPos, float speed)
{
    nlohmann::json payloadJson;
    payloadJson["xPos"] = xPos;
    payloadJson["yPos"] = yPos;
    payloadJson["speed"] = speed;

    const hw::HardwareCommand command{id(), "moveXY", payloadJson.dump()};
    return hardware().execute(command).ok;
}

bool GenericDeviceDriver::moveZ(float zPos, float speed)
{
    nlohmann::json payloadJson;
    payloadJson["zPos"] = zPos;
    payloadJson["speed"] = speed;

    const hw::HardwareCommand command{id(), "moveZ", payloadJson.dump()};
    return hardware().execute(command).ok;
}

bool GenericDeviceDriver::rotate(float angle, float speed)
{
    nlohmann::json payloadJson;
    payloadJson["angle"] = angle;
    payloadJson["speed"] = speed;

    const hw::HardwareCommand command{id(), "rotate", payloadJson.dump()};
    return hardware().execute(command).ok;
}

bool GenericDeviceDriver::disableAllSteppers()
{
    const hw::HardwareCommand command{id(), "disableAllSteppers", "{}"};
    return hardware().execute(command).ok;
}

bool GenericDeviceDriver::operateActor(int digitalChannel, bool state)
{
    nlohmann::json payloadJson;
    payloadJson["channel"] = digitalChannel;
    payloadJson["state"] = state;

    const hw::HardwareCommand command{id(), "operateActorDigital", payloadJson.dump()};
    return hardware().execute(command).ok;
}

bool GenericDeviceDriver::operateActor(int analogChannel, float value)
{
    nlohmann::json payloadJson;
    payloadJson["channel"] = analogChannel;
    payloadJson["value"] = value;

    const hw::HardwareCommand command{id(), "operateActorAnalog", payloadJson.dump()};
    return hardware().execute(command).ok;
}

bool GenericDeviceDriver::readSensorDigital(int digitalChannel, bool& outValue)
{
    nlohmann::json payloadJson;
    payloadJson["channel"] = digitalChannel;

    const hw::HardwareCommand command{id(), "readSensorDigital", payloadJson.dump()};
    const hw::HardwareResponse response = hardware().execute(command);
    if (!response.ok) {
        return false;
    }

    outValue = false;
    return true;
}

bool GenericDeviceDriver::readSensorAnalog(int analogChannel, float& outValue)
{
    nlohmann::json payloadJson;
    payloadJson["channel"] = analogChannel;

    const hw::HardwareCommand command{id(), "readSensorAnalog", payloadJson.dump()};
    const hw::HardwareResponse response = hardware().execute(command);
    if (!response.ok) {
        return false;
    }

    outValue = 0.0f;
    return true;
}

std::string GenericDeviceDriver::readVersionNumber()
{
    const hw::HardwareCommand command{id(), "readVersionNumber", "{}"};
    const hw::HardwareResponse response = hardware().execute(command);
    if (!response.ok) {
        return "";
    }

    return "0.1.0";
}

} // namespace drivers
} // namespace opensmt
