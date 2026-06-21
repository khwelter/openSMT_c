#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "openSMT/comm/Frame.hpp"

namespace opensmt {
namespace comm {

struct Endpoint {
    std::string ip;
    std::uint16_t port;
};

class MessageBus {
public:
    using Handler = std::function<void(const Frame&)>;

    MessageBus();
    ~MessageBus();

    bool start(const std::string& listenIp, std::uint16_t listenPort);
    void stop();

    bool subscribe(const std::string& moduleName, Handler handler);
    bool addMonitor(Handler handler);
    bool addRoute(const std::string& destinationModule, const Endpoint& endpoint);

    bool publish(const Frame& frame);

private:
    bool bindSocket(const std::string& listenIp, std::uint16_t listenPort);
    bool sendUdp(const Frame& frame, const Endpoint& endpoint);
    void receiverLoop();
    void dispatchLocal(const Frame& frame);

    int socketFd_;
    std::atomic<bool> running_;
    std::thread receiverThread_;

    std::mutex handlersMutex_;
    std::unordered_map<std::string, std::vector<Handler>> handlers_;
    std::vector<Handler> monitors_;

    std::mutex routesMutex_;
    std::unordered_map<std::string, Endpoint> routes_;
};

} // namespace comm
} // namespace opensmt
