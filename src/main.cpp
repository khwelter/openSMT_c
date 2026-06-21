#include <cstdio>
#include <string>

#include "openSMT/App.hpp"

int main(int argc, char** argv)
{
    std::string configPath = "config/config.json";
    if (argc > 1 && argv[1] != nullptr) {
        configPath = argv[1];
    }

    opensmt::App app;
    if (!app.run(configPath)) {
        std::fprintf(stderr, "Application failed to start\n");
        return 1;
    }

    return 0;
}
