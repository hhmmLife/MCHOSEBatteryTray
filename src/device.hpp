#pragma once
#include "common.hpp"

namespace g7 {
struct BatteryResult {
    bool valid=false, receiverPresent=false, charging=false;
    int percent=-1;
    DWORD error=0, elapsedMs=0;
    std::wstring message, devicePath;
    std::string response;
    LocalTime sampled=Now();
};
class DeviceReader {
    std::wstring cachedPath_;
    std::wstring Discover();
public:
    BatteryResult Query(HANDLE stopEvent);
};
}
