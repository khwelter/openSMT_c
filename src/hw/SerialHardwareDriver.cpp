#include "openSMT/hw/SerialHardwareDriver.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "openSMT/hw/MarlinSerialProtocolAdapter.hpp"

namespace opensmt {
namespace hw {

SerialHardwareDriver::SerialHardwareDriver(
    std::string hardwareId,
    std::string portName,
    int baudRate,
    int dataBits,
    std::string parity,
        int stopBits,
        std::string protocol,
        int defaultRotateSpeed,
        int serialResponseTimeoutMs)
    : hardwareId_(std::move(hardwareId)),
      portName_(std::move(portName)),
      baudRate_(baudRate),
      dataBits_(dataBits),
      parity_(std::move(parity)),
      stopBits_(stopBits),
            protocol_(std::move(protocol)),
            defaultRotateSpeed_(defaultRotateSpeed),
            serialResponseTimeoutMs_(serialResponseTimeoutMs),
            protocolAdapter_(nullptr),
            fileDescriptor_(-1),
            devicePath_(),
            readBuffer_(),
      started_(false)
{
}

const std::string& SerialHardwareDriver::id() const
{
    return hardwareId_;
}

const std::string& SerialHardwareDriver::type() const
{
    static const std::string kType = "serial";
    return kType;
}

bool SerialHardwareDriver::start()
{
    if (started_) {
        return true;
    }

    if (!createProtocolAdapter()) {
        std::fprintf(stderr, "[hw][%s] unknown serial protocol '%s'\n", hardwareId_.c_str(), protocol_.c_str());
        return false;
    }

    if (!openPort()) {
        std::fprintf(stderr, "[hw][%s] failed to open serial port '%s'\n", hardwareId_.c_str(), portName_.c_str());
        return false;
    }

    started_ = true;
    std::fprintf(
        stderr,
        "[hw][%s] serial start port=%s device=%s baud=%d dataBits=%d parity=%s stopBits=%d protocol=%s timeoutMs=%d\n",
        hardwareId_.c_str(),
        portName_.c_str(),
        devicePath_.c_str(),
        baudRate_,
        dataBits_,
        parity_.c_str(),
        stopBits_,
        protocolAdapter_->id().c_str(),
        serialResponseTimeoutMs_);
    return true;
}

void SerialHardwareDriver::stop()
{
    if (!started_) {
        return;
    }

    closePort();
    started_ = false;
    std::fprintf(stderr, "[hw][%s] serial stop\n", hardwareId_.c_str());
}

HardwareResponse SerialHardwareDriver::execute(const HardwareCommand& command)
{
    if (!started_) {
        return {false, "{}", "hardware not started"};
    }

    if (!protocolAdapter_) {
        return {false, "{}", "serial protocol adapter not initialized"};
    }

    if (fileDescriptor_ < 0) {
        return {false, "{}", "serial port is not open"};
    }

    std::lock_guard<std::mutex> lock(ioMutex_);

    const SerialCommandBuildResult buildResult =
        protocolAdapter_->buildCommand(command.deviceDriverId, command.action, command.payloadJson);
    if (!buildResult.ok) {
        return {false, "{}", buildResult.error};
    }

    std::fprintf(
        stderr,
        "[hw][%s] %s action=%s payload=%s\n",
        hardwareId_.c_str(),
        command.deviceDriverId.c_str(),
        command.action.c_str(),
        command.payloadJson.c_str());

    std::fprintf(stderr, "[hw][%s][tx] %s\n", hardwareId_.c_str(), buildResult.commandText.c_str());

    std::string ioError;
    if (!writeLine(buildResult.commandText, ioError)) {
        return {false, "{}", ioError};
    }

    HardwareResponse response = waitForMarlinResponse(command.action);
    if (!response.ok) {
        return response;
    }

    nlohmann::json resultJson;
    resultJson["hardwareId"] = hardwareId_;
    resultJson["portName"] = portName_;
    resultJson["devicePath"] = devicePath_;
    resultJson["action"] = command.action;
    resultJson["protocol"] = protocolAdapter_->id();
    resultJson["serialCommand"] = buildResult.commandText;
    resultJson["deviceResponse"] = nlohmann::json::parse(response.payloadJson);
    resultJson["accepted"] = true;

    return {true, resultJson.dump(), ""};
}

bool SerialHardwareDriver::createProtocolAdapter()
{
    if (protocol_ == "marlin-gcode") {
        protocolAdapter_ = std::make_unique<MarlinSerialProtocolAdapter>(defaultRotateSpeed_);
        return true;
    }

    protocolAdapter_.reset();
    return false;
}

bool SerialHardwareDriver::openPort()
{
    devicePath_ = resolveDevicePath();
    fileDescriptor_ = ::open(devicePath_.c_str(), O_RDWR | O_NOCTTY);
    if (fileDescriptor_ < 0) {
        std::fprintf(stderr, "[hw][%s] open failed: %s\n", hardwareId_.c_str(), std::strerror(errno));
        return false;
    }

    termios options;
    std::memset(&options, 0, sizeof(options));
    if (::tcgetattr(fileDescriptor_, &options) != 0) {
        std::fprintf(stderr, "[hw][%s] tcgetattr failed: %s\n", hardwareId_.c_str(), std::strerror(errno));
        closePort();
        return false;
    }

    const speed_t speed = mapBaudRate(baudRate_);
    if (speed == 0) {
        std::fprintf(stderr, "[hw][%s] unsupported baud rate: %d\n", hardwareId_.c_str(), baudRate_);
        closePort();
        return false;
    }

    ::cfsetispeed(&options, speed);
    ::cfsetospeed(&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CSIZE;
    if (dataBits_ == 7) {
        options.c_cflag |= CS7;
    } else {
        options.c_cflag |= CS8;
    }

    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~PARODD;
    if (parity_ == "E") {
        options.c_cflag |= PARENB;
    } else if (parity_ == "O") {
        options.c_cflag |= PARENB;
        options.c_cflag |= PARODD;
    }

    if (stopBits_ == 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }

    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    options.c_oflag &= ~OPOST;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (::tcsetattr(fileDescriptor_, TCSANOW, &options) != 0) {
        std::fprintf(stderr, "[hw][%s] tcsetattr failed: %s\n", hardwareId_.c_str(), std::strerror(errno));
        closePort();
        return false;
    }

    ::tcflush(fileDescriptor_, TCIOFLUSH);
    return true;
}

void SerialHardwareDriver::closePort()
{
    if (fileDescriptor_ >= 0) {
        ::close(fileDescriptor_);
        fileDescriptor_ = -1;
    }
    devicePath_.clear();
    readBuffer_.clear();
}

bool SerialHardwareDriver::writeLine(const std::string& line, std::string& outError)
{
    std::string wire = line;
    wire.push_back('\n');

    const char* data = wire.c_str();
    std::size_t remaining = wire.size();

    while (remaining > 0) {
        const ssize_t bytesWritten = ::write(fileDescriptor_, data, remaining);
        if (bytesWritten < 0) {
            if (errno == EINTR) {
                continue;
            }
            outError = "serial write failed: " + std::string(std::strerror(errno));
            return false;
        }

        if (bytesWritten == 0) {
            outError = "serial write returned zero bytes";
            return false;
        }

        remaining -= static_cast<std::size_t>(bytesWritten);
        data += bytesWritten;
    }

    if (::tcdrain(fileDescriptor_) != 0) {
        outError = "serial drain failed: " + std::string(std::strerror(errno));
        return false;
    }

    return true;
}

bool SerialHardwareDriver::readLineUntil(std::string& outLine, std::string& outError)
{
    outLine.clear();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(serialResponseTimeoutMs_);

    while (std::chrono::steady_clock::now() < deadline) {
        const std::size_t newlinePos = readBuffer_.find('\n');
        if (newlinePos != std::string::npos) {
            outLine = readBuffer_.substr(0, newlinePos);
            readBuffer_.erase(0, newlinePos + 1);
            while (!outLine.empty() && (outLine.back() == '\r' || outLine.back() == '\n')) {
                outLine.pop_back();
            }
            return true;
        }

        char chunk[256];
        const ssize_t bytesRead = ::read(fileDescriptor_, chunk, sizeof(chunk));
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            outError = "serial read failed: " + std::string(std::strerror(errno));
            return false;
        }

        if (bytesRead == 0) {
            continue;
        }

        readBuffer_.append(chunk, chunk + bytesRead);
    }

    outError = "serial response timeout";
    return false;
}

HardwareResponse SerialHardwareDriver::waitForMarlinResponse(const std::string& action)
{
    nlohmann::json responseJson;
    responseJson["action"] = action;
    responseJson["lines"] = nlohmann::json::array();

    while (true) {
        std::string line;
        std::string readError;
        if (!readLineUntil(line, readError)) {
            return {false, responseJson.dump(), readError};
        }

        if (line.empty()) {
            continue;
        }

        responseJson["lines"].push_back(line);
        std::fprintf(stderr, "[hw][%s][rx] %s\n", hardwareId_.c_str(), line.c_str());

        if (line.rfind("ok", 0) == 0 || line == "ok") {
            responseJson["status"] = "ok";
            return {true, responseJson.dump(), ""};
        }

        if (line.rfind("Error:", 0) == 0 || line.rfind("error:", 0) == 0) {
            responseJson["status"] = "error";
            return {false, responseJson.dump(), line};
        }

        if (line.rfind("busy:", 0) == 0 ||
            line.rfind("echo:", 0) == 0 ||
            line == "start") {
            continue;
        }
    }
}

speed_t SerialHardwareDriver::mapBaudRate(int baudRate) const
{
    switch (baudRate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return static_cast<speed_t>(0);
    }
}

std::string SerialHardwareDriver::resolveDevicePath() const
{
    if (portName_.empty()) {
        return portName_;
    }

    if (portName_.front() == '/') {
        return portName_;
    }

    return "/dev/" + portName_;
}

} // namespace hw
} // namespace opensmt
