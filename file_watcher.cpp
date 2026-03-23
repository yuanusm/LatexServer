#include "file_watcher.h"

FileWatcher::FileWatcher(std::filesystem::path path) : path_(std::move(path)) {}

void FileWatcher::setPath(std::filesystem::path path) {
    path_ = std::move(path);
    initialized_ = false;
}

bool FileWatcher::hasChanged() {
    if (path_.empty() || !std::filesystem::exists(path_)) {
        return false;
    }

    const auto current = std::filesystem::last_write_time(path_);
    if (!initialized_) {
        lastWrite_ = current;
        initialized_ = true;
        return false;
    }

    if (current != lastWrite_) {
        lastWrite_ = current;
        return true;
    }
    return false;
}

void FileWatcher::reset() {
    initialized_ = false;
}
