#include "device.hpp"
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>
#include <cwctype>
#include <vector>

namespace g7 {
std::wstring DeviceReader::Discover() {
    GUID guid;HidD_GetHidGuid(&guid);
    std::vector<wchar_t> interfaces;
    CONFIGRET status=CR_FAILURE;
    for(int retry=0;retry<3;++retry){
        ULONG size=0;
        if(CM_Get_Device_Interface_List_SizeW(&size,&guid,nullptr,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS)return {};
        interfaces.assign(size+1,L'\0');
        status=CM_Get_Device_Interface_ListW(&guid,nullptr,interfaces.data(),ULONG(interfaces.size()),CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if(status!=CR_BUFFER_SMALL)break;
    }
    if(status!=CR_SUCCESS)return {};
    for(const wchar_t* path=interfaces.data();*path;path+=wcslen(path)+1){
        std::wstring match=path;
        std::transform(match.begin(),match.end(),match.begin(),[](wchar_t ch){return wchar_t(std::towlower(ch));});
        if(match.find(L"vid_a8a5&pid_2255")==std::wstring::npos)continue;
        // Read metadata only; never open the keyboard or mouse movement collections for input.
        Handle handle(CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        if(!handle.valid())continue;
        HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
        if(!HidD_GetAttributes(handle.get(),&attributes)||attributes.VendorID!=0xA8A5||attributes.ProductID!=0x2255)continue;
        PHIDP_PREPARSED_DATA data=nullptr;
        if(!HidD_GetPreparsedData(handle.get(),&data))continue;
        HIDP_CAPS caps{};NTSTATUS parsed=HidP_GetCaps(data,&caps);HidD_FreePreparsedData(data);
        if(parsed==HIDP_STATUS_SUCCESS&&caps.UsagePage==0xFF01&&caps.Usage==0x10&&
           caps.InputReportByteLength==65&&caps.OutputReportByteLength==65)return path;
    }
    return {};
}
static bool Transfer(HANDLE device,bool write,BYTE* bytes,DWORD size,HANDLE stop,DWORD timeout,DWORD& count,DWORD& error){
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event.valid()){error=GetLastError();return false;}
    OVERLAPPED ov{};ov.hEvent=event.get();
    BOOL done=write?WriteFile(device,bytes,size,&count,&ov):ReadFile(device,bytes,size,&count,&ov);
    if(done)return true;
    error=GetLastError();if(error!=ERROR_IO_PENDING)return false;
    HANDLE waits[]={stop,event.get()};DWORD result=WaitForMultipleObjects(2,waits,FALSE,timeout);
    if(result!=WAIT_OBJECT_0+1){
        DWORD cause=result==WAIT_OBJECT_0?ERROR_OPERATION_ABORTED:result==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
        CancelIoEx(device,&ov);GetOverlappedResult(device,&ov,&count,TRUE);error=cause;return false;
    }
    if(!GetOverlappedResult(device,&ov,&count,FALSE)){error=GetLastError();return false;}
    return true;
}
BatteryResult DeviceReader::Query(HANDLE stop) {
    BatteryResult r;ULONGLONG started=GetTickCount64();
    auto finish=[&](){r.elapsedMs=DWORD(GetTickCount64()-started);r.sampled=Now();return r;};
    if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0){r.error=ERROR_OPERATION_ABORTED;return finish();}
    if(cachedPath_.empty())cachedPath_=Discover();
    r.devicePath=cachedPath_;
    if(cachedPath_.empty()){r.message=L"未检测到 G7 2.4G 接收器";return finish();}
    Handle device(CreateFileW(cachedPath_.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
    if(!device.valid()){r.error=GetLastError();r.message=L"无法打开接收器："+ErrorText(r.error);cachedPath_.clear();return finish();}
    r.receiverPresent=true;
    // Exact getBatteryInfo() request from the official G7 /gmouse driver.
    // Windows HID includes a zero report-ID byte; response payload AA 30 ... [8]=%, [9]=charging.
    BYTE command[65]={0,0x55,0x30,0xA5,0x0B,0x2E,1,1,1,0,0};
    DWORD count=0,error=0;
    if(!Transfer(device.get(),true,command,65,stop,1500,count,error)||count!=65){
        r.error=error;r.message=L"状态查询发送失败";cachedPath_.clear();return finish();
    }
    ULONGLONG deadline=GetTickCount64()+2000;
    while(true){
        BYTE response[65]{};
        ULONGLONG current=GetTickCount64();if(current>=deadline)break;
        DWORD remaining=DWORD(deadline-current);
        if(!Transfer(device.get(),false,response,65,stop,remaining,count,error)){r.error=error;break;}
        if(count<11||response[0]!=0||response[1]!=0xAA||response[2]!=0x30||response[3]!=0xA5||response[4]!=0x0B)continue;
        if(response[9]>100||response[10]>1)continue;
        r.valid=true;r.percent=response[9];r.charging=response[10]!=0;r.message=L"已连接 · 2.4G";
        char hex[4];for(DWORD i=0;i<count;++i){std::snprintf(hex,sizeof(hex),"%02X",response[i]);if(i)r.response+=' ';r.response+=hex;}
        return finish();
    }
    r.message=L"暂时读不到电量，鼠标可能休眠";
    if(r.error==ERROR_DEVICE_NOT_CONNECTED||r.error==ERROR_INVALID_HANDLE||r.error==ERROR_GEN_FAILURE){
        cachedPath_.clear();r.receiverPresent=false;r.message=L"接收器已断开";
    }
    return finish();
}
}
