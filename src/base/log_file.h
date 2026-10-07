// The log's daily files (the fork's diagnostic trail; base/log.h writes
// every line here too once setLogDir was called).
#pragma once

#include "base/time.h"

#include <cstddef>
#include <cstdint>

namespace base {

// Also append every line to a file per local day in `dir` (created if
// missing): msga-YYYY-MM-DD.log, a new one at midnight, files older than
// keepDays removed, each capped at 32 MB (one notice, then nothing more that
// day). The app's diagnostic trail: lines carry the date and milliseconds.
// Returns false when the directory cannot be made.
bool setLogDir(const char *dir, int keepDays);

// log.cpp's: one finished line at local time `t` (`secs` since the epoch).
void logToDayFile(const CivilTime &t, int64_t secs, const char *line, size_t len);

} // namespace base
