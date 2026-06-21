#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "openSMT/config/AppConfig.hpp"
#include "openSMT/drivers/DeviceDriver.hpp"
#include "openSMT/hw/IHardwareDriver.hpp"
#include "openSMT/modules/IModule.hpp"

namespace opensmt {
namespace modules {

class DeviceDriverRuntimeModule : public IModule {
public:
    DeviceDriverRuntimeModule(std::string moduleId, const config::AppConfig& appConfig);

    const std::string& id() const override;
    const std::string& type() const override;

    bool start(const ModuleContext& context) override;
    void stop() override;

private:
    std::shared_ptr<hw::IHardwareDriver> createHardware(const config::HardwareDriverConfig& hardwareConfig) const;

    std::string moduleId_;
    const config::AppConfig& appConfig_;
    bool started_;

    std::unordered_map<std::string, std::shared_ptr<hw::IHardwareDriver>> hardwareById_;
    std::vector<std::unique_ptr<drivers::DeviceDriver>> deviceDrivers_;
};

} // namespace modules
} // namespace opensmt
