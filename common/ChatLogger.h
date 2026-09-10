#pragma once

#include "Logger.h"

#include <initializer_list>
#include <ostream>
#include <sstream>
#include <string>

namespace chat::observability {

using Field = logsystem::LogField;
using Level = logsystem::LogLevel;

void initialize(const std::string& service_name);
bool enabled(Level level);
void log(Level level, const std::string& event, const std::string& message,
         std::initializer_list<Field> fields = {});
void flush();
void shutdown();
std::uint64_t droppedCount();
std::string newRequestId();

// Transitional stream adapter for legacy server diagnostics.  It lets old
// insertion expressions become one JSONL record while call sites are migrated
// to named events and structured fields.
class LogStream {
  public:
    LogStream(Level level, std::string event);
    ~LogStream();
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStream(LogStream&& other) noexcept;

    template <typename T> LogStream& operator<<(const T& value) {
        if (active_) buffer_ << value;
        return *this;
    }
    LogStream& operator<<(std::ostream& (*manipulator)(std::ostream&));

  private:
    Level level_;
    std::string event_;
    std::ostringstream buffer_;
    bool active_ = true;
};

LogStream stream(Level level, std::string event = "legacy.output");

} // namespace chat::observability
