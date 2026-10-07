#include "base/log_file.h"

#include "base/file.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace base {

namespace {

// The daily files (setLogDir).
std::mutex        g_mutex; // setLogDir against the writers
constexpr int64_t kMaxDayBytes = 32ll << 20;
std::string       g_dir;
int               g_keepDays = 14;
int               g_day      = -1; // yyyymmdd of g_dayFile
FILE             *g_dayFile  = nullptr;
int64_t           g_dayBytes = 0;
bool              g_capped   = false;

std::string dayName(const CivilTime &t) {
    char name[32];
    std::snprintf(name, sizeof name, "msga-%04d-%02d-%02d.log", t.year, t.month, t.day);
    return name;
}

// Under g_mutex: the file for `t`'s day, and the old ones pruned.
void openDay(const CivilTime &t, int64_t secs) {
    if (g_dayFile)
        std::fclose(g_dayFile);
    g_day                              = t.year * 10000 + t.month * 100 + t.day;
    const std::string path             = file::join(g_dir, dayName(t));
    g_dayFile                          = std::fopen(path.c_str(), "a");
    g_dayBytes                         = std::max<int64_t>(0, file::size(path));
    g_capped                           = false;
    const std::string           oldest = dayName(localTime(secs - int64_t(g_keepDays) * 86400));
    std::vector<file::DirEntry> entries;
    if (file::listDir(g_dir, &entries))
        for (const file::DirEntry &e : entries)
            if (!e.isDir && e.name.size() == oldest.size() && e.name.rfind("msga-", 0) == 0 &&
                e.name < oldest)
                file::remove(file::join(g_dir, e.name));
}

} // namespace

bool setLogDir(const char *dir, int keepDays) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_dayFile)
        std::fclose(g_dayFile);
    g_dayFile  = nullptr;
    g_day      = -1;
    g_dir      = dir ? dir : "";
    g_keepDays = keepDays > 0 ? keepDays : 1;
    return g_dir.empty() || file::makeDirs(g_dir);
}

void logToDayFile(const CivilTime &t, int64_t secs, const char *line, size_t len) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_dir.empty()) {
        if (t.year * 10000 + t.month * 100 + t.day != g_day)
            openDay(t, secs);
        if (g_dayFile && !g_capped) {
            if (g_dayBytes + int64_t(len) > kMaxDayBytes) {
                g_capped = true;
                std::fputs("(log size cap reached: nothing more today)\n", g_dayFile);
            } else {
                std::fwrite(line, 1, len, g_dayFile);
                g_dayBytes += int64_t(len);
            }
            std::fflush(g_dayFile);
        }
    }
}

} // namespace base
