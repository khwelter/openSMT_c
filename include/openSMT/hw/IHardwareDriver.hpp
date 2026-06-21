#pragma once

#include <string>

namespace opensmt {
namespace hw {

struct HardwareCommand {
    std::string deviceDriverId;
    std::string action;
    std::string payloadJson;
};

struct HardwareResponse {
    bool ok;
    std::string payloadJson;
    std::string error;
};

class IHardwareDriver {
public:
    virtual ~IHardwareDriver() = default;

    virtual const std::string& id() const = 0;
    virtual const std::string& type() const = 0;

    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual HardwareResponse execute(const HardwareCommand& command) = 0;
};

} // namespace hw
} // namespace opensmt
