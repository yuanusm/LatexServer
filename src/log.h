#pragma once

#include <string>

enum class LogLevel {
    INFO,
    WARN,
    ERROR
};

enum class LogComponent {
    SERVER,
    CLIENT
};

void setLogComponent(LogComponent component);
void log(LogLevel level, const std::string& message);

