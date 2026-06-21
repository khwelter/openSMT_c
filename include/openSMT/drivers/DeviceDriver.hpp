#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "openSMT/comm/Frame.hpp"
#include "openSMT/comm/MessageBus.hpp"
#include "openSMT/hw/IHardwareDriver.hpp"

namespace opensmt {
namespace drivers {

class DeviceDriver {
public:
    DeviceDriver(std::string moduleId, std::shared_ptr<hw::IHardwareDriver> hardwareDriver);
    virtual ~DeviceDriver() = default;

    const std::string& id() const;

    bool start(comm::MessageBus& bus);
    void stop();

    virtual bool moveXY(float xPos, float yPos, float speed) = 0;
    virtual bool moveZ(float zPos, float speed) = 0;
    virtual bool rotate(float angle, float speed) = 0;
    virtual bool disableAllSteppers() = 0;
    virtual bool operateActor(int digitalChannel, bool state) = 0;
    virtual bool operateActor(int analogChannel, float value) = 0;
    virtual bool readSensorDigital(int digitalChannel, bool& outValue) = 0;
    virtual bool readSensorAnalog(int analogChannel, float& outValue) = 0;
    virtual std::string readVersionNumber() = 0;

protected:
    hw::IHardwareDriver& hardware();

private:
    void onFrame(const comm::Frame& frame);
    bool buildReplyDestination(const comm::Frame& requestFrame, const std::string& replyMode, std::string& outDestination) const;
    void sendReply(
        comm::MessageBus& bus,
        const comm::Frame& requestFrame,
        const std::string& action,
        bool ok,
        const std::string& resultJson,
        const std::string& error,
        const std::string& replyMode);

    std::string moduleId_;
    std::shared_ptr<hw::IHardwareDriver> hardwareDriver_;
    comm::MessageBus* bus_;
    bool started_;
};

} // namespace drivers
} // namespace opensmt
