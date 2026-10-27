#include "storage.hpp"
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace mchose {
Paths Paths::Locate() {
    std::wstring name(32768,L'\0');
    DWORD count = GetModuleFileNameW(nullptr,name.data(),DWORD(name.size()));
    if (!count || count == name.size()) throw std::runtime_error("Cannot locate executable");
    name.resize(count);
    Paths p; p.exe=name; p.root=p.exe.parent_path(); p.data=p.root/L"data"; p.logs=p.data/L"logs"; return p;
}
void Paths::Ensure() const { fs::create_directories(logs); }
void AtomicJson(const fs::path& path, const Json& json) {
    fs::path temp = path; temp += L".tmp";
    auto content = json.dump(2) + "\n";
    Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    DWORD written=0;
    if (!file.valid() || !WriteFile(file.get(),content.data(),DWORD(content.size()),&written,nullptr) ||
        written != content.size() || !FlushFileBuffers(file.get()))
        throw std::runtime_error("Cannot write " + Utf8(path.wstring()));
    file.reset();
    if (!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace " + Utf8(path.wstring()));
}
static Json ReadJson(const fs::path& path) {
    if (fs::file_size(path) > 65536) throw std::runtime_error("JSON file exceeds 64 KiB");
    std::ifstream input(path); return Json::parse(input);
}
static void Validate(const Config& c) {
    if(c.hour<0||c.hour>23||c.minute<0||c.minute>59||c.threshold<1||c.threshold>100||c.pollSeconds<15||c.pollSeconds>3600)
        throw std::runtime_error("Invalid time, threshold or refresh interval");
    for(int day:c.weekdays)if(day<0||day>6)throw std::runtime_error("Invalid weekday");
    for(const auto* dates:{&c.skipDates,&c.extraDates})for(const auto& date:*dates)
        if(!ValidDate(date))throw std::runtime_error("Invalid exception date: "+date);
}
Config LoadConfig(const Paths& p) {
    auto path=p.data/L"config.json"; Config c;
    if(!fs::exists(path)){SaveConfig(p,c);return c;}
    auto j=ReadJson(path);
    if(!j.is_object()||j.value("schemaVersion",1)!=1)throw std::runtime_error("Unsupported config schema");
    c.reminders=j.value("remindersEnabled",true);c.startup=j.value("startupEnabled",false);
    std::string time=j.value("reminderTime",std::string("20:59"));
    if(time.size()!=5||time[2]!=':'||time[0]<'0'||time[0]>'9'||time[1]<'0'||time[1]>'9'||time[3]<'0'||time[3]>'9'||time[4]<'0'||time[4]>'9')
        throw std::runtime_error("reminderTime must be HH:mm");
    c.hour=(time[0]-'0')*10+time[1]-'0';c.minute=(time[3]-'0')*10+time[4]-'0';
    c.threshold=j.value("lowBatteryThreshold",25);c.pollSeconds=j.value("pollSeconds",60);
    c.weekdays=j.value("weekdays",c.weekdays);
    c.skipDates=j.value("skipDates",c.skipDates);c.extraDates=j.value("extraWorkDates",c.extraDates);
    Validate(c);return c;
}
void SaveConfig(const Paths& p,const Config& c) {
    Validate(c);char time[8];std::snprintf(time,sizeof(time),"%02d:%02d",c.hour,c.minute);
    AtomicJson(p.data/L"config.json",{{"schemaVersion",1},{"remindersEnabled",c.reminders},{"startupEnabled",c.startup},
        {"reminderTime",time},{"lowBatteryThreshold",c.threshold},{"pollSeconds",c.pollSeconds},
        {"weekdays",c.weekdays},{"skipDates",c.skipDates},{"extraWorkDates",c.extraDates}});
}
ReminderState LoadState(const Paths& p) {
    auto path=p.data/L"state.json";if(!fs::exists(path))return {};
    auto j=ReadJson(path);ReminderState s{j.value("lastCheckedDate",std::string()),j.value("lastNotifiedDate",std::string())};
    if((!s.lastCheckedDate.empty()&&!ValidDate(s.lastCheckedDate))||(!s.lastNotifiedDate.empty()&&!ValidDate(s.lastNotifiedDate)))
        throw std::runtime_error("Invalid reminder state");
    return s;
}
void SaveState(const Paths& p,const ReminderState& s) {
    AtomicJson(p.data/L"state.json",{{"schemaVersion",1},{"lastCheckedDate",s.lastCheckedDate},{"lastNotifiedDate",s.lastNotifiedDate}});
}
void Log(const Paths& p,const std::string& message) {
    static std::mutex mutex;std::lock_guard lock(mutex);
    try {
        auto path=p.logs/L"app.log";auto backup=p.logs/L"app.previous.log";
        if(fs::exists(path)&&fs::file_size(path)>=256*1024){
            MoveFileExW(path.c_str(),backup.c_str(),MOVEFILE_REPLACE_EXISTING);
        }
        std::ofstream out(path,std::ios::app|std::ios::binary);
        auto now=Now();out<<DateKey(now)<<" "<<Utf8(ClockText(now))<<" "<<message<<"\n";
    }catch(...){}
}
static constexpr wchar_t RunKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool StartupRegistered() {
    DWORD size=0;return RegGetValueW(HKEY_CURRENT_USER,RunKey,AppName,RRF_RT_REG_SZ,nullptr,nullptr,&size)==ERROR_SUCCESS;
}
void SetStartup(const Paths& p,bool enabled) {
    HKEY key=nullptr;LONG status=RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot access startup registration");
    if(enabled){std::wstring command=L"\""+p.exe.wstring()+L"\" --background";
        status=RegSetValueExW(key,AppName,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*2));
    }else{status=RegDeleteValueW(key,AppName);if(status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;}
    RegCloseKey(key);if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot update startup registration");
}
bool DarkTheme() {
    DWORD light=1,size=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
    return light==0;
}
}
