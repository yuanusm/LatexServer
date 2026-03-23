#pragma once

#include <filesystem>

class FileWatcher {
public:
    explicit FileWatcher(std::filesystem::path path = {});

    void setPath(std::filesystem::path path);
    bool hasChanged();
    void reset();

private:
    std::filesystem::path path_;
    std::filesystem::file_time_type lastWrite_{};
    bool initialized_ = false;
};
