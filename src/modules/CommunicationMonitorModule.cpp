#include "openSMT/modules/CommunicationMonitorModule.hpp"

#include <cstdio>

#include "openSMT/comm/Frame.hpp"

namespace opensmt {
namespace modules {

CommunicationMonitorModule::CommunicationMonitorModule(std::string moduleId)
    : moduleId_(std::move(moduleId)), started_(false)
{
}

const std::string& CommunicationMonitorModule::id() const
{
    return moduleId_;
}

const std::string& CommunicationMonitorModule::type() const
{
    static const std::string kType = "communication-monitor";
    return kType;
}

bool CommunicationMonitorModule::start(const ModuleContext& context)
{
    if (started_) {
        return true;
    }

    context.bus.addMonitor([this](const comm::Frame& frame) {
        std::fprintf(
            stderr,
            "[comm-monitor][%s] %s -> %s type=%s ts=%llu\n",
            moduleId_.c_str(),
            frame.sourceModule.c_str(),
            frame.destinationModule.c_str(),
            frame.payloadType.c_str(),
            static_cast<unsigned long long>(frame.timestampEpochMs));
    });

    started_ = true;
    std::fprintf(stderr, "[module][%s] started\n", moduleId_.c_str());
    return true;
}

void CommunicationMonitorModule::stop()
{
    if (!started_) {
        return;
    }

    started_ = false;
    std::fprintf(stderr, "[module][%s] stopped\n", moduleId_.c_str());
}

} // namespace modules
} // namespace opensmt
