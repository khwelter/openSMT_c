#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "openSMT/config/AppConfig.hpp"
#include "openSMT/modules/IModule.hpp"

namespace opensmt {

class App {
public:
    bool run(const std::string& rootConfigPath);

    struct GlobalAxisPositions {
        float x = 0.0f;
        float y = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;
        float z3 = 0.0f;
        float z4 = 0.0f;
        float r1 = 0.0f;
        float r2 = 0.0f;
        float r3 = 0.0f;
        float r4 = 0.0f;
    };

private:
    bool registerControlEndpoint();
    void requestStop();
    void initializePositionStore(const config::AppConfig& appConfig);
    void initializeActorStore(const config::AppConfig& appConfig);
    void trackFrameForPositionStore(const comm::Frame& frame);
    void updateAxisPositionFromCommand(const comm::Frame& frame);
    void updateAxisPositionFromReply(const comm::Frame& frame);
    void updateActorValueFromCommand(const comm::Frame& frame);
    bool extractMarlinValue(const std::string& line, const char* label, float& outValue) const;
    void applyMarlinHomingReport(const std::string& boardId, const std::string& reportLine);
    const config::ActorConfig* findActorByDestinationAndIndex(const std::string& destinationModule, int index) const;
    bool replyWithPositions(const comm::Frame& queryFrame);

    bool startCommunication(const config::AppConfig& appConfig);
    bool startModules(const config::AppConfig& appConfig);
    void stopModules();

    std::unique_ptr<modules::IModule> createModule(const config::ModuleConfig& moduleConfig, const config::AppConfig& appConfig);

    comm::MessageBus bus_;
    std::vector<std::unique_ptr<modules::IModule>> modules_;
    config::AppConfig appConfig_;

    std::mutex positionsMutex_;
    GlobalAxisPositions axisPositions_;

    std::mutex actorsMutex_;
    std::map<std::string, int> actorValues_;

    std::mutex stopMutex_;
    std::condition_variable stopCv_;
    bool stopRequested_;
};

} // namespace opensmt
