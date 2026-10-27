#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace mchose::hid {
enum class Protocol { Gmouse, Inverted, A5 };
struct Model { std::wstring_view name; Protocol protocol; uint16_t id; };
std::span<const Model> Models();
const Model* FindModel(std::wstring_view product);
const Model* FindModel(uint16_t id);
const char* ProtocolName(Protocol protocol);
struct Descriptor {
    uint16_t vendor=0, product=0, page=0, usage=0, inputLength=0, outputLength=0, featureLength=0;
    bool shortFeature=false, longFeature=false, zeroFeature=false;
};
struct Match { Protocol protocol; const Model* model=nullptr; };
bool MatchDevice(const Descriptor& descriptor,std::wstring_view product,Match& match);
struct Reading { bool valid=false,charging=false; int percent=-1; const Model* model=nullptr; };
Reading Parse(Protocol protocol,std::span<const uint8_t> response,const Model* model=nullptr);
enum class Operation { Write, Read, SetFeature, GetFeature };
// The Windows adapter supplies bounded overlapped I/O and cancellable delays.
class Transport {
public:
    virtual ~Transport()=default;
    virtual bool Transfer(Operation operation,std::span<uint8_t> bytes,size_t& count,unsigned timeoutMs)=0;
    virtual bool Pause(unsigned milliseconds)=0;
    virtual bool Cancelled() const=0;
};
Reading Read(Transport& transport,const Match& match,const Descriptor& descriptor,
             std::span<uint8_t> response,size_t& responseSize);
}
