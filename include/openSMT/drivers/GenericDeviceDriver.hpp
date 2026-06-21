#pragma once

#include <memory>
#include <string>

#include "openSMT/drivers/DeviceDriver.hpp"

namespace opensmt {
namespace drivers {

class GenericDeviceDriver : public DeviceDriver {
public:
    GenericDeviceDriver(
        std::string moduleId,
        std::shared_ptr<hw::IHardwareDriver> hardwareDriver,
        float moveXYSlackThresholdMm,
        float moveXYSlackCompensationMm);

    bool moveXY(float xPos, float yPos, float speed) override;
    bool moveZ(float zPos, float speed) override;
    bool rotate(float angle, float speed) override;
    bool disableAllSteppers() override;
    bool operateActor(int digitalChannel, bool state) override;
    bool operateActor(int analogChannel, float value) override;
    bool readSensorDigital(int digitalChannel, bool& outValue) override;
    bool readSensorAnalog(int analogChannel, float& outValue) override;
    std::string readVersionNumber() override;
};

} // namespace drivers
} // namespace opensmt
