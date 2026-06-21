#pragma once

#include <string>

#include "openSMT/hw/ISerialProtocolAdapter.hpp"

namespace opensmt {
namespace hw {

class MarlinSerialProtocolAdapter : public ISerialProtocolAdapter {
public:
    explicit MarlinSerialProtocolAdapter(int defaultRotateSpeed);

    const std::string& id() const override;
    SerialCommandBuildResult buildCommand(
        const std::string& deviceDriverId,
        const std::string& action,
        const std::string& payloadJson) const override;

private:
    int defaultRotateSpeed_;
};

} // namespace hw
} // namespace opensmt
