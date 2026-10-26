#include "notifications.hpp"
#include <roapi.h>
#include <winstring.h>
// LLVM-MinGW 20261020 aliases BYTE and boolean to the same C++ type. We do not
// use IReference<boolean>; skip that duplicate specialization in its WinRT header.
#ifdef __MINGW32__
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <windows.ui.notifications.h>
#include <windows.data.xml.dom.h>
#include <notificationactivationcallback.h>
#include <wrl.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>
#include <atomic>

namespace g7 {
using Microsoft::WRL::ComPtr;
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Data::Xml::Dom;
static constexpr CLSID ActivatorClsid={0xe9c2d221,0xf9cf,0x47dd,{0xb8,0x21,0x4c,0x12,0xce,0x41,0x89,0x65}};
static constexpr wchar_t ClsidKey[]=L"Software\\Classes\\CLSID\\{E9C2D221-F9CF-47DD-B821-4C12CE418965}";
static constexpr wchar_t AumidKey[]=L"Software\\Classes\\AppUserModelId\\G7BatteryTray.Desktop";
struct WinString {
    HSTRING value=nullptr;
    explicit WinString(const std::wstring& text){WindowsCreateString(text.c_str(),UINT32(text.size()),&value);}
    ~WinString(){WindowsDeleteString(value);}
};
static HRESULT ShortcutPath(fs::path& out) {
    PWSTR directory=nullptr;HRESULT hr=SHGetKnownFolderPath(FOLDERID_Programs,0,nullptr,&directory);
    if(SUCCEEDED(hr))out=fs::path(directory)/L"G7BatteryTray.lnk";
    CoTaskMemFree(directory);return hr;
}
static HRESULT RegisterShortcut(const Paths& paths) {
    fs::path target;HRESULT hr=ShortcutPath(target);if(FAILED(hr))return hr;
    ComPtr<IShellLinkW> link;hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
    if(FAILED(hr))return hr;
    link->SetPath(paths.exe.c_str());link->SetArguments(L"--show-details");link->SetWorkingDirectory(paths.root.c_str());
    link->SetIconLocation(paths.exe.c_str(),0);link->SetDescription(L"迈从鼠标电量与充电提醒");
    ComPtr<IPropertyStore> store;hr=link.As(&store);if(FAILED(hr))return hr;
    PROPVARIANT value{};hr=InitPropVariantFromString(AppId,&value);
    if(SUCCEEDED(hr))hr=store->SetValue(PKEY_AppUserModel_ID,value);PropVariantClear(&value);if(FAILED(hr))return hr;
    value.vt=VT_CLSID;value.puuid=const_cast<CLSID*>(&ActivatorClsid);
    hr=store->SetValue(PKEY_AppUserModel_ToastActivatorCLSID,value);if(FAILED(hr))return hr;
    hr=store->Commit();if(FAILED(hr))return hr;
    ComPtr<IPersistFile> file;hr=link.As(&file);if(FAILED(hr))return hr;
    hr=file->Save(target.c_str(),TRUE);
    if(SUCCEEDED(hr))SHChangeNotify(SHCNE_CREATE,SHCNF_PATHW,target.c_str(),nullptr);
    return hr;
}
template<class Derived,class Interface> class ComObject:public Interface {
    std::atomic<ULONG> refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** object) override {
        if(!object)return E_POINTER;*object=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(Interface)){*object=static_cast<Interface*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {ULONG count=--refs_;if(!count)delete static_cast<Derived*>(this);return count;}
};
class ActivationCallback final : public ComObject<ActivationCallback,INotificationActivationCallback> {
    fs::path exe_;
    HANDLE activated_;
public:
    ActivationCallback(fs::path exe,HANDLE activated):exe_(std::move(exe)),activated_(activated){}
    HRESULT STDMETHODCALLTYPE Activate(LPCWSTR,LPCWSTR,const NOTIFICATION_USER_INPUT_DATA*,ULONG) override {
        HWND owner=FindWindowW(WindowClass,nullptr);
        if(owner)PostMessageW(owner,ShowDetailsMessage,0,0);
        else{
            auto command=L"\""+exe_.wstring()+L"\" --show-details";
            STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            PROCESS_INFORMATION process{};
            if(CreateProcessW(exe_.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe_.parent_path().c_str(),&startup,&process)){
                CloseHandle(process.hThread);CloseHandle(process.hProcess);
            }
        }
        SetEvent(activated_);return S_OK;
    }
};
class ActivationFactory final : public ComObject<ActivationFactory,IClassFactory> {
    fs::path exe_;
    HANDLE activated_;
public:
    ActivationFactory(fs::path exe,HANDLE activated):exe_(std::move(exe)),activated_(activated){}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** object) override {
        if(outer)return CLASS_E_NOAGGREGATION;
        ComPtr<ActivationCallback> callback;callback.Attach(new ActivationCallback(exe_,activated_));return callback->QueryInterface(iid,object);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override {return S_OK;}
};
Notifications::~Notifications(){if(classCookie_)CoRevokeClassObject(classCookie_);}
bool Notifications::Initialize() {
    HRESULT hr=RegisterShortcut(paths_);
    if(SUCCEEDED(hr)){
        HKEY key=nullptr;
        auto name=std::wstring(ClsidKey)+L"\\LocalServer32";
        LONG status=RegCreateKeyExW(HKEY_CURRENT_USER,name.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
        if(status==ERROR_SUCCESS){
            std::wstring command=L"\""+paths_.exe.wstring()+L"\" --toast-activated";
            status=RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*2));
            RegCloseKey(key);
        }
        if(status!=ERROR_SUCCESS)hr=HRESULT_FROM_WIN32(status);
    }
    if(SUCCEEDED(hr)){
        // Explicit unpackaged identity avoids waiting for Start-menu indexing.
        HKEY key=nullptr;
        LONG status=RegCreateKeyExW(HKEY_CURRENT_USER,AumidKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
        if(status==ERROR_SUCCESS){
            const wchar_t* name=NotificationName;
            const wchar_t* clsid=L"{E9C2D221-F9CF-47DD-B821-4C12CE418965}";
            status=RegSetValueExW(key,L"DisplayName",0,REG_SZ,reinterpret_cast<const BYTE*>(name),DWORD((wcslen(name)+1)*2));
            if(status==ERROR_SUCCESS)status=RegSetValueExW(key,L"CustomActivator",0,REG_SZ,reinterpret_cast<const BYTE*>(clsid),DWORD((wcslen(clsid)+1)*2));
            RegCloseKey(key);
        }
        if(status!=ERROR_SUCCESS)hr=HRESULT_FROM_WIN32(status);
    }
    initialized_=SUCCEEDED(hr);
    if(!initialized_)Log(paths_,"Notification registration failed: "+std::to_string(static_cast<unsigned long>(hr)));
    return initialized_;
}
bool Notifications::WaitForActivation(){
    Handle activated(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!activated.valid())return false;
    ComPtr<IClassFactory> factory;factory.Attach(new ActivationFactory(paths_.exe,activated.get()));
    HRESULT hr=CoRegisterClassObject(ActivatorClsid,factory.Get(),CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&classCookie_);
    if(FAILED(hr)){Log(paths_,"Toast activation server failed: "+std::to_string(static_cast<unsigned long>(hr)));return false;}
    bool success=WaitForSingleObject(activated.get(),15000)==WAIT_OBJECT_0;
    // Allow the RPC response to finish before this disposable COM server exits.
    if(success)Sleep(250);
    CoRevokeClassObject(classCookie_);classCookie_=0;return success;
}
static std::wstring EscapeXml(const std::wstring& text){
    std::wstring out;for(wchar_t ch:text){switch(ch){case L'&':out+=L"&amp;";break;case L'<':out+=L"&lt;";break;case L'>':out+=L"&gt;";break;case L'\"':out+=L"&quot;";break;default:out+=ch;}}return out;
}
bool Notifications::Show(const std::wstring& title,const std::wstring& body,bool test){
    if(!initialized_)return false;
    const char* stage="activate XML document";
    ComPtr<IInspectable> documentObject;
    WinString xmlClass(L"Windows.Data.Xml.Dom.XmlDocument");HRESULT hr=RoActivateInstance(xmlClass.value,&documentObject);
    ComPtr<IXmlDocument> document;ComPtr<IXmlDocumentIO> io;
    if(SUCCEEDED(hr)){stage="XML interfaces";hr=documentObject.As(&document);}if(SUCCEEDED(hr))hr=documentObject.As(&io);
    auto xml=L"<toast launch=\"details\" duration=\"long\"><visual><binding template=\"ToastGeneric\"><text>"+
        EscapeXml(title)+L"</text><text>"+EscapeXml(body)+L"</text></binding></visual></toast>";
    WinString content(xml);if(SUCCEEDED(hr)){stage="load toast XML";hr=io->LoadXml(content.value);}
    ComPtr<IToastNotificationFactory> factory;WinString toastClass(L"Windows.UI.Notifications.ToastNotification");
    if(SUCCEEDED(hr)){stage="toast factory";hr=RoGetActivationFactory(toastClass.value,IID_PPV_ARGS(&factory));}
    ComPtr<IToastNotification> toast;if(SUCCEEDED(hr)){stage="create toast";hr=factory->CreateToastNotification(document.Get(),&toast);}
    if(SUCCEEDED(hr)){
        ComPtr<IToastNotification2> tagged;
        if(SUCCEEDED(toast.As(&tagged))){WinString tag(test?L"test":L"battery"),group(L"G7");tagged->put_Tag(tag.value);tagged->put_Group(group.value);}
    }
    ComPtr<IToastNotificationManagerStatics> manager;WinString managerClass(L"Windows.UI.Notifications.ToastNotificationManager");
    if(SUCCEEDED(hr)){stage="notification manager";hr=RoGetActivationFactory(managerClass.value,IID_PPV_ARGS(&manager));}
    ComPtr<IToastNotifier> notifier;WinString appId(AppId);
    if(SUCCEEDED(hr)){stage="create app notifier";hr=manager->CreateToastNotifierWithId(appId.value,&notifier);}
    // get_Setting can return ERROR_NOT_FOUND for an unpackaged identity even
    // though Show is supported. Let the notification platform enforce settings.
    if(SUCCEEDED(hr)){stage="show toast";hr=notifier->Show(toast.Get());}
    if(FAILED(hr))Log(paths_,std::string("Toast failed at ")+stage+": "+std::to_string(static_cast<unsigned long>(hr)));
    else Log(paths_,test?"Test notification submitted":"Low battery notification submitted");
    return SUCCEEDED(hr);
}
void Notifications::RemoveRegistration(){
    // Clear our own retained notification history while the identity still exists.
    ComPtr<IToastNotificationManagerStatics2> manager;
    WinString cls(L"Windows.UI.Notifications.ToastNotificationManager"),id(AppId);
    if(SUCCEEDED(RoGetActivationFactory(cls.value,IID_PPV_ARGS(&manager)))){
        ComPtr<IToastNotificationHistory> history;if(SUCCEEDED(manager->get_History(&history)))history->ClearWithId(id.value);
    }
    fs::path shortcut;if(SUCCEEDED(ShortcutPath(shortcut))){DeleteFileW(shortcut.c_str());SHChangeNotify(SHCNE_DELETE,SHCNF_PATHW,shortcut.c_str(),nullptr);}
    RegDeleteTreeW(HKEY_CURRENT_USER,ClsidKey);RegDeleteTreeW(HKEY_CURRENT_USER,AumidKey);
    Log(paths_,"Removed notification and startup registrations");
}
int RunNotificationCommand(const Paths& paths,const std::wstring& command,int percent){
    HRESULT hr=RoInitialize(RO_INIT_MULTITHREADED);
    if(FAILED(hr)){Log(paths,"Notification helper initialization failed");return 1;}
    SetCurrentProcessExplicitAppUserModelID(AppId);
    bool success=false;
    {
        Notifications notifications(paths);
        if(command==L"--toast-activated")success=notifications.WaitForActivation();
        else if(command==L"--notification-unregister"){notifications.RemoveRegistration();success=true;}
        else if(notifications.Initialize()){
            if(command==L"--notification-register")success=true;
            else if(command==L"--notification-test")success=notifications.Show(L"迈从鼠标电量提醒测试",L"这是一条测试通知，定时提醒记录不会改变。",true);
            else if(command==L"--notification-battery"&&percent>=0&&percent<=100)
                success=notifications.Show(L"鼠标需要充电",L"迈从鼠标当前电量 "+std::to_wstring(percent)+L"%，建议连接充电线。");
        }
    }
    RoUninitialize();return success?0:1;
}
}
