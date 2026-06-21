#pragma once

#include <termios.h>

#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include "openSMT/hw/ISerialProtocolAdapter.hpp"
#include "openSMT/hw/IHardwareDriver.hpp"

namespace opensmt {
namespace hw {

class SerialHardwareDriver : public IHardwareDriver {
public:
    SerialHardwareDriver(
        std::string hardwareId,
        std::string portName,
        int baudRate,
        int dataBits,
        std::string parity,
        int stopBits,
        std::string protocol,
        int defaultRotateSpeed,
        int serialResponseTimeoutMs);

    const std::string& id() const override;
    const std::string& type() const override;

    bool start() override;
    void stop() override;

    HardwareResponse execute(const HardwareCommand& command) override;

private:
    bool createProtocolAdapter();
    bool openPort();
    void closePort();

    bool writeLine(const std::string& line, std::string& outError);
    bool readLineUntil(std::string& outLine, std::string& outError);
    HardwareResponse waitForMarlinResponse(const std::string& action);
    std::string resolveDevicePath() const;

    speed_t mapBaudRate(int baudRate) const;

    std::string hardwareId_;
    std::string portName_;
    int baudRate_;
    int dataBits_;
    std::string parity_;
    int stopBits_;
    std::string protocol_;
    int defaultRotateSpeed_;
    int serialResponseTimeoutMs_;
    std::unique_ptr<ISerialProtocolAdapter> protocolAdapter_;
    int fileDescriptor_;
    std::string devicePath_;
    std::mutex ioMutex_;
    std::string readBuffer_;
    bool started_;
};

} // namespace hw
} // namespace opensmt
