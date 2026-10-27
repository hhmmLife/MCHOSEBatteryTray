#pragma once
#include "common.hpp"
#include "../third_party/json.hpp"

namespace mchose {
using Json = nlohmann::json;
struct Paths {
    fs::path exe, root, data, logs;
    static Paths Locate();
    void Ensure() const;
};
void AtomicJson(const fs::path& path, const Json& json);
Config LoadConfig(const Paths& paths);
void SaveConfig(const Paths& paths, const Config& config);
ReminderState LoadState(const Paths& paths);
void SaveState(const Paths& paths, const ReminderState& state);
void Log(const Paths& paths, const std::string& message);
bool StartupRegistered();
void SetStartup(const Paths& paths, bool enabled);
bool DarkTheme();
}
