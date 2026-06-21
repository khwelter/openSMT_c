cmake_minimum_required(VERSION 3.16)

project(openSMT_c VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(Threads REQUIRED)

file(GLOB_RECURSE OPENSMT_CONFIG_INPUTS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/config/*
)

set(OPENSMT_CONFIG_SYNC_STAMP ${CMAKE_BINARY_DIR}/config/.config_sync.stamp)

add_custom_command(
    OUTPUT ${OPENSMT_CONFIG_SYNC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/config
        ${CMAKE_BINARY_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E touch ${OPENSMT_CONFIG_SYNC_STAMP}
    DEPENDS ${OPENSMT_CONFIG_INPUTS}
)

add_custom_target(sync_runtime_config ALL DEPENDS ${OPENSMT_CONFIG_SYNC_STAMP})

add_library(smt_placer
    src/App.cpp
    src/comm/MessageBus.cpp
    src/config/ConfigLoader.cpp
    src/control/AppControlMessage.cpp
    src/drivers/DeviceDriver.cpp
    src/drivers/GenericDeviceDriver.cpp
    src/hw/CanHardwareDriver.cpp
    src/hw/MarlinSerialProtocolAdapter.cpp
    src/hw/SerialHardwareDriver.cpp
    src/modules/CommunicationMonitorModule.cpp
    src/modules/DeviceDriverRuntimeModule.cpp
    src/modules/PlacerModule.cpp
    src/Placer.cpp
)

target_include_directories(smt_placer
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(smt_placer
    PUBLIC
        nlohmann_json::nlohmann_json
        Threads::Threads
)

add_executable(openSMT_c_app
    src/main.cpp
)

target_link_libraries(openSMT_c_app
    PRIVATE
        smt_placer
)
add_dependencies(openSMT_c_app sync_runtime_config)

add_executable(appterm
    src/appterm_main.cpp
)

target_link_libraries(appterm
    PRIVATE
        smt_placer
)
add_dependencies(appterm sync_runtime_config)

add_executable(appHomeXY
    src/appHomeXY_main.cpp
)

target_link_libraries(appHomeXY
    PRIVATE
        smt_placer
)
add_dependencies(appHomeXY sync_runtime_config)

add_executable(appHomeZ
    src/appHomeZ_main.cpp
)

target_link_libraries(appHomeZ
    PRIVATE
        smt_placer
)
add_dependencies(appHomeZ sync_runtime_config)

add_executable(appMoveXY
    src/appMoveXY_main.cpp
)

target_link_libraries(appMoveXY
    PRIVATE
        smt_placer
)
add_dependencies(appMoveXY sync_runtime_config)

add_executable(appMoveZ
    src/appMoveZ_main.cpp
)

target_link_libraries(appMoveZ
    PRIVATE
        smt_placer
)
add_dependencies(appMoveZ sync_runtime_config)

add_executable(appMoveR
    src/appMoveR_main.cpp
)

target_link_libraries(appMoveR
    PRIVATE
        smt_placer
)
add_dependencies(appMoveR sync_runtime_config)

add_executable(appGetPos
    src/appGetPos_main.cpp
)

target_link_libraries(appGetPos
    PRIVATE
        smt_placer
)
add_dependencies(appGetPos sync_runtime_config)

add_executable(appGetActors
    src/appGetActors_main.cpp
)

target_link_libraries(appGetActors
    PRIVATE
        smt_placer
)
add_dependencies(appGetActors sync_runtime_config)

add_executable(appActor
    src/appActor_main.cpp
)

target_link_libraries(appActor
    PRIVATE
        smt_placer
)
add_dependencies(appActor sync_runtime_config)

add_executable(appGoto
    src/appGoto_main.cpp
)

target_link_libraries(appGoto
    PRIVATE
        smt_placer
)
add_dependencies(appGoto sync_runtime_config)

add_executable(appDisableAll
    src/appDisableAll_main.cpp
)

target_link_libraries(appDisableAll
    PRIVATE
        smt_placer
)
add_dependencies(appDisableAll sync_runtime_config)

add_executable(appRotZ
    src/appRotZ_main.cpp
)

target_link_libraries(appRotZ
    PRIVATE
        smt_placer
)
add_dependencies(appRotZ sync_runtime_config)
