// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The operator log, and Format, which builds a message the same printf way. A log line says what
// happened and what to do about it, in plain words. Lines go to a function the caller chooses:
// the program prints them to stderr with a UTC time, the tests keep them in a list.
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#ifdef _MSC_VER
#include <sal.h>
#define SCO_RELAY_FORMAT _Printf_format_string_
#define SCO_RELAY_PRINTF(a, b)
#else
#define SCO_RELAY_FORMAT
#define SCO_RELAY_PRINTF(a, b) __attribute__((format(printf, a, b)))
#endif

namespace sco::relay {

enum class LogLevel : uint8_t { Error, Warn, Info, Debug };   // each level also shows the ones before it

const char* LogLevelName(LogLevel level);
// "error", "warn", "info" or "debug"; false for anything else.
bool ParseLogLevel(std::string_view text, LogLevel* out);

class Logger {
public:
    using Sink = std::function<void(LogLevel level, const std::string& line)>;

    Logger(LogLevel level, Sink sink) : level_(level), sink_(std::move(sink)) {}
    bool Enabled(LogLevel level) const { return level <= level_; }
    // printf style. A line above the chosen level is dropped before it is formatted.
    void Log(LogLevel level, SCO_RELAY_FORMAT const char* format, ...) SCO_RELAY_PRINTF(3, 4);

private:
    LogLevel level_;
    Sink     sink_;
};

// The program's sink: "2026-10-12T14:03:22Z info  text" on stderr, one line at a time.
Logger::Sink StderrSink();

// printf into a std::string.
std::string Format(SCO_RELAY_FORMAT const char* format, ...) SCO_RELAY_PRINTF(1, 2);

// printf wants unsigned long long for %llu, and uint64_t is a different type on Linux.
inline unsigned long long Ull(uint64_t value) { return value; }

}  // namespace sco::relay
