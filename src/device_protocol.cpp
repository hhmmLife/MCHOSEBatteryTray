#include "device_protocol.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace g7::hid {
static constexpr Model models[]={
    {L"MCHOSE G7",Protocol::Gmouse,0}, {L"MCHOSE G7 Pro",Protocol::Gmouse,0},
    {L"MCHOSE G3 A",Protocol::Gmouse,0}, {L"MCHOSE G3 V2",Protocol::Gmouse,0},
    {L"MCHOSE G3 V2 Pro",Protocol::Gmouse,0},
    {L"MCHOSE A7e",Protocol::Gmouse,0}, {L"MCHOSE A7e Pro",Protocol::Gmouse,0},
    {L"MCHOSE M7",Protocol::Inverted,0x0020}, {L"MCHOSE M7 Pro",Protocol::Inverted,0x0030},
    {L"MCHOSE M7 Ultra",Protocol::Inverted,0x0031},
    {L"MCHOSE L7",Protocol::Inverted,0x00c0}, {L"MCHOSE L7 Pro",Protocol::Inverted,0x00b0},
    {L"MCHOSE L7 Pro+",Protocol::Inverted,0x4015}, {L"MCHOSE L7 Ultra",Protocol::Inverted,0x00b1},
    {L"MCHOSE L7 Ultra+",Protocol::Inverted,0x4016},
    {L"MCHOSE A7",Protocol::Inverted,0x0070}, {L"MCHOSE A7 Pro",Protocol::Inverted,0x0010},
    {L"MCHOSE A7 Ultra",Protocol::Inverted,0x0011}, {L"MCHOSE A7 Ultra(RE)",Protocol::Inverted,0x4110},
    {L"MCHOSE A7 V2 Pro",Protocol::Inverted,0x4018}, {L"MCHOSE A7 V2 Pro+",Protocol::Inverted,0x4023},
    {L"MCHOSE A7 V2 Ultra",Protocol::Inverted,0x4019}, {L"MCHOSE A7 V2 Ultra+",Protocol::Inverted,0x4021},
    {L"MCHOSE A7X Ultra",Protocol::Inverted,0x4011}, {L"MCHOSE K7 Ultra",Protocol::Inverted,0x4150},
    {L"MCHOSE AX5 V2",Protocol::Inverted,0x4010},
    {L"MCHOSE A5 Pro Max",Protocol::A5,0}
};
std::span<const Model> Models(){return models;}
static std::wstring Normalize(std::wstring_view value){
    std::wstring text(value);
    for(auto& ch:text)if(ch>=L'a'&&ch<=L'z')ch-=L'a'-L'A';
    for(auto prefix:{L"REALTEK ",L"YJX-CHIP ",L"MCHOSE "})
        if(text.starts_with(prefix))text.erase(0,std::wstring_view(prefix).size());
    while(!text.empty()&&text.back()==L' ')text.pop_back();
    if(text==L"M7 1K")text=L"M7";
    return text;
}
const Model* FindModel(std::wstring_view product){
    auto name=Normalize(product);
    for(const auto& model:models)if(Normalize(model.name)==name)return &model;
    return nullptr;
}
const Model* FindModel(uint16_t id){
    if(id==0x00a1)id=0x4010; // AX5 V2's older wireless model identifier in the official link table.
    for(const auto& model:models)if(model.protocol==Protocol::Inverted&&model.id==id)return &model;
    return nullptr;
}
const char* ProtocolName(Protocol protocol){
    switch(protocol){case Protocol::Gmouse:return "gmouse-output";case Protocol::Inverted:return "inverted-feature";case Protocol::A5:return "a5-feature";}
    return "unknown";
}
bool MatchDevice(const Descriptor& d,std::wstring_view product,Match& match){
    const Model* model=FindModel(product);
    if(d.page==0xff01&&d.usage==0x10&&d.inputLength==65&&d.outputLength==65&&
       ((d.vendor==0xa8a5&&d.product==0x2255)||(d.vendor==0x3837&&(d.product==0x4245||d.product==0x1008)))){
        if(model&&model->protocol!=Protocol::Gmouse)return false;
        // Shared receiver IDs cannot identify a variant; retain a generic label if its string is unknown.
        match={Protocol::Gmouse,model};return true;
    }
    if(d.vendor==0x2023&&(d.product==0xf013||d.product==0xf015)&&d.page==0xffff&&
       (d.usage==0||d.usage==1)&&d.zeroFeature&&d.featureLength==65){
        match={Protocol::A5,FindModel(L"MCHOSE A5 Pro Max")};return true;
    }
    // Receiver-only IDs from the official model/link table. Wired model IDs are not probed.
    constexpr uint16_t receivers[]={0x1020,0x1021,0x1009,0x100a,0x100b,0x1001};
    if((d.vendor==0x5253||d.vendor==0x3837)&&
       std::find(std::begin(receivers),std::end(receivers),d.product)!=std::end(receivers)&&
       d.page==0xff01&&d.usage==1&&d.featureLength<=65&&
       ((d.shortFeature&&d.featureLength>=21)||(d.longFeature&&d.featureLength==65))){
        if(model&&model->protocol!=Protocol::Inverted)return false;
        match={Protocol::Inverted,model};return true;
    }
    return false;
}
Reading Parse(Protocol protocol,std::span<const uint8_t> r,const Model* model){
    Reading result;
    if(protocol==Protocol::Gmouse){
        if(r.size()<11||r[0]!=0||r[1]!=0xaa||r[2]!=0x30||r[9]>100||r[10]>1)return result;
        result={true,r[10]!=0,r[9],model};
    }else if(protocol==Protocol::Inverted){
        if(r.size()<13||(r[0]!=0x11&&r[0]!=0x12)||r[1]!=0xf9)return result;
        auto byte=[&](size_t index){return uint8_t(r[index]^0xff);};
        uint16_t vendor=uint16_t(byte(2)|(byte(3)<<8)),id=uint16_t(byte(4)|(byte(5)<<8));
        auto actual=FindModel(id);
        // Mode 1 is the radio link; bit 3 confirms the mouse is connected. All-zero sleep replies fail here.
        if((vendor!=0x5253&&vendor!=0x3837)||!actual||(model&&model!=actual)||
           (byte(10)&7)!=1||!(byte(10)&8)||byte(11)>100||byte(12)>1)return result;
        result={true,byte(12)!=0,byte(11),actual};
    }else{
        // Windows feature buffers include report ID 0 before the A1 payload marker.
        if(r.size()<9||r[0]!=0||r[1]!=0xa1||r[5]!=0||r[6]!=0x83||r[8]>100)return result;
        result={true,r[7]!=0,r[8],model};
    }
    return result;
}
Reading Read(Transport& t,const Match& match,const Descriptor& d,std::span<uint8_t> response,size_t& size){
    size=0;Reading result;std::array<uint8_t,65> command{};
    if(t.Cancelled()||response.size()<65||d.featureLength>65)return result;
    auto transfer=[&](Operation op,std::span<uint8_t> data,unsigned timeout){
        size_t count=0;
        if(t.Cancelled()||!t.Transfer(op,data,count,timeout))return false;
        if(op==Operation::Read||op==Operation::GetFeature){size=std::min(count,response.size());}
        else if(op==Operation::Write&&count!=data.size())return false;
        return true;
    };
    if(match.protocol==Protocol::Gmouse){
        constexpr uint8_t prefix[]={0,0x55,0x30,0xa5,0x0b,0x2e,1,1,1};
        std::copy(std::begin(prefix),std::end(prefix),command.begin());
        if(!transfer(Operation::Write,command,1500))return result;
        for(int attempt=0;attempt<16&&!t.Cancelled();++attempt){
            std::fill(response.begin(),response.end(),0);
            if(!transfer(Operation::Read,response.first(65),2000))break;
            result=Parse(match.protocol,response.first(size),match.model);if(result.valid)return result;
        }
    }else if(match.protocol==Protocol::Inverted){
        for(uint8_t id:{uint8_t(0x11),uint8_t(0x12)}){
            if((id==0x11&&!d.shortFeature)||(id==0x12&&!d.longFeature))continue;
            for(int attempt=0;attempt<4&&!t.Cancelled();++attempt){
                std::fill(command.begin(),command.end(),0xff);command[0]=id;command[1]=0xf9;
                if(!transfer(Operation::SetFeature,std::span(command).first(d.featureLength),750))break;
                if(!t.Pause(80))return result;
                std::fill(response.begin(),response.end(),0);response[0]=id;
                if(!transfer(Operation::GetFeature,response.first(d.featureLength),750))break;
                result=Parse(match.protocol,response.first(size),match.model);if(result.valid)return result;
            }
        }
    }else{
        command[3]=2;command[4]=2;command[6]=0x83;
        for(int attempt=0;attempt<8&&!t.Cancelled();++attempt){
            if(attempt==0||attempt==3){
                if(!transfer(Operation::SetFeature,std::span(command).first(d.featureLength),750))break;
                if(!t.Pause(34))return result;
            }else if(!t.Pause(30))return result;
            std::fill(response.begin(),response.end(),0);
            if(!transfer(Operation::GetFeature,response.first(d.featureLength),750))break;
            result=Parse(match.protocol,response.first(size),match.model);if(result.valid)return result;
        }
    }
    return result;
}
}
