#include "openSMT/comm/MessageBus.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <unistd.h>

namespace opensmt {
namespace comm {

namespace {

std::string frameToWire(const Frame& frame)
{
    nlohmann::json jsonFrame;
    jsonFrame["timestampEpochMs"] = frame.timestampEpochMs;
    jsonFrame["destinationModule"] = frame.destinationModule;
    jsonFrame["sourceModule"] = frame.sourceModule;
    jsonFrame["payloadType"] = frame.payloadType;
    jsonFrame["payloadJson"] = frame.payloadJson;
    return jsonFrame.dump();
}

bool wireToFrame(const std::string& wireData, Frame& outFrame)
{
    nlohmann::json jsonFrame;
    try {
        jsonFrame = nlohmann::json::parse(wireData);
    } catch (...) {
        return false;
    }

    if (!jsonFrame.contains("timestampEpochMs") ||
        !jsonFrame.contains("destinationModule") ||
        !jsonFrame.contains("sourceModule") ||
        !jsonFrame.contains("payloadType") ||
        !jsonFrame.contains("payloadJson")) {
        return false;
    }

    outFrame.timestampEpochMs = jsonFrame["timestampEpochMs"].get<std::uint64_t>();
    outFrame.destinationModule = jsonFrame["destinationModule"].get<std::string>();
    outFrame.sourceModule = jsonFrame["sourceModule"].get<std::string>();
    outFrame.payloadType = jsonFrame["payloadType"].get<std::string>();
    outFrame.payloadJson = jsonFrame["payloadJson"].get<std::string>();
    return true;
}

} // namespace

MessageBus::MessageBus()
    : socketFd_(-1), running_(false)
{
}

MessageBus::~MessageBus()
{
    stop();
}

bool MessageBus::start(const std::string& listenIp, std::uint16_t listenPort)
{
    if (running_) {
        return true;
    }

    if (!bindSocket(listenIp, listenPort)) {
        return false;
    }

    running_ = true;
    receiverThread_ = std::thread(&MessageBus::receiverLoop, this);
    return true;
}

void MessageBus::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;

    if (socketFd_ >= 0) {
        ::shutdown(socketFd_, SHUT_RDWR);
        ::close(socketFd_);
        socketFd_ = -1;
    }

    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }
}

bool MessageBus::subscribe(const std::string& moduleName, Handler handler)
{
    std::lock_guard<std::mutex> lock(handlersMutex_);
    handlers_[moduleName].push_back(std::move(handler));
    return true;
}

bool MessageBus::addMonitor(Handler handler)
{
    std::lock_guard<std::mutex> lock(handlersMutex_);
    monitors_.push_back(std::move(handler));
    return true;
}

bool MessageBus::addRoute(const std::string& destinationModule, const Endpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(routesMutex_);
    routes_[destinationModule] = endpoint;
    return true;
}

bool MessageBus::publish(const Frame& frame)
{
    dispatchLocal(frame);

    Endpoint endpoint;
    bool hasRoute = false;
    {
        std::lock_guard<std::mutex> lock(routesMutex_);
        const auto routeIt = routes_.find(frame.destinationModule);
        if (routeIt != routes_.end()) {
            endpoint = routeIt->second;
            hasRoute = true;
        }
    }

    if (!hasRoute) {
        return true;
    }

    return sendUdp(frame, endpoint);
}

bool MessageBus::bindSocket(const std::string& listenIp, std::uint16_t listenPort)
{
    socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        std::fprintf(stderr, "MessageBus: socket creation failed: %s\n", std::strerror(errno));
        return false;
    }

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(listenPort);

    if (::inet_pton(AF_INET, listenIp.c_str(), &address.sin_addr) != 1) {
        std::fprintf(stderr, "MessageBus: invalid listen IP '%s'\n", listenIp.c_str());
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    if (::bind(socketFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "MessageBus: bind failed: %s\n", std::strerror(errno));
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    return true;
}

bool MessageBus::sendUdp(const Frame& frame, const Endpoint& endpoint)
{
    if (socketFd_ < 0) {
        return false;
    }

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);

    if (::inet_pton(AF_INET, endpoint.ip.c_str(), &address.sin_addr) != 1) {
        std::fprintf(stderr, "MessageBus: invalid route IP '%s'\n", endpoint.ip.c_str());
        return false;
    }

    const std::string wireData = frameToWire(frame);
    const auto bytesSent = ::sendto(
        socketFd_,
        wireData.c_str(),
        wireData.size(),
        0,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address));

    return bytesSent >= 0;
}

void MessageBus::receiverLoop()
{
    while (running_) {
        char buffer[8192];
        std::memset(buffer, 0, sizeof(buffer));

        sockaddr_in senderAddress;
        socklen_t senderSize = sizeof(senderAddress);

        const auto bytesRead = ::recvfrom(
            socketFd_,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(&senderAddress),
            &senderSize);

        if (bytesRead <= 0) {
            continue;
        }

        Frame frame;
        if (!wireToFrame(std::string(buffer, static_cast<std::size_t>(bytesRead)), frame)) {
            std::fprintf(stderr, "MessageBus: dropped malformed frame\n");
            continue;
        }

        char senderIpText[INET_ADDRSTRLEN];
        std::memset(senderIpText, 0, sizeof(senderIpText));
        if (::inet_ntop(AF_INET, &senderAddress.sin_addr, senderIpText, sizeof(senderIpText)) != nullptr) {
            frame.senderIp = senderIpText;
        }
        frame.senderPort = ntohs(senderAddress.sin_port);

        dispatchLocal(frame);
    }
}

void MessageBus::dispatchLocal(const Frame& frame)
{
    std::vector<Handler> monitorHandlers;
    std::vector<Handler> destinationHandlers;
    std::vector<Handler> broadcastHandlers;

    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        monitorHandlers = monitors_;

        if (frame.destinationModule == kBroadcastDestination) {
            for (const auto& entry : handlers_) {
                for (const auto& handler : entry.second) {
                    broadcastHandlers.push_back(handler);
                }
            }
        } else {
            const auto destinationIt = handlers_.find(frame.destinationModule);
            if (destinationIt != handlers_.end()) {
                destinationHandlers = destinationIt->second;
            }
        }
    }

    for (const auto& monitorHandler : monitorHandlers) {
        monitorHandler(frame);
    }

    for (const auto& destinationHandler : destinationHandlers) {
        destinationHandler(frame);
    }

    for (const auto& broadcastHandler : broadcastHandlers) {
        broadcastHandler(frame);
    }
}

} // namespace comm
} // namespace opensmt
