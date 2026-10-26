#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>

namespace g7 {
struct Config {
    bool reminders = true;
    bool startup = false;
    int hour = 20, minute = 59, threshold = 25, pollSeconds = 60;
    std::set<int> weekdays{1, 2, 3, 4, 5}; // Sunday = 0.
    std::set<std::string> skipDates, extraDates;
};
struct ReminderState {
    std::string lastCheckedDate, lastNotifiedDate;
};
struct LocalTime {
    int year, month, day, weekday, hour, minute, second;
};
inline std::string DateKey(const LocalTime& t) {
    char text[16];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", t.year, t.month, t.day);
    return text;
}
inline bool ValidDate(const std::string& value) {
    int y = 0, m = 0, d = 0; char tail = 0;
    if (value.size() != 10 || value[4] != '-' || value[7] != '-' ||
        std::sscanf(value.c_str(), "%d-%d-%d%c", &y, &m, &d, &tail) != 3) return false;
    auto date = std::chrono::year(y) / std::chrono::month(m) / std::chrono::day(d);
    return y >= 2000 && y <= 9999 && date.ok() && DateKey({y,m,d,0,0,0,0}) == value;
}
inline bool Workday(const Config& c, const LocalTime& t) {
    auto key = DateKey(t);
    if (c.skipDates.contains(key)) return false;
    return c.extraDates.contains(key) || c.weekdays.contains(t.weekday);
}
inline bool ReminderDue(const Config& c, const ReminderState& s, const LocalTime& t) {
    auto key = DateKey(t);
    return c.reminders && Workday(c, t) && t.hour * 60 + t.minute >= c.hour * 60 + c.minute &&
           s.lastCheckedDate != key && s.lastNotifiedDate != key;
}
inline bool NeedsCharge(const Config& c, int percent, bool charging) {
    return percent >= 0 && percent <= 100 && percent < c.threshold && !charging;
}
// Long waits are recalculated on resume/time-change; no high resolution timer is used.
inline unsigned NextReminderSeconds(const Config& c, const ReminderState& s, LocalTime t) {
    if (!c.reminders) return 86400;
    if (ReminderDue(c, s, t)) return 60; // Retry unreadable/sleeping devices slowly.
    auto today = std::chrono::sys_days(std::chrono::year(t.year)/std::chrono::month(t.month)/std::chrono::day(t.day));
    int elapsed = t.hour * 3600 + t.minute * 60 + t.second;
    for (int offset = 0; offset <= 370; ++offset) {
        auto date = std::chrono::year_month_day(today + std::chrono::days(offset));
        LocalTime next{int(date.year()), int(unsigned(date.month())), int(unsigned(date.day())),
                       (t.weekday + offset) % 7, c.hour, c.minute, 0};
        int delay = offset * 86400 + c.hour * 3600 + c.minute * 60 - elapsed;
        if (delay > 0 && Workday(c, next) && s.lastCheckedDate != DateKey(next) && s.lastNotifiedDate != DateKey(next))
            return static_cast<unsigned>(delay);
    }
    return 86400;
}
}
