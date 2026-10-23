#pragma once
// Opt-in debug logging (frontend "输出调试日志" toggle). Appends timestamped
// lines to <exe dir>/debug/debug.log. Disabled by default and fully silent
// (a disabled log() is a no-op and never touches the filesystem).
#include <string>

namespace mra::debuglog {

void setEnabled(bool on);            // creates the debug dir + marker line
bool enabled();
void log(const std::string& line);   // timestamped append; no-op when off

}  // namespace mra::debuglog
