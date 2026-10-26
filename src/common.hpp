#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000002
#include <windows.h>
#include <string>
#include <filesystem>
#include <utility>
#include "model.hpp"

namespace g7 {
namespace fs = std::filesystem;
inline constexpr wchar_t AppName[] = L"G7BatteryTray";
inline constexpr wchar_t AppId[] = L"G7BatteryTray.Desktop";
inline constexpr wchar_t WindowClass[] = L"G7BatteryTray.Main.v1";
inline constexpr UINT ShowDetailsMessage = WM_APP + 1;
inline std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count) return L"[无效 UTF-8]";
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), count);
    return out;
}
inline std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}
inline LocalTime Now() {
    SYSTEMTIME t; GetLocalTime(&t);
    return {t.wYear,t.wMonth,t.wDay,t.wDayOfWeek,t.wHour,t.wMinute,t.wSecond};
}
inline std::wstring ClockText(const LocalTime& t) {
    wchar_t text[24]; swprintf_s(text, L"%02d:%02d:%02d",t.hour,t.minute,t.second); return text;
}
inline std::wstring ErrorText(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::wstring result = text ? text : L"Windows 错误";
    if (text) LocalFree(text);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result + L" (" + std::to_wstring(code) + L")";
}
class Handle {
    HANDLE value_ = INVALID_HANDLE_VALUE;
public:
    Handle() = default;
    explicit Handle(HANDLE value):value_(value){}
    ~Handle(){reset();}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept: value_(std::exchange(other.value_,INVALID_HANDLE_VALUE)){}
    void reset(HANDLE value = INVALID_HANDLE_VALUE){if(valid())CloseHandle(value_);value_=value;}
    bool valid() const {return value_ && value_ != INVALID_HANDLE_VALUE;}
    HANDLE get() const {return value_;}
};
}
