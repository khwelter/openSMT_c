#include "openSMT/modules/DeviceDriverRuntimeModule.hpp"

#include <cstdio>

#include "openSMT/drivers/GenericDeviceDriver.hpp"
#include "openSMT/hw/CanHardwareDriver.hpp"
#include "openSMT/hw/SerialHardwareDriver.hpp"

namespace opensmt {
namespace modules {

DeviceDriverRuntimeModule::DeviceDriverRuntimeModule(std::string moduleId, const config::AppConfig& appConfig)
    : moduleId_(std::move(moduleId)), appConfig_(appConfig), started_(false)
{
}

const std::string& DeviceDriverRuntimeModule::id() const
{
    return moduleId_;
}

const std::string& DeviceDriverRuntimeModule::type() const
{
    static const std::string kType = "device-driver-runtime";
    return kType;
}

bool DeviceDriverRuntimeModule::start(const ModuleContext& context)
{
    if (started_) {
        return true;
    }

    for (const auto& hardwareConfig : appConfig_.hardwareDrivers) {
        if (!hardwareConfig.enabled) {
            continue;
        }

        std::shared_ptr<hw::IHardwareDriver> hardwareDriver = createHardware(hardwareConfig);
        if (!hardwareDriver) {
            std::fprintf(stderr, "[module][%s] unknown hardware driver type '%s'\n", moduleId_.c_str(), hardwareConfig.driverType.c_str());
            return false;
        }

        if (!hardwareDriver->start()) {
            std::fprintf(stderr, "[module][%s] failed to start hardware '%s'\n", moduleId_.c_str(), hardwareConfig.id.c_str());
            return false;
        }

        hardwareById_[hardwareConfig.id] = hardwareDriver;
    }

    for (const auto& deviceConfig : appConfig_.deviceDrivers) {
        if (!deviceConfig.enabled) {
            continue;
        }

        const auto hardwareIt = hardwareById_.find(deviceConfig.hardwareId);
        if (hardwareIt == hardwareById_.end()) {
            std::fprintf(
                stderr,
                "[module][%s] missing hardware '%s' for device '%s'\n",
                moduleId_.c_str(),
                deviceConfig.hardwareId.c_str(),
                deviceConfig.id.c_str());
            return false;
        }

        std::unique_ptr<drivers::DeviceDriver> deviceDriver =
            std::make_unique<drivers::GenericDeviceDriver>(
                deviceConfig.id,
                hardwareIt->second,
                appConfig_.motion.moveXYSlackThresholdMm,
                appConfig_.motion.moveXYSlackCompensationMm);

        if (!deviceDriver->start(context.bus)) {
            std::fprintf(stderr, "[module][%s] failed to start device driver '%s'\n", moduleId_.c_str(), deviceConfig.id.c_str());
            return false;
        }

        deviceDrivers_.push_back(std::move(deviceDriver));
    }

    started_ = true;
    std::fprintf(
        stderr,
        "[module][%s] started with %zu hardware driver(s), %zu device driver(s)\n",
        moduleId_.c_str(),
        hardwareById_.size(),
        deviceDrivers_.size());
    return true;
}

void DeviceDriverRuntimeModule::stop()
{
    if (!started_) {
        return;
    }

    for (auto it = deviceDrivers_.rbegin(); it != deviceDrivers_.rend(); ++it) {
        (*it)->stop();
    }
    deviceDrivers_.clear();

    for (auto& entry : hardwareById_) {
        entry.second->stop();
    }
    hardwareById_.clear();

    started_ = false;
    std::fprintf(stderr, "[module][%s] stopped\n", moduleId_.c_str());
}

std::shared_ptr<hw::IHardwareDriver> DeviceDriverRuntimeModule::createHardware(
    const config::HardwareDriverConfig& hardwareConfig) const
{
    if (hardwareConfig.driverType == "serial") {
        return std::make_shared<hw::SerialHardwareDriver>(
            hardwareConfig.id,
            hardwareConfig.portName,
            hardwareConfig.baudRate,
            hardwareConfig.dataBits,
            hardwareConfig.parity,
            hardwareConfig.stopBits,
            hardwareConfig.serialProtocol,
                hardwareConfig.defaultRotateSpeed,
                hardwareConfig.serialResponseTimeoutMs);
    }

    if (hardwareConfig.driverType == "can") {
        return std::make_shared<hw::CanHardwareDriver>(hardwareConfig.id);
    }

    return nullptr;
}

} // namespace modules
} // namespace opensmt
