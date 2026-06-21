#pragma once

#include <string>

#include "openSMT/config/AppConfig.hpp"

namespace opensmt {
namespace config {

class ConfigLoader {
public:
    bool load(const std::string& rootConfigPath, AppConfig& outConfig, std::string& outError) const;
};

} // namespace config
} // namespace opensmt
