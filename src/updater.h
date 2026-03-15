#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace tunngle {

enum class UpdateStatus {
    Idle,
    Checking,
    UpdateAvailable,
    Downloading,
    DownloadComplete,
    Error,
    UpToDate,
};

struct UpdateInfo {
    std::string version;
    std::string download_url;
    std::string local_path;
};

void UpdaterCheckAsync();
void UpdaterDownloadAsync();
void UpdaterInstallAndExit();
UpdateStatus UpdaterGetStatus();
std::string UpdaterGetError();
UpdateInfo UpdaterGetInfo();
float UpdaterGetProgress();

}  // namespace tunngle
