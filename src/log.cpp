#include "log.h"

#include <iostream>
#include <mutex>

namespace {

std::mutex gLogMutex;
LogComponent gComponent = LogComponent::SERVER;

const char* toText(LogComponent component) {
    switch (component) {
        case LogComponent::SERVER:
            return "SERVER";
        case LogComponent::CLIENT:
            return "CLIENT";
    }
    return "SERVER";
}

const char* toText(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
    }
    return "INFO";
}

}  // namespace

void setLogComponent(LogComponent component) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    gComponent = component;
}

void log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    std::cout << "[" << toText(gComponent) << "][" << toText(level) << "] " << message << '\n';
}

