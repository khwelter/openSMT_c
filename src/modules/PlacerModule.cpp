#include "openSMT/modules/PlacerModule.hpp"

#include <chrono>
#include <cstdio>

#include "openSMT/comm/Frame.hpp"

namespace opensmt {
namespace modules {

namespace {

std::uint64_t nowEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}

} // namespace

PlacerModule::PlacerModule(std::string moduleId)
    : moduleId_(std::move(moduleId)), started_(false)
{
}

const std::string& PlacerModule::id() const
{
    return moduleId_;
}

const std::string& PlacerModule::type() const
{
    static const std::string kType = "placer";
    return kType;
}

bool PlacerModule::start(const ModuleContext& context)
{
    if (started_) {
        return true;
    }

    context.bus.subscribe(moduleId_, [this](const comm::Frame& frame) {
        std::fprintf(
            stderr,
            "[placer][%s] received payloadType=%s from=%s\n",
            moduleId_.c_str(),
            frame.payloadType.c_str(),
            frame.sourceModule.c_str());
    });

    comm::Frame startupFrame;
    startupFrame.timestampEpochMs = nowEpochMs();
    startupFrame.destinationModule = comm::kBroadcastDestination;
    startupFrame.sourceModule = moduleId_;
    startupFrame.payloadType = "module-started";
    startupFrame.payloadJson = "{\"module\":\"placer\"}";

    if (!context.bus.publish(startupFrame)) {
        std::fprintf(stderr, "[placer][%s] failed to publish startup frame\n", moduleId_.c_str());
        return false;
    }

    started_ = true;
    std::fprintf(stderr, "[module][%s] started\n", moduleId_.c_str());
    return true;
}

void PlacerModule::stop()
{
    if (!started_) {
        return;
    }

    started_ = false;
    std::fprintf(stderr, "[module][%s] stopped\n", moduleId_.c_str());
}

} // namespace modules
} // namespace opensmt
