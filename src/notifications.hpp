#pragma once
#include "storage.hpp"

namespace g7 {
class Notifications {
    const Paths& paths_;
    DWORD classCookie_=0;
    bool initialized_=false;
public:
    explicit Notifications(const Paths& paths):paths_(paths){}
    ~Notifications();
    bool Initialize();
    bool WaitForActivation();
    bool Show(const std::wstring& title,const std::wstring& body,bool test=false);
    void RemoveRegistration();
};
int RunNotificationCommand(const Paths& paths,const std::wstring& command,int percent=-1);
}
