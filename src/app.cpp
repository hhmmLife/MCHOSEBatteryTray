#include "device.hpp"
#include "notifications.hpp"
#include "notification_client.hpp"
#include "tray_icon.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <dbt.h>
#include <hidsdi.h>
#include <commctrl.h>
#include <windowsx.h>
#include <imm.h>
#include <thread>
#include <memory>
#include <sstream>
#include <vector>

namespace g7 {
constexpr UINT QueryDone=WM_APP+2, TestNotificationMessage=WM_APP+3,SettingsMessage=WM_APP+4,HideWindowsMessage=WM_APP+5,RemoveMessage=WM_APP+6,StatusMessage=WM_APP+7;
constexpr UINT NotificationDone=WM_APP+8;
constexpr UINT ReleaseFontsMessage=WM_APP+9;
constexpr UINT TrayMessage=WM_APP+10;
enum : UINT {PollTimer=1,ScheduleTimer=2,PopupTimer=3};
enum : UINT {Details=10,Refresh,Settings,TestNotification,Startup,OpenFolder,Cleanup,Exit};

static int Scale(int value,UINT dpi){return MulDiv(value,int(dpi),96);}
static HFONT Font(int height,UINT dpi,int weight=FW_NORMAL){
    return CreateFontW(-Scale(height,dpi),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}

class App {
    struct NotificationResult {bool success=false,test=false;std::string date;};
    Paths paths_;Config config_;ReminderState reminder_;
    HWND window_=nullptr,popup_=nullptr,settings_=nullptr;
    HFONT font_=nullptr,popupFont_=nullptr,bigFont_=nullptr;
    HICON icon_=nullptr,classIcon_=nullptr;HDEVNOTIFY deviceNotify_=nullptr;
    Handle stop_{CreateEventW(nullptr,TRUE,FALSE,nullptr)},work_{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    std::thread worker_,notificationWorker_;
    bool notifying_=false;
    bool busy_=false,requery_=false,closing_=false,trayAdded_=false;
    bool dark_=false,everRead_=false;int iconPercent_=-2,iconSize_=0;bool iconValid_=false,iconCharging_=false,iconDark_=false;
    BatteryResult result_,lastGood_;
    std::wstring lastStatus_;
    UINT taskbarCreated_=0,dpi_=96;
    ULONGLONG nextNotifyAttempt_=0;
    unsigned queryCount_=0;

    void EnsureFonts(bool popup=false){
        if(!font_)font_=Font(14,dpi_);
        if(popup){if(!popupFont_)popupFont_=Font(15,dpi_);if(!bigFont_)bigFont_=Font(42,dpi_,FW_SEMIBOLD);}
    }
    void ReleaseUnusedFonts(){
        if(settings_||(popup_&&IsWindowVisible(popup_)))return;
        for(HFONT* font:{&font_,&popupFont_,&bigFont_}){if(*font)DeleteObject(*font);*font=nullptr;}
    }

    static App* Self(HWND hwnd){return reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));}
    static LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        App* self=Self(hwnd);
        if(msg==WM_NCCREATE){self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window_=hwnd;}
        if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
        try{return self->Message(msg,wp,lp);}catch(const std::exception& e){Log(self->paths_,std::string("UI error: ")+e.what());return 0;}
    }
    static LRESULT CALLBACK PopupProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        App* self=Self(hwnd);
        if(msg==WM_NCCREATE){self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
        if(msg==WM_PAINT){self->PaintPopup(hwnd);return 0;}
        if(msg==WM_LBUTTONDOWN||msg==WM_RBUTTONDOWN){ShowWindow(hwnd,SW_HIDE);KillTimer(self->window_,PopupTimer);self->ReleaseUnusedFonts();return 0;}
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    static LRESULT CALLBACK SettingsProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        App* self=Self(hwnd);
        if(msg==WM_NCCREATE){self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->settings_=hwnd;}
        if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
        if(msg==WM_COMMAND){
            if(LOWORD(wp)==IDOK){self->SaveSettings();return 0;}
            if(LOWORD(wp)==IDCANCEL){DestroyWindow(hwnd);return 0;}
        }
        if(msg==WM_DESTROY){self->settings_=nullptr;PostMessageW(self->window_,ReleaseFontsMessage,0,0);return 0;}
        if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    void StartQuery(bool queueIfBusy=false){
        if(closing_)return;
        if(busy_){requery_|=queueIfBusy;return;}
        busy_=true;KillTimer(window_,PollTimer);SetEvent(work_.get());
    }
    void Worker(){
        if(!NotificationIdentityCurrent(paths_))RunNotificationHelper(paths_,L"--notification-register",stop_.get());
        DeviceReader reader;HANDLE events[]={stop_.get(),work_.get()};
        while(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1){
            auto response=std::make_unique<BatteryResult>();
            try{*response=reader.Query(stop_.get());}catch(const std::exception& e){response->message=L"读取异常："+Wide(e.what());}
            if(WaitForSingleObject(stop_.get(),0)==WAIT_OBJECT_0)break;
            if(PostMessageW(window_,QueryDone,0,reinterpret_cast<LPARAM>(response.get())))response.release();
        }
    }
    NOTIFYICONDATAW TrayData() const {
        NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=window_;data.uID=1;
        // HWND + uID remains valid after moving an unsigned portable executable.
        // A fixed GUID is bound to the original path by the Windows shell.
        return data;
    }
    std::wstring Tooltip() const {
        std::wstring text=L"MCHOSE G7 · ";
        if(result_.valid)text+=std::to_wstring(result_.percent)+L"%"+(result_.charging?L" · 充电中":L" · 2.4G");
        else text+=result_.receiverPresent?L"暂不可读 / 可能休眠":L"未连接";
        if(!result_.valid&&everRead_)text+=L"\n上次 "+std::to_wstring(lastGood_.percent)+L"% · "+ClockText(lastGood_.sampled);
        else if(result_.valid)text+=L"\n更新于 "+ClockText(result_.sampled);
        else text+=L"\n"+result_.message;
        return text;
    }
    void UpdateTray(bool force=false){
        dark_=DarkTheme();int size=GetSystemMetricsForDpi(SM_CXSMICON,dpi_);
        bool changed=force||size!=iconSize_||result_.percent!=iconPercent_||result_.valid!=iconValid_||result_.charging!=iconCharging_||dark_!=iconDark_;
        auto data=TrayData();data.uFlags|=NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;data.uCallbackMessage=TrayMessage;
        auto tip=Tooltip();lstrcpynW(data.szTip,tip.c_str(),int(std::size(data.szTip)));
        HICON previous=nullptr;
        if(changed||!icon_){previous=icon_;icon_=MakeIcon(size,result_.percent,result_.valid,result_.charging,dark_);data.uFlags|=NIF_ICON;data.hIcon=icon_;}
        if(!trayAdded_){data.uFlags|=NIF_ICON;data.hIcon=icon_;trayAdded_=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
            data.uVersion=NOTIFYICON_VERSION_4;if(trayAdded_)Shell_NotifyIconW(NIM_SETVERSION,&data);
        }else Shell_NotifyIconW(NIM_MODIFY,&data);
        if(previous)DestroyIcon(previous);
        iconPercent_=result_.percent;iconValid_=result_.valid;iconCharging_=result_.charging;iconDark_=dark_;iconSize_=size;
        if(popup_&&IsWindowVisible(popup_))InvalidateRect(popup_,nullptr,FALSE);
    }
    void ArmSchedule(){
        KillTimer(window_,ScheduleTimer);
        if(config_.reminders){auto delay=NextReminderSeconds(config_,reminder_,Now());
            SetTimer(window_,ScheduleTimer,UINT(std::min<ULONGLONG>(ULONGLONG(delay)*1000,USER_TIMER_MAXIMUM)),nullptr);}
    }
    void Notify(bool test){
        if(notifying_||closing_)return;
        if(notificationWorker_.joinable())notificationWorker_.join();
        notifying_=true;
        std::wstring argument=test?L"--notification-test":L"--notification-battery="+std::to_wstring(result_.percent);
        auto date=DateKey(Now());
        notificationWorker_=std::thread([this,test,date,argument]{
            auto response=std::make_unique<NotificationResult>();response->test=test;response->date=date;
            response->success=RunNotificationHelper(paths_,argument,stop_.get());
            if(WaitForSingleObject(stop_.get(),0)==WAIT_OBJECT_0)return;
            if(PostMessageW(window_,NotificationDone,0,reinterpret_cast<LPARAM>(response.get())))response.release();
        });
    }
    void OnNotification(std::unique_ptr<NotificationResult> response){
        if(notificationWorker_.joinable())notificationWorker_.join();notifying_=false;
        if(response->success&&!response->test){
            reminder_.lastNotifiedDate=response->date;reminder_.lastCheckedDate=response->date;
            try{SaveState(paths_,reminder_);}catch(const std::exception& e){Log(paths_,e.what());}
            ArmSchedule();
        }
        if(!response->success&&response->test)MessageBoxW(settings_?settings_:window_,L"Windows 未能接收通知。请检查系统的通知权限，诊断信息位于 data\\logs\\app.log。",L"测试通知",MB_OK|MB_ICONINFORMATION);
    }
    void OnResult(std::unique_ptr<BatteryResult> response){
        busy_=false;++queryCount_;result_=std::move(*response);
        if(result_.valid){lastGood_=result_;everRead_=true;}
        std::wstring status=result_.valid?L"Device online":result_.message;
        if(status!=lastStatus_){Log(paths_,Utf8(status));lastStatus_=status;}
        UpdateTray();auto now=Now();
        if(result_.valid&&ReminderDue(config_,reminder_,now)){
            bool low=NeedsCharge(config_,result_.percent,result_.charging);
            if(!low){reminder_.lastCheckedDate=DateKey(now);try{SaveState(paths_,reminder_);}catch(const std::exception& e){Log(paths_,e.what());}}
            else if(!notifying_&&GetTickCount64()>=nextNotifyAttempt_){
                nextNotifyAttempt_=GetTickCount64()+5*60*1000;
                Notify(false);
            }
        }
        SetTimer(window_,PollTimer,UINT((result_.valid?config_.pollSeconds:std::max(config_.pollSeconds,300))*1000),nullptr);
        ArmSchedule();
        if(requery_){requery_=false;StartQuery();}
    }
    void ShowPopup(){
        EnsureFonts(true);
        if(!popup_)popup_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,L"G7BatteryTray.Popup",L"G7 电量详情",WS_POPUP|WS_BORDER,
                                          0,0,1,1,window_,nullptr,GetModuleHandleW(nullptr),this);
        if(!popup_)return;
        POINT cursor;GetCursorPos(&cursor);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
        int width=Scale(340,dpi_),height=Scale(200,dpi_);
        int x=std::clamp(cursor.x-width/2,monitor.rcWork.left+8,std::max(monitor.rcWork.left+8,monitor.rcWork.right-width-8));
        int y=std::clamp(cursor.y-height-12,monitor.rcWork.top+8,std::max(monitor.rcWork.top+8,monitor.rcWork.bottom-height-8));
        SetWindowPos(popup_,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(popup_,nullptr,FALSE);SetTimer(window_,PopupTimer,4000,nullptr);
    }
    void PaintPopup(HWND hwnd){
        PAINTSTRUCT paint;HDC target=BeginPaint(hwnd,&paint);RECT bounds;GetClientRect(hwnd,&bounds);
        HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,bounds.right,bounds.bottom);
        auto oldBitmap=SelectObject(dc,bitmap);HBRUSH background=CreateSolidBrush(dark_?RGB(30,35,42):RGB(250,251,252));
        FillRect(dc,&bounds,background);DeleteObject(background);SetBkMode(dc,TRANSPARENT);
        auto color=dark_?RGB(237,242,246):RGB(30,40,50);SetTextColor(dc,color);
        auto text=[&](const std::wstring& value,int x,int y,int width,int height,HFONT font,COLORREF color){
            auto old=SelectObject(dc,font);SetTextColor(dc,color);RECT rect{Scale(x,dpi_),Scale(y,dpi_),Scale(x+width,dpi_),Scale(y+height,dpi_)};
            DrawTextW(dc,value.c_str(),-1,&rect,DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK);SelectObject(dc,old);
        };
        text(L"MCHOSE G7",22,17,290,25,popupFont_,color);
        std::wstring percent=result_.valid?std::to_wstring(result_.percent)+L"%":L"—";
        text(percent,22,44,180,58,bigFont_,color);
        DrawIconEx(dc,Scale(264,dpi_),Scale(50,dpi_),icon_,Scale(40,dpi_),Scale(40,dpi_),0,nullptr,DI_NORMAL);
        text(result_.valid?(result_.charging?L"正在充电 · 2.4G":L"已连接 · 2.4G"):result_.message,22,112,298,36,popupFont_,color);
        std::wstring updated=everRead_?L"上次读取 "+ClockText(lastGood_.sampled)+(result_.valid?L"":L" · "+std::to_wstring(lastGood_.percent)+L"%"):
                                               L"等待读取鼠标电量";
        text(updated,22,157,298,25,font_,dark_?RGB(163,175,185):RGB(100,112,122));
        BitBlt(target,0,0,bounds.right,bounds.bottom,dc,0,0,SRCCOPY);
        SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(hwnd,&paint);
    }
    void Menu(){
        HMENU menu=CreatePopupMenu();
        auto status=result_.valid?L"MCHOSE G7  ·  "+std::to_wstring(result_.percent)+L"%":L"MCHOSE G7  ·  暂不可读";
        AppendMenuW(menu,MF_STRING|MF_DISABLED,0,status.c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,Details,L"电量详情");AppendMenuW(menu,MF_STRING|(busy_?MF_GRAYED:0),Refresh,L"立即刷新");
        AppendMenuW(menu,MF_STRING,Settings,L"提醒与刷新设置…");AppendMenuW(menu,MF_STRING|(notifying_?MF_GRAYED:0),TestNotification,L"测试 Windows 通知");
        AppendMenuW(menu,MF_STRING|(config_.startup?MF_CHECKED:0),Startup,L"开机自动启动");
        AppendMenuW(menu,MF_STRING,OpenFolder,L"打开数据目录");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,Cleanup,L"移除系统登记并退出");AppendMenuW(menu,MF_STRING,Exit,L"退出");
        POINT point;GetCursorPos(&point);SetForegroundWindow(window_);
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window_,nullptr);
        DestroyMenu(menu);PostMessageW(window_,WM_NULL,0,0);if(command)Command(command);
    }
    void ToggleStartup(){
        Config next=config_;next.startup=!next.startup;
        try{SetStartup(paths_,next.startup);try{SaveConfig(paths_,next);}catch(...){SetStartup(paths_,config_.startup);throw;}config_=next;}
        catch(const std::exception& e){MessageBoxW(window_,Wide(e.what()).c_str(),L"自启设置未保存",MB_OK|MB_ICONERROR);}
    }
    void OpenDataDirectory(){
        wchar_t directory[32768]{};UINT size=GetWindowsDirectoryW(directory,UINT(std::size(directory)));
        if(!size||size>=std::size(directory))return;
        fs::path explorer=fs::path(directory)/L"explorer.exe";
        auto command=L"\""+explorer.wstring()+L"\" \""+paths_.data.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNORMAL;
        PROCESS_INFORMATION process{};
        // Let Explorer handle its own shell/COM state instead of loading it here.
        if(CreateProcessW(explorer.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)){
            CloseHandle(process.hThread);CloseHandle(process.hProcess);
        }
    }
    void Command(int command){
        switch(command){
        case Details:ShowPopup();break;
        case Refresh:StartQuery();break;
        case Settings:ShowSettings();break;
        case TestNotification:Notify(true);break;
        case Startup:ToggleStartup();break;
        case OpenFolder:OpenDataDirectory();break;
        case Cleanup:
            config_.startup=false;SetStartup(paths_,false);SaveConfig(paths_,config_);RunNotificationHelper(paths_,L"--notification-unregister",stop_.get());
            PostMessageW(window_,WM_CLOSE,0,0);break;
        case Exit:PostMessageW(window_,WM_CLOSE,0,0);break;
        }
    }
    HWND Control(const wchar_t* type,const std::wstring& text,DWORD style,int x,int y,int width,int height,int id=0){
        HWND control=CreateWindowExW(wcscmp(type,L"EDIT")==0?WS_EX_CLIENTEDGE:0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,
            Scale(x,dpi_),Scale(y,dpi_),Scale(width,dpi_),Scale(height,dpi_),settings_,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);return control;
    }
    static std::wstring JoinDates(const std::set<std::string>& dates){std::wstring out;for(const auto& date:dates){if(!out.empty())out+=L", ";out+=Wide(date);}return out;}
    static std::set<std::string> ParseDates(std::wstring text){
        for(auto& c:text)if(c==L','||c==L';'||c==L'，'||c==L'；')c=L' ';
        std::wistringstream input(text);std::wstring date;std::set<std::string> values;
        while(input>>date){auto utf=Utf8(date);if(!ValidDate(utf))throw std::runtime_error("例外日期请使用 YYYY-MM-DD，多个日期用逗号分隔。");values.insert(utf);}return values;
    }
    std::wstring Field(int id){wchar_t text[8192];GetDlgItemTextW(settings_,id,text,int(std::size(text)));return text;}
    void ShowSettings(){
        if(settings_){SetForegroundWindow(settings_);return;}
        EnsureFonts();
        RECT area{0,0,Scale(460,dpi_),Scale(505,dpi_)};AdjustWindowRectExForDpi(&area,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_APPWINDOW,dpi_);
        settings_=CreateWindowExW(WS_EX_APPWINDOW,L"G7BatteryTray.Settings",L"G7BatteryTray 设置",WS_CAPTION|WS_SYSMENU,
            CW_USEDEFAULT,CW_USEDEFAULT,area.right-area.left,area.bottom-area.top,nullptr,nullptr,GetModuleHandleW(nullptr),this);
        if(!settings_)return;
        Control(L"BUTTON",L"启用低电量定时提醒",BS_AUTOCHECKBOX|WS_TABSTOP,20,18,400,25,101);
        SendDlgItemMessageW(settings_,101,BM_SETCHECK,config_.reminders?BST_CHECKED:BST_UNCHECKED,0);
        wchar_t time[8];swprintf_s(time,L"%02d:%02d",config_.hour,config_.minute);
        Control(L"STATIC",L"提醒时间（本地时间）",0,20,58,210,23);Control(L"EDIT",time,WS_TABSTOP|ES_AUTOHSCROLL,270,55,150,26,102);
        Control(L"STATIC",L"电量低于此值时提醒（%）",0,20,96,240,23);Control(L"EDIT",std::to_wstring(config_.threshold),WS_TABSTOP|ES_NUMBER,270,93,150,26,103);
        Control(L"STATIC",L"刷新间隔（15–3600 秒）",0,20,134,240,23);Control(L"EDIT",std::to_wstring(config_.pollSeconds),WS_TABSTOP|ES_NUMBER,270,131,150,26,104);
        Control(L"STATIC",L"提醒日期",0,20,176,400,24);
        const wchar_t* days[]={L"日",L"一",L"二",L"三",L"四",L"五",L"六"};
        for(int i=0;i<7;++i){Control(L"BUTTON",days[i],BS_AUTOCHECKBOX|WS_TABSTOP,20+i*60,205,53,25,201+i);SendDlgItemMessageW(settings_,201+i,BM_SETCHECK,config_.weekdays.contains(i)?BST_CHECKED:BST_UNCHECKED,0);}
        Control(L"STATIC",L"跳过日期（节假日，YYYY-MM-DD）",0,20,250,420,24);
        Control(L"EDIT",JoinDates(config_.skipDates),WS_TABSTOP|ES_AUTOHSCROLL,20,278,400,26,105);
        Control(L"STATIC",L"额外工作日期（调休补班，逗号分隔）",0,20,320,420,24);
        Control(L"EDIT",JoinDates(config_.extraDates),WS_TABSTOP|ES_AUTOHSCROLL,20,348,400,26,106);
        Control(L"STATIC",L"充电中不提醒；同日只提醒一次。\n休眠后当天补检，跨天不补发。",0,20,392,420,48);
        Control(L"BUTTON",L"保存",WS_TABSTOP|BS_DEFPUSHBUTTON,250,458,80,30,IDOK);
        Control(L"BUTTON",L"取消",WS_TABSTOP|BS_PUSHBUTTON,340,458,80,30,IDCANCEL);
        for(int id:{102,103,104})SendDlgItemMessageW(settings_,id,EM_SETLIMITTEXT,id==102?5:4,0);
        for(int id:{105,106})SendDlgItemMessageW(settings_,id,EM_SETLIMITTEXT,4096,0);
        // The launcher may use STARTF_USESHOWWINDOW/SW_HIDE for background launch.
        // A second explicit show makes this user-requested window visible as intended.
        ShowWindow(settings_,SW_SHOWNORMAL);ShowWindow(settings_,SW_SHOW);SetForegroundWindow(settings_);SetFocus(GetDlgItem(settings_,102));
        RECT rect{};GetWindowRect(settings_,&rect);
        Log(paths_,"Settings visible="+std::to_string(IsWindowVisible(settings_)!=FALSE)+", size="+std::to_string(rect.right-rect.left)+"x"+std::to_string(rect.bottom-rect.top));
    }
    void SaveSettings(){
        try{
            Config next=config_;next.reminders=SendDlgItemMessageW(settings_,101,BM_GETCHECK,0,0)==BST_CHECKED;
            auto time=Field(102);int h=0,m=0;wchar_t tail=0;
            if(time.size()!=5||time[2]!=L':'||swscanf_s(time.c_str(),L"%d:%d%c",&h,&m,&tail,1)!=2||h<0||h>23||m<0||m>59)
                throw std::runtime_error("提醒时间请填写为 HH:mm，例如 20:59。");
            auto number=[&](int id,int low,int high){auto text=Field(id);if(text.empty()||text.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("请填写有效数字。");int value=std::stoi(text);if(value<low||value>high)throw std::runtime_error("阈值应为 1–100，刷新间隔应为 15–3600 秒。");return value;};
            next.hour=h;next.minute=m;next.threshold=number(103,1,100);next.pollSeconds=number(104,15,3600);next.weekdays.clear();
            for(int i=0;i<7;++i)if(SendDlgItemMessageW(settings_,201+i,BM_GETCHECK,0,0)==BST_CHECKED)next.weekdays.insert(i);
            next.skipDates=ParseDates(Field(105));next.extraDates=ParseDates(Field(106));SaveConfig(paths_,next);config_=next;
            DestroyWindow(settings_);ArmSchedule();StartQuery();Log(paths_,"Settings saved");
        }catch(const std::exception& e){MessageBoxW(settings_,Wide(e.what()).c_str(),L"设置未保存",MB_OK|MB_ICONINFORMATION);}
    }
    LRESULT Message(UINT msg,WPARAM wp,LPARAM lp){
        if(msg==taskbarCreated_){trayAdded_=false;UpdateTray(true);return 0;}
        switch(msg){
        case TrayMessage:
            if(LOWORD(lp)==WM_CONTEXTMENU)Menu();
            else if(LOWORD(lp)==WM_LBUTTONDBLCLK||LOWORD(lp)==NIN_KEYSELECT)ShowPopup();
            return 0;
        case ShowDetailsMessage:ShowPopup();return 0;
        case TestNotificationMessage:Notify(true);return 0;
        case SettingsMessage:ShowSettings();return 0;
        case HideWindowsMessage:
            if(settings_)DestroyWindow(settings_);if(popup_)ShowWindow(popup_,SW_HIDE);KillTimer(window_,PopupTimer);ReleaseUnusedFonts();return 0;
        case RemoveMessage:Command(Cleanup);return 0;
        case StatusMessage:{
            RECT rect{};if(settings_)GetWindowRect(settings_,&rect);
            AtomicJson(paths_.data/L"status.json",{{"running",true},{"processId",GetCurrentProcessId()},{"trayRegistered",trayAdded_},
                {"batteryValid",result_.valid},{"batteryPercent",result_.percent},{"charging",result_.charging},{"receiverPresent",result_.receiverPresent},
                {"queryCount",queryCount_},{"lastSample",DateKey(result_.sampled)+" "+Utf8(ClockText(result_.sampled))},
                {"notificationBusy",notifying_},
                {"settingsVisible",settings_&&IsWindowVisible(settings_)},{"settingsBounds",{rect.left,rect.top,rect.right,rect.bottom}},
                {"reminderTime",config_.hour*60+config_.minute},{"threshold",config_.threshold},{"startupEnabled",config_.startup},
                {"lastCheckedDate",reminder_.lastCheckedDate},{"lastNotifiedDate",reminder_.lastNotifiedDate}});
            return 0;
        }
        case QueryDone:OnResult(std::unique_ptr<BatteryResult>(reinterpret_cast<BatteryResult*>(lp)));return 0;
        case NotificationDone:OnNotification(std::unique_ptr<NotificationResult>(reinterpret_cast<NotificationResult*>(lp)));return 0;
        case ReleaseFontsMessage:ReleaseUnusedFonts();return 0;
        case WM_TIMER:
            if(wp==PollTimer||wp==ScheduleTimer){KillTimer(window_,wp);StartQuery();if(wp==ScheduleTimer)ArmSchedule();}
            else if(wp==PopupTimer){POINT point;GetCursorPos(&point);RECT rect;GetWindowRect(popup_,&rect);if(PtInRect(&rect,point))SetTimer(window_,PopupTimer,1000,nullptr);else{ShowWindow(popup_,SW_HIDE);KillTimer(window_,PopupTimer);ReleaseUnusedFonts();}}
            return 0;
        case WM_DEVICECHANGE:if(wp==DBT_DEVICEARRIVAL||wp==DBT_DEVICEREMOVECOMPLETE)StartQuery(true);return 0;
        case WM_POWERBROADCAST:if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND){ArmSchedule();StartQuery(true);}return TRUE;
        case WM_TIMECHANGE:ArmSchedule();StartQuery();return 0;
        case WM_SETTINGCHANGE:UpdateTray(true);ArmSchedule();return 0;
        case WM_DPICHANGED:dpi_=HIWORD(wp);UpdateTray(true);return 0;
        case WM_QUERYENDSESSION:return TRUE;
        case WM_ENDSESSION:if(wp)DestroyWindow(window_);return 0;
        case WM_CLOSE:DestroyWindow(window_);return 0;
        case WM_DESTROY:
            closing_=true;SetEvent(stop_.get());for(UINT timer:{PollTimer,ScheduleTimer,PopupTimer})KillTimer(window_,timer);
            if(settings_)DestroyWindow(settings_);if(popup_)DestroyWindow(popup_);
            if(trayAdded_){auto data=TrayData();Shell_NotifyIconW(NIM_DELETE,&data);trayAdded_=false;}
            PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window_,msg,wp,lp);
    }
public:
    explicit App(Paths paths):paths_(std::move(paths)){}
    ~App(){
        SetEvent(stop_.get());if(worker_.joinable())worker_.join();if(notificationWorker_.joinable())notificationWorker_.join();
        MSG message;while(PeekMessageW(&message,window_,QueryDone,QueryDone,PM_REMOVE))delete reinterpret_cast<BatteryResult*>(message.lParam);
        while(PeekMessageW(&message,window_,NotificationDone,NotificationDone,PM_REMOVE))delete reinterpret_cast<NotificationResult*>(message.lParam);
        if(deviceNotify_)UnregisterDeviceNotification(deviceNotify_);
        if(icon_)DestroyIcon(icon_);if(classIcon_)DestroyIcon(classIcon_);for(HFONT font:{font_,popupFont_,bigFont_})if(font)DeleteObject(font);
    }
    int Run(bool details,bool test,bool settings){
        paths_.Ensure();config_=LoadConfig(paths_);
        try{reminder_=LoadState(paths_);}catch(const std::exception& e){Log(paths_,std::string("Reminder state reset: ")+e.what());}
        // Rebuild only our own external references after moving the portable folder.
        if(config_.startup)SetStartup(paths_,true);else if(StartupRegistered())SetStartup(paths_,false);
        dpi_=GetDpiForSystem();
        HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=MainProc;cls.lpszClassName=WindowClass;
        classIcon_=MakeIcon(32,100,true,false,false);
        cls.hIcon=classIcon_;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
        cls.lpfnWndProc=PopupProc;cls.lpszClassName=L"G7BatteryTray.Popup";cls.style=CS_DBLCLKS;RegisterClassExW(&cls);
        cls.lpfnWndProc=SettingsProc;cls.lpszClassName=L"G7BatteryTray.Settings";cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassExW(&cls);
        taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");
        window_=CreateWindowExW(WS_EX_TOOLWINDOW,WindowClass,AppName,WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,this);
        if(!window_)throw std::runtime_error("Cannot create tray event window");
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};filter.dbcc_size=sizeof(filter);filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;HidD_GetHidGuid(&filter.dbcc_classguid);
        deviceNotify_=RegisterDeviceNotificationW(window_,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
        result_.message=L"正在读取电量…";UpdateTray();
        worker_=std::thread([this]{Worker();});StartQuery();ArmSchedule();
        Log(paths_,"G7BatteryTray 1.1.0 started");
        if(details)PostMessageW(window_,ShowDetailsMessage,0,0);if(test)PostMessageW(window_,TestNotificationMessage,0,0);
        if(settings)PostMessageW(window_,SettingsMessage,0,0);
        MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
            if(settings_&&IsDialogMessageW(settings_,&message))continue;
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        return 0;
    }
};
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    using namespace g7;
    // This thread only edits numeric times, thresholds and ISO dates. Prevent
    // unnecessary IME/TSF initialization for our windows; other apps are untouched.
    ImmDisableIME(GetCurrentThreadId());
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    auto has=[&](const wchar_t* flag){for(int i=1;i<argc;++i)if(wcscmp(argv[i],flag)==0)return true;return false;};
    bool probe=has(L"--probe"),exit=has(L"--exit"),test=has(L"--test-notification"),details=has(L"--show-details"),settings=has(L"--settings"),background=has(L"--background"),remove=has(L"--unregister"),status=has(L"--status");
    std::wstring notificationCommand;int notificationPercent=-1;
    for(int i=1;i<argc;++i){
        std::wstring argument=argv[i];
        if(argument==L"--notification-register"||argument==L"--notification-test"||argument==L"--notification-unregister"||argument==L"--toast-activated")notificationCommand=argument;
        constexpr wchar_t prefix[]=L"--notification-battery=";
        if(argument.starts_with(prefix)){
            auto value=argument.substr(std::size(prefix)-1);
            if(value.empty()||value.size()>3||value.find_first_not_of(L"0123456789")!=std::wstring::npos){LocalFree(argv);return 2;}
            notificationPercent=std::stoi(value);notificationCommand=L"--notification-battery";
        }
    }
    LocalFree(argv);
    try{
        auto paths=Paths::Locate();
        if(!notificationCommand.empty()){paths.Ensure();return RunNotificationCommand(paths,notificationCommand,notificationPercent);}
        if(probe){
            paths.Ensure();Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));DeviceReader reader;auto result=reader.Query(stop.get());
            AtomicJson(paths.data/L"probe.json",{{"valid",result.valid},{"batteryPercent",result.percent},{"charging",result.charging},
                {"receiverPresent",result.receiverPresent},{"elapsedMs",result.elapsedMs},{"message",Utf8(result.message)},
                {"timestamp",DateKey(result.sampled)+" "+Utf8(ClockText(result.sampled))},{"response",result.response}});
            return result.valid?0:2;
        }
        HWND running=FindWindowW(WindowClass,nullptr);
        if(exit){if(running)PostMessageW(running,WM_CLOSE,0,0);return 0;}
        if(status){if(running)PostMessageW(running,StatusMessage,0,0);return running?0:3;}
        if(remove){
            if(running){PostMessageW(running,RemoveMessage,0,0);return 0;}
            paths.Ensure();auto config=LoadConfig(paths);config.startup=false;SetStartup(paths,false);SaveConfig(paths,config);
            Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));
            return RunNotificationHelper(paths,L"--notification-unregister",stop.get())?0:1;
        }
        Handle mutex(CreateMutexW(nullptr,FALSE,L"Local\\G7BatteryTray.Instance.v1"));
        if(GetLastError()==ERROR_ALREADY_EXISTS){if(running)PostMessageW(running,test?TestNotificationMessage:settings?SettingsMessage:background?HideWindowsMessage:ShowDetailsMessage,0,0);return 0;}
        if(!mutex.valid())throw std::runtime_error("Cannot create single-instance mutex");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        App app(std::move(paths));return app.Run(details,test,settings);
    }catch(const std::exception& e){
        if(!notificationCommand.empty())return 1;
        auto text=L"程序无法启动：\n"+Wide(e.what())+L"\n\n请确认整个文件夹可写，且 data\\config.json 格式正确。";
        MessageBoxW(nullptr,text.c_str(),L"G7BatteryTray",MB_OK|MB_ICONERROR);return 1;
    }
}
