#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace fpv {
struct UpdateStatus {
    bool busy{}, available{}, compatible{};
    std::string version, notes, sha256;
    std::wstring releaseUrl;
    std::filesystem::path downloaded;
    std::string message = "Update checker ready.";
    unsigned long long bytes{};
};

// Networking and verification stay on one owned worker, never the game/render thread.
class Updater {
public:
    ~Updater() { Close(); }
    void Start(bool automatic, bool preview);
    void Check(bool preview);
    void Download();
    void Close();
    UpdateStatus Status() const;
private:
    void Launch(bool download, bool preview, bool automatic);
    void Work(bool download, bool preview, bool automatic);
    mutable std::mutex mutex_;
    UpdateStatus state_;
    std::thread worker_;
    std::atomic<bool> stopping_{};
};
}
