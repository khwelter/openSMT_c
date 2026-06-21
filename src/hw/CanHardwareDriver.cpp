#include "openSMT/hw/CanHardwareDriver.hpp"

#include <cstdio>

#include <nlohmann/json.hpp>

namespace opensmt {
namespace hw {

CanHardwareDriver::CanHardwareDriver(std::string hardwareId)
    : hardwareId_(std::move(hardwareId)), started_(false)
{
}

const std::string& CanHardwareDriver::id() const
{
    return hardwareId_;
}

const std::string& CanHardwareDriver::type() const
{
    static const std::string kType = "can";
    return kType;
}

bool CanHardwareDriver::start()
{
    if (started_) {
        return true;
    }

    started_ = true;
    std::fprintf(stderr, "[hw][%s] CAN placeholder start\n", hardwareId_.c_str());
    return true;
}

void CanHardwareDriver::stop()
{
    if (!started_) {
        return;
    }

    started_ = false;
    std::fprintf(stderr, "[hw][%s] CAN placeholder stop\n", hardwareId_.c_str());
}

HardwareResponse CanHardwareDriver::execute(const HardwareCommand& command)
{
    if (!started_) {
        return {false, "{}", "hardware not started"};
    }

    nlohmann::json resultJson;
    resultJson["hardwareId"] = hardwareId_;
    resultJson["action"] = command.action;
    resultJson["accepted"] = false;
    resultJson["detail"] = "CAN driver placeholder";

    return {false, resultJson.dump(), "CAN placeholder does not execute commands yet"};
}

} // namespace hw
} // namespace opensmt
