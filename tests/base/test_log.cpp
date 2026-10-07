#include "base/file.h"
#include "base/log.h"
#include "base/log_file.h"
#include "base/time.h"
#include "support/test.h"

#include <string>

TEST("log: setLogDir writes a dated file per day and prunes the old ones") {
    const std::string dir = base::test::makeTempDir("msga_log_test_");
    REQUIRE(!dir.empty());
    // Files from the past: one inside the window, one past it, and an
    // unrelated file the pruning leaves alone.
    const int64_t now  = base::nowMicros() / 1000000;
    auto          name = [](int64_t secs) {
        const base::CivilTime t = base::localTime(secs);
        char                  buf[32];
        std::snprintf(buf, sizeof buf, "msga-%04d-%02d-%02d.log", t.year, t.month, t.day);
        return std::string(buf);
    };
    const std::string recent = name(now - 2 * 86400), stale = name(now - 30 * 86400);
    REQUIRE(file::writeAtomic(file::join(dir, recent), "old\n"));
    REQUIRE(file::writeAtomic(file::join(dir, stale), "old\n"));
    REQUIRE(file::writeAtomic(file::join(dir, "notes.txt"), "keep\n"));

    base::setLogToStderr(false);
    REQUIRE(base::setLogDir(dir.c_str(), 14));
    LOG_INFO("test", "hello %d", 42);
    LOG_DEBUG("test", "below the level: not written");
    base::setLogDir(nullptr, 14);
    base::setLogToStderr(true);

    std::string today;
    REQUIRE(file::readAll(file::join(dir, name(now)), &today));
    // "YYYY-MM-DD HH:MM:SS.mmm I test: hello 42"
    const base::CivilTime t = base::localTime(now);
    char                  date[16];
    std::snprintf(date, sizeof date, "%04d-%02d-%02d ", t.year, t.month, t.day);
    CHECK(today.rfind(date, 0) == 0);
    CHECK(today.find(" I test: hello 42\n") != std::string::npos);
    CHECK(today.find("below the level") == std::string::npos);
    CHECK(file::exists(file::join(dir, recent)));
    CHECK_FALSE(file::exists(file::join(dir, stale)));
    CHECK(file::exists(file::join(dir, "notes.txt")));
}
