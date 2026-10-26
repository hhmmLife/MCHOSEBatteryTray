#include "notification_client.hpp"

namespace g7 {
bool NotificationIdentityCurrent(const Paths& paths){
    wchar_t command[32768]{};DWORD size=sizeof(command);
    constexpr wchar_t key[]=L"Software\\Classes\\CLSID\\{E9C2D221-F9CF-47DD-B821-4C12CE418965}\\LocalServer32";
    if(RegGetValueW(HKEY_CURRENT_USER,key,nullptr,RRF_RT_REG_SZ,nullptr,command,&size)!=ERROR_SUCCESS)return false;
    auto expected=L"\""+paths.exe.wstring()+L"\" --toast-activated";
    if(expected!=command)return false;
    wchar_t display[128]{};DWORD displaySize=sizeof(display);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Classes\\AppUserModelId\\G7BatteryTray.Desktop",L"DisplayName",RRF_RT_REG_SZ,
                   nullptr,display,&displaySize)!=ERROR_SUCCESS||std::wstring_view(display)!=NotificationName)return false;
    DWORD activatorSize=0;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Classes\\AppUserModelId\\G7BatteryTray.Desktop",L"CustomActivator",RRF_RT_REG_SZ,nullptr,nullptr,&activatorSize)!=ERROR_SUCCESS)return false;
    wchar_t appData[32768]{};DWORD count=GetEnvironmentVariableW(L"APPDATA",appData,DWORD(std::size(appData)));
    if(!count||count>=std::size(appData))return false;
    return GetFileAttributesW((fs::path(appData)/L"Microsoft/Windows/Start Menu/Programs/G7BatteryTray.lnk").c_str())!=INVALID_FILE_ATTRIBUTES;
}
bool RunNotificationHelper(const Paths& paths,const std::wstring& argument,HANDLE stopEvent){
    std::wstring command=L"\""+paths.exe.wstring()+L"\" "+argument;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(paths.exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,paths.root.c_str(),&startup,&process)){
        Log(paths,"Notification helper launch failed: "+std::to_string(GetLastError()));return false;
    }
    Handle child(process.hProcess),thread(process.hThread);thread.reset();
    HANDLE events[]={stopEvent,child.get()};DWORD wait=WaitForMultipleObjects(2,events,FALSE,15000);
    if(wait!=WAIT_OBJECT_0+1){
        // This process is our disposable helper, never an unrelated application.
        TerminateProcess(child.get(),ERROR_OPERATION_ABORTED);WaitForSingleObject(child.get(),1000);
        if(wait!=WAIT_OBJECT_0)Log(paths,"Notification helper timed out");
        return false;
    }
    DWORD code=1;return GetExitCodeProcess(child.get(),&code)&&code==0;
}
}
