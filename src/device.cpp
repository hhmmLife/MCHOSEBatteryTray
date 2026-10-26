#include "device.hpp"
#include <hidsdi.h>
#include <hidpi.h>
#include <hidclass.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <cwctype>

namespace g7 {
static void FeatureIds(PHIDP_PREPARSED_DATA data,const HIDP_CAPS& caps,hid::Descriptor& descriptor){
    auto add=[&](UCHAR id){descriptor.shortFeature|=id==0x11;descriptor.longFeature|=id==0x12;descriptor.zeroFeature|=id==0;};
    USHORT count=caps.NumberFeatureValueCaps;
    std::vector<HIDP_VALUE_CAPS> values(count);
    if(count&&HidP_GetValueCaps(HidP_Feature,values.data(),&count,data)==HIDP_STATUS_SUCCESS)
        for(USHORT i=0;i<count;++i)add(values[i].ReportID);
    count=caps.NumberFeatureButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> buttons(count);
    if(count&&HidP_GetButtonCaps(HidP_Feature,buttons.data(),&count,data)==HIDP_STATUS_SUCCESS)
        for(USHORT i=0;i<count;++i)add(buttons[i].ReportID);
}
std::vector<DeviceReader::Candidate> DeviceReader::Discover(){
    GUID guid;HidD_GetHidGuid(&guid);std::vector<wchar_t> interfaces;
    CONFIGRET status=CR_FAILURE;
    for(int retry=0;retry<3;++retry){
        ULONG size=0;
        if(CM_Get_Device_Interface_List_SizeW(&size,&guid,nullptr,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS)return {};
        interfaces.assign(size+1,L'\0');
        status=CM_Get_Device_Interface_ListW(&guid,nullptr,interfaces.data(),ULONG(interfaces.size()),CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if(status!=CR_BUFFER_SMALL)break;
    }
    if(status!=CR_SUCCESS)return {};
    std::vector<Candidate> candidates;
    for(const wchar_t* path=interfaces.data();*path;path+=wcslen(path)+1){
        std::wstring lower=path;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t ch){return wchar_t(std::towlower(ch));});
        if(lower.find(L"vid_a8a5")==std::wstring::npos&&lower.find(L"vid_3837")==std::wstring::npos&&
           lower.find(L"vid_5253")==std::wstring::npos&&lower.find(L"vid_2023")==std::wstring::npos)continue;
        // Metadata only: keyboard, consumer and mouse-motion collections are never queried.
        Handle handle(CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        if(!handle.valid())continue;
        HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
        if(!HidD_GetAttributes(handle.get(),&attributes))continue;
        PHIDP_PREPARSED_DATA data=nullptr;if(!HidD_GetPreparsedData(handle.get(),&data))continue;
        HIDP_CAPS caps{};NTSTATUS parsed=HidP_GetCaps(data,&caps);
        hid::Descriptor descriptor{attributes.VendorID,attributes.ProductID,caps.UsagePage,caps.Usage,
                                   caps.InputReportByteLength,caps.OutputReportByteLength,caps.FeatureReportByteLength};
        if(parsed==HIDP_STATUS_SUCCESS)FeatureIds(data,caps,descriptor);
        HidD_FreePreparsedData(data);if(parsed!=HIDP_STATUS_SUCCESS)continue;
        wchar_t product[126]{};HidD_GetProductString(handle.get(),product,sizeof(product));
        product[std::size(product)-1]=0;
        hid::Match match{};
        if(hid::MatchDevice(descriptor,product,match))candidates.push_back({path,descriptor,match});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
        int rankA=a.match.protocol==hid::Protocol::A5&&a.descriptor.usage==1?1:0;
        int rankB=b.match.protocol==hid::Protocol::A5&&b.descriptor.usage==1?1:0;
        if(rankA!=rankB)return rankA<rankB;
        return a.path<b.path;
    });
    return candidates;
}
class WindowsTransport final:public hid::Transport {
    HANDLE device_,stop_;ULONGLONG deadline_;
    Handle event_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
public:
    DWORD error=0;
    WindowsTransport(HANDLE device,HANDLE stop,ULONGLONG deadline):device_(device),stop_(stop),deadline_(deadline){}
    bool Cancelled() const override{return WaitForSingleObject(stop_,0)==WAIT_OBJECT_0||GetTickCount64()>=deadline_;}
    bool Pause(unsigned milliseconds) override{
        auto now=GetTickCount64();if(Cancelled())return false;
        return deadline_-now>milliseconds&&WaitForSingleObject(stop_,milliseconds)==WAIT_TIMEOUT;
    }
    bool Transfer(hid::Operation operation,std::span<uint8_t> bytes,size_t& count,unsigned timeoutMs) override{
        count=0;error=0;
        if(!event_.valid()){error=ERROR_NOT_ENOUGH_MEMORY;return false;}
        auto now=GetTickCount64();if(Cancelled()){
            error=WaitForSingleObject(stop_,0)==WAIT_OBJECT_0?ERROR_OPERATION_ABORTED:ERROR_TIMEOUT;return false;
        }
        DWORD timeout=DWORD(std::min<ULONGLONG>(timeoutMs,deadline_-now));
        ResetEvent(event_.get());OVERLAPPED ov{};ov.hEvent=event_.get();DWORD transferred=0;
        BOOL done=FALSE;auto length=DWORD(bytes.size());
        if(operation==hid::Operation::Write)done=WriteFile(device_,bytes.data(),length,&transferred,&ov);
        else if(operation==hid::Operation::Read)done=ReadFile(device_,bytes.data(),length,&transferred,&ov);
        else done=DeviceIoControl(device_,operation==hid::Operation::GetFeature?IOCTL_HID_GET_FEATURE:IOCTL_HID_SET_FEATURE,
                                   bytes.data(),length,bytes.data(),length,&transferred,&ov);
        if(!done){
            error=GetLastError();if(error!=ERROR_IO_PENDING)return false;
            HANDLE waits[]={stop_,event_.get()};DWORD wait=WaitForMultipleObjects(2,waits,FALSE,timeout);
            if(wait!=WAIT_OBJECT_0+1){
                DWORD cause=wait==WAIT_OBJECT_0?ERROR_OPERATION_ABORTED:wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
                CancelIoEx(device_,&ov);GetOverlappedResult(device_,&ov,&transferred,TRUE);error=cause;return false;
            }
            if(!GetOverlappedResult(device_,&ov,&transferred,FALSE)){error=GetLastError();return false;}
        }
        if(operation==hid::Operation::GetFeature&&bytes[0]==0&&transferred<length)++transferred;
        count=std::min<size_t>(transferred,bytes.size());error=0;return true;
    }
};
BatteryResult DeviceReader::QueryCandidate(const Candidate& candidate,HANDLE stop,ULONGLONG deadline){
    BatteryResult r;r.devicePath=candidate.path;r.vendorId=candidate.descriptor.vendor;r.productId=candidate.descriptor.product;
    r.protocol=hid::ProtocolName(candidate.match.protocol);
    r.modelName=candidate.match.model?std::wstring(candidate.match.model->name):L"MCHOSE 鼠标";
    Handle device(CreateFileW(candidate.path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
    if(!device.valid()){r.error=GetLastError();r.message=L"无法打开接收器："+ErrorText(r.error);return r;}
    r.receiverPresent=true;WindowsTransport transport(device.get(),stop,deadline);
    std::array<uint8_t,65> response{};size_t size=0;
    auto reading=hid::Read(transport,candidate.match,candidate.descriptor,response,size);
    r.error=transport.error;r.valid=reading.valid;r.percent=reading.percent;r.charging=reading.charging;
    if(reading.model)r.modelName=reading.model->name;
    if(r.valid){
        r.message=L"已连接 · 2.4G";
        char hex[4];for(size_t i=0;i<size;++i){std::snprintf(hex,sizeof(hex),"%02X",response[i]);if(i)r.response+=' ';r.response+=hex;}
    }else r.message=L"暂时读不到电量，鼠标可能休眠或当前连接不受支持";
    if(r.error==ERROR_DEVICE_NOT_CONNECTED||r.error==ERROR_INVALID_HANDLE||r.error==ERROR_GEN_FAILURE){
        r.receiverPresent=false;r.message=L"接收器已断开";
    }
    return r;
}
BatteryResult DeviceReader::Query(HANDLE stop){
    ULONGLONG started=GetTickCount64(),deadline=started+5000;BatteryResult result;
    auto finish=[&](){result.elapsedMs=DWORD(GetTickCount64()-started);result.sampled=Now();return result;};
    auto cancelled=[&](){return WaitForSingleObject(stop,0)==WAIT_OBJECT_0||GetTickCount64()>=deadline;};
    if(cancelled()){result.error=ERROR_OPERATION_ABORTED;return finish();}
    std::wstring previous;
    if(cached_){
        previous=cached_->path;result=QueryCandidate(*cached_,stop,std::min(deadline,GetTickCount64()+3500));
        if(result.valid||cancelled())return finish();
        cached_.reset();
    }
    auto candidates=Discover();
    for(const auto& candidate:candidates){
        if(cancelled())break;
        if(candidate.path==previous)continue;
        auto current=QueryCandidate(candidate,stop,std::min(deadline,GetTickCount64()+3500));
        if(current.valid){cached_=candidate;result=std::move(current);return finish();}
        if(!result.receiverPresent)result=std::move(current);
    }
    if(!result.receiverPresent)cached_.reset();
    if(result.message.empty())result.message=L"未检测到支持的迈从 2.4G 接收器";
    return finish();
}
}
