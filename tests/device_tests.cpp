#include "../src/device_protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace g7::hid;
static int checks=0;
static void Check(bool value,const char* name){++checks;if(!value){std::cerr<<"FAIL: "<<name<<'\n';std::exit(1);}}
static std::vector<uint8_t> Inverted(int percent=82,int charge=0,uint16_t model=0x0031,uint8_t report=0x11){
    std::vector<uint8_t> r(65,0xff);r[0]=report;r[1]=0xf9;
    const uint8_t decoded[]={0x53,0x52,uint8_t(model),uint8_t(model>>8),2,5,7,0,9,uint8_t(percent),uint8_t(charge)};
    for(size_t i=0;i<std::size(decoded);++i)r[i+2]=decoded[i]^0xff;
    return r;
}
static std::vector<uint8_t> Gmouse(int percent=82,int charge=0){
    std::vector<uint8_t> r(65);const uint8_t data[]={0,0xaa,0x30,0xa5,0x0b,0x0a,1,1,1,uint8_t(percent),uint8_t(charge)};
    std::copy(std::begin(data),std::end(data),r.begin());return r;
}
static std::vector<uint8_t> A5(int percent=22,int charge=0){
    std::vector<uint8_t> r(65);r[1]=0xa1;r[6]=0x83;r[7]=uint8_t(charge);r[8]=uint8_t(percent);return r;
}
struct Fake:Transport {
    std::vector<std::vector<uint8_t>> replies,sent;
    std::vector<unsigned> pauses;
    int reads=0,writes=0;bool stopped=false,cancelOnPause=false,failShort=false,partialWrite=false,failRead=false;
    bool Transfer(Operation op,std::span<uint8_t> bytes,size_t& count,unsigned timeout) override{
        Check(timeout>0&&timeout<=2000,"I/O timeout bounded");
        count=bytes.size();
        if(op==Operation::SetFeature||op==Operation::Write){
            ++writes;sent.emplace_back(bytes.begin(),bytes.end());
            if(failShort&&bytes[0]==0x11)return false;
            if(partialWrite)--count;
            return true;
        }
        ++reads;if(failRead)return false;
        std::vector<uint8_t> r=reads<=int(replies.size())?replies[size_t(reads-1)]:std::vector<uint8_t>(bytes.size());
        count=std::min(bytes.size(),r.size());std::copy_n(r.begin(),count,bytes.begin());return true;
    }
    bool Pause(unsigned ms) override{pauses.push_back(ms);if(cancelOnPause)stopped=true;return !stopped;}
    bool Cancelled() const override{return stopped;}
};
int main(){
    Descriptor g{0xa8a5,0x2255,0xff01,0x10,65,65,0};
    Descriptor feature{0x5253,0x1020,0xff01,1,0,0,65,true,true,false};
    Descriptor a5{0x2023,0xf013,0xffff,0,0,0,65,false,false,true};Match match{};
    Check(Models().size()==27,"27 named variants in catalog");
    for(const auto& model:Models()){
        Check(FindModel(model.name)==&model,"catalog names unique and round trip");
        auto d=model.protocol==Protocol::Gmouse?g:model.protocol==Protocol::A5?a5:feature;
        Check(MatchDevice(d,model.name,match)&&match.model==&model,"every named variant matches its protocol");
        if(model.protocol==Protocol::Inverted)Check(FindModel(model.id)==&model,"wireless model identifiers unique");
    }
    Check(FindModel(L"RealTek MCHOSE M7 Ultra")==FindModel(0x31),"RealTek manufacturer prefix");
    Check(FindModel(L"YJX-CHIP MCHOSE g7")==FindModel(L"G7"),"G7 case and manufacturer alias");
    Check(FindModel(L"MCHOSE M7 1K")==FindModel(0x20),"M7 1K alias");
    Check(FindModel(0xa1)==FindModel(L"AX5 V2"),"AX5 V2 older wireless model identifier");
    Check(!FindModel(L"G7 Max")&&!FindModel(L"G3 Ultra"),"other protocol families not misidentified");
    for(auto d:{g,feature,a5}){
        d.page=1;Check(!MatchDevice(d,L"",match),"mouse and keyboard input collections rejected");
        d.page=0xff0b;Check(!MatchDevice(d,L"",match),"non-answering sibling collection rejected");
    }
    auto d=g;d.vendor=0xa8a4;Check(!MatchDevice(d,L"G7",match),"G wired VID excluded");
    d=g;d.product=0x2256;Check(!MatchDevice(d,L"G7",match),"unknown G receiver excluded");
    d=g;d.usage=1;Check(!MatchDevice(d,L"G7",match),"G collection usage checked");
    d=g;d.outputLength=64;Check(!MatchDevice(d,L"G7",match),"G report length checked");
    d=g;d.vendor=0x3837;d.product=0x4245;Check(MatchDevice(d,L"G3 V2",match),"G3 V2 receiver");
    d.product=0x1008;Check(MatchDevice(d,L"A7e Pro",match),"A7e receiver");
    d.product=0x4042;Check(!MatchDevice(d,L"G3 V2 Pro",match),"G3 wired product excluded");
    d=feature;d.vendor=0x3837;d.product=0x100b;Check(MatchDevice(d,L"A7 V2 Ultra",match),"new RealTek receiver VID");
    d.product=0x0031;Check(!MatchDevice(d,L"M7 Ultra",match),"wired model PID excluded");
    d=feature;d.shortFeature=false;d.longFeature=false;Check(!MatchDevice(d,L"M7 Ultra",match),"feature IDs required");
    d=feature;d.featureLength=66;Check(!MatchDevice(d,L"M7 Ultra",match),"oversized feature descriptor rejected");
    d=feature;d.featureLength=20;Check(!MatchDevice(d,L"M7 Ultra",match),"shorter-than-command feature descriptor rejected");
    d=a5;d.product=0xf015;Check(MatchDevice(d,L"MCHOSE A5 2.4G",match),"A5 4K receiver");
    d.product=0xf019;Check(!MatchDevice(d,L"",match),"A5 cable excluded");
    Check(!MatchDevice(g,L"M7",match),"name and transport family cannot disagree");
    for(int level=0;level<=100;++level){
        for(auto protocol:{Protocol::Gmouse,Protocol::Inverted,Protocol::A5}){
            auto r=protocol==Protocol::Gmouse?Gmouse(level):protocol==Protocol::Inverted?Inverted(level):A5(level);
            auto parsed=Parse(protocol,r);Check(parsed.valid&&parsed.percent==level,"raw 0..100 preserved without rounding");
        }
    }
    for(auto protocol:{Protocol::Gmouse,Protocol::Inverted,Protocol::A5}){
        auto r=protocol==Protocol::Gmouse?Gmouse(255):protocol==Protocol::Inverted?Inverted(255):A5(255);
        Check(!Parse(protocol,r).valid,"out-of-range battery not clamped to a plausible value");
        for(size_t length=0;length<9;++length)Check(!Parse(protocol,std::span(r).first(length)).valid,"truncated report rejected");
        r.assign(65,0);Check(!Parse(protocol,r).valid,"silent sleep reply not a zero-percent battery");
    }
    auto r=Gmouse(82,1);r[4]=0x3c;Check(Parse(Protocol::Gmouse,r).charging,"G charging reply header variant");
    r=Gmouse(82,2);Check(!Parse(Protocol::Gmouse,r).valid,"malformed G charging flag");
    r=Inverted(100);auto captured=Parse(Protocol::Inverted,r);
    Check(captured.valid&&captured.percent==100&&captured.model==FindModel(0x31),"published M7 status capture");
    r[2]=0;Check(!Parse(Protocol::Inverted,r).valid,"unknown response vendor rejected");
    r=Inverted(80,0,0xffff);Check(!Parse(Protocol::Inverted,r).valid,"unknown decoded model rejected");
    r=Inverted();r[10]=0xff;Check(!Parse(Protocol::Inverted,r).valid,"wired or disconnected flags rejected");
    r[10]=0xfe;Check(!Parse(Protocol::Inverted,r).valid,"radio mode without connected flag rejected");
    r=Inverted(80,3);Check(!Parse(Protocol::Inverted,r).valid,"malformed RealTek charging flag");
    Check(!Parse(Protocol::Inverted,Inverted(),FindModel(0xb0)).valid,"descriptor name and decoded model disagreement rejected");
    r=A5();r[6]=0x82;Check(!Parse(Protocol::A5,r).valid,"A5 echoed command required");
    r=A5();r[5]=1;Check(!Parse(Protocol::A5,r).valid,"A5 echoed page required");
    std::array<uint8_t,65> response{};size_t size=0;
    Fake io;io.replies={std::vector<uint8_t>(65),Gmouse()};
    auto value=Read(io,{Protocol::Gmouse,FindModel(L"G7")},g,response,size);
    Check(value.valid&&io.reads==2&&io.writes==1,"G unrelated input skipped without re-sending");
    Check(io.sent[0]==std::vector<uint8_t>({0,0x55,0x30,0xa5,0x0b,0x2e,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}),"G exact existing read-only request");
    io=Fake{};io.partialWrite=true;Read(io,{Protocol::Gmouse},g,response,size);Check(io.reads==0,"short write aborts query");
    io=Fake{};Read(io,{Protocol::Gmouse},g,response,size);Check(io.reads==16,"stale G input loop bounded");
    io=Fake{};io.failRead=true;Read(io,{Protocol::Gmouse},g,response,size);Check(io.reads==1,"read timeout ends G query");
    io=Fake{};io.replies={std::vector<uint8_t>(65,0xff),Inverted()};
    value=Read(io,{Protocol::Inverted},feature,response,size);
    Check(value.valid&&io.writes==2&&io.reads==2,"RealTek echo re-asked for each attempt");
    Check(io.sent[0][0]==0x11&&io.sent[0][1]==0xf9&&std::all_of(io.sent[0].begin()+2,io.sent[0].end(),[](uint8_t byte){return byte==0xff;}),"RealTek command and all padding inverted");
    io=Fake{};io.failShort=true;io.replies={Inverted(25,1,0x31,0x12)};
    value=Read(io,{Protocol::Inverted},feature,response,size);
    Check(value.valid&&value.charging&&io.writes==2,"short feature failure falls back to advertised long report");
    io=Fake{};Read(io,{Protocol::Inverted},feature,response,size);Check(io.writes==8&&io.reads==8,"sleeping RealTek retries bounded");
    io=Fake{};io.cancelOnPause=true;Read(io,{Protocol::Inverted},feature,response,size);Check(io.writes==1&&io.reads==0,"cancellation during delay prevents more USB calls");
    io=Fake{};io.stopped=true;Read(io,{Protocol::Gmouse},g,response,size);Check(io.writes==0&&io.reads==0,"pre-cancelled query performs no I/O");
    io=Fake{};io.replies={A5(22,1)};value=Read(io,{Protocol::A5,FindModel(L"A5 Pro Max")},a5,response,size);
    Check(value.valid&&value.percent==22&&value.charging,"A5 battery transaction");
    Check(io.sent[0][0]==0&&io.sent[0][3]==2&&io.sent[0][4]==2&&io.sent[0][5]==0&&io.sent[0][6]==0x83,"A5 read-only page/route/command");
    io=Fake{};Read(io,{Protocol::A5},a5,response,size);Check(io.writes==2&&io.reads==8,"A5 bounded reads and one re-send");
    io=Fake{};Read(io,{Protocol::Gmouse},g,std::span(response).first(10),size);Check(io.writes==0,"undersized caller buffer rejected before I/O");
    std::cout<<"PASS: "<<checks<<" device selection, protocol and transport checks\n";
}
