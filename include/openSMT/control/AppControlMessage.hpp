#pragma once

#include <string>
#include <utility>
#include <vector>

namespace opensmt {
namespace control {

constexpr const char* kAppMainModuleName = "appmain";
constexpr const char* kAppTermModuleName = "appterm";
constexpr const char* kAppStopRequestPayloadType = "app-stop-request";
constexpr const char* kAppPositionQueryPayloadType = "app-position-query";
constexpr const char* kAppPositionReplyPayloadType = "app-position-reply";

enum class AppControlCommand {
    Stop,
    GetPositions
};

struct AppAxisPositions {
    float x;
    float y;
    float z1;
    float z2;
    float z3;
    float z4;
    float r1;
    float r2;
    float r3;
    float r4;
};

struct AppPosition3 {
    float x;
    float y;
    float z;
};

struct AppReferencePositions {
    AppPosition3 posCalib;
    AppPosition3 posCalibSec;
    AppPosition3 posPark;
    AppPosition3 posCamBot;
    AppPosition3 posDiscard;
    AppPosition3 posChange;
};

struct AppPositionReplyMessage {
    AppAxisPositions axis;
    AppReferencePositions reference;
    std::vector<std::pair<std::string, int>> actors;
};

struct AppControlMessage {
    AppControlCommand command;
    std::string reason;
};

std::string toCommandString(AppControlCommand command);
bool commandFromString(const std::string& value, AppControlCommand& outCommand);
std::string serializePayloadJson(const AppControlMessage& message);
bool parsePayloadJson(const std::string& payloadJsonText, AppControlMessage& outMessage);
std::string serializePositionReplyPayloadJson(const AppPositionReplyMessage& message);
bool parsePositionReplyPayloadJson(const std::string& payloadJsonText, AppPositionReplyMessage& outMessage);

} // namespace control
} // namespace opensmt