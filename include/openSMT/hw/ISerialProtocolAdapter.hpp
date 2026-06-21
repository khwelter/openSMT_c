#pragma once

#include <string>

namespace opensmt {
namespace hw {

struct SerialCommandBuildResult {
    bool ok;
    std::string commandText;
    std::string error;
};

class ISerialProtocolAdapter {
public:
    virtual ~ISerialProtocolAdapter() = default;

    virtual const std::string& id() const = 0;
    virtual SerialCommandBuildResult buildCommand(
        const std::string& deviceDriverId,
        const std::string& action,
        const std::string& payloadJson) const = 0;
};

} // namespace hw
} // namespace opensmt
