#pragma once

#include <string>

#include "openSMT/comm/MessageBus.hpp"

namespace opensmt {
namespace modules {

struct ModuleContext {
    comm::MessageBus& bus;
};

class IModule {
public:
    virtual ~IModule() = default;

    virtual const std::string& id() const = 0;
    virtual const std::string& type() const = 0;

    virtual bool start(const ModuleContext& context) = 0;
    virtual void stop() = 0;
};

} // namespace modules
} // namespace opensmt
