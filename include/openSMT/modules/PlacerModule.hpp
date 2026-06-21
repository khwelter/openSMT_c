#pragma once

#include <string>

#include "openSMT/modules/IModule.hpp"

namespace opensmt {
namespace modules {

class PlacerModule : public IModule {
public:
    explicit PlacerModule(std::string moduleId);

    const std::string& id() const override;
    const std::string& type() const override;

    bool start(const ModuleContext& context) override;
    void stop() override;

private:
    std::string moduleId_;
    bool started_;
};

} // namespace modules
} // namespace opensmt
