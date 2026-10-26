#pragma once
#include "storage.hpp"

namespace g7 {
bool NotificationIdentityCurrent(const Paths& paths);
// The same executable performs COM/WinRT work in a short-lived helper process.
// No request files, resident helper, working-set trimming, or busy polling.
bool RunNotificationHelper(const Paths& paths,const std::wstring& argument,HANDLE stopEvent);
}
