#pragma once

#include <string>

#include "openSMT/hw/IHardwareDriver.hpp"

namespace opensmt {
namespace hw {

class CanHardwareDriver : public IHardwareDriver {
public:
    explicit CanHardwareDriver(std::string hardwareId);

    const std::string& id() const override;
    const std::string& type() const override;

    bool start() override;
    void stop() override;

    HardwareResponse execute(const HardwareCommand& command) override;

private:
    std::string hardwareId_;
    bool started_;
};

} // namespace hw
} // namespace opensmt
