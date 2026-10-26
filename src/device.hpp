#pragma once
#include "common.hpp"
#include "device_protocol.hpp"
#include <optional>
#include <vector>

namespace g7 {
struct BatteryResult {
    bool valid=false, receiverPresent=false, charging=false;
    int percent=-1;
    DWORD error=0, elapsedMs=0;
    std::wstring message, devicePath;
    std::wstring modelName;
    std::string protocol;
    uint16_t vendorId=0,productId=0;
    std::string response;
    LocalTime sampled=Now();
};
class DeviceReader {
    struct Candidate { std::wstring path; hid::Descriptor descriptor; hid::Match match; };
    std::optional<Candidate> cached_;
    std::vector<Candidate> Discover();
    BatteryResult QueryCandidate(const Candidate& candidate,HANDLE stop,ULONGLONG deadline);
public:
    BatteryResult Query(HANDLE stopEvent);
};
}
