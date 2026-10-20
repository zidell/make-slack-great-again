#include "app/slack/browser_login.h"
#include "app/slack/fork_browser_import.h"
#include "app/model/jobs.h"
#include "base/file.h"
#include "base/process.h"
#include "plat/plat.h"
#include "support/test.h"

#include <sqlite3.h>
#include <cstdlib>
#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>

namespace {
struct Env {
    std::string key, value;
    bool        existed;
    Env(const char *name, const std::string &next)
        : key(name), value(base::env(name)), existed(std::getenv(name) != nullptr) {
        setenv(name, next.c_str(), 1);
    }
    ~Env() {
        if (existed)
            setenv(key.c_str(), value.c_str(), 1);
        else
            unsetenv(key.c_str());
    }
};
} // namespace

TEST("regular browser: reads committed WAL cookies from the selected profile only") {
    const std::string root    = file::join(base::homeDir(), "regular-chrome");
    const std::string profile = file::join(root, "Profile 2");
    REQUIRE(file::makeDirs(profile));
    REQUIRE(
        file::writeAtomic(
            file::join(root, "Local State"), R"({"profile":{"last_used":"Profile 2"}})"
        )
    );
    Env      override("CHROME_USER_DATA_DIR", root);
    sqlite3 *db = nullptr;
    REQUIRE(sqlite3_open(file::join(profile, "Cookies").c_str(), &db) == SQLITE_OK);
    REQUIRE(
        sqlite3_exec(
            db,
            "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0;"
            "CREATE TABLE cookies(host_key TEXT,name TEXT,value TEXT,encrypted_value BLOB);"
            "INSERT INTO cookies VALUES('.slack.com','d','xoxd-fake-regular-session',X'');"
            "INSERT INTO cookies VALUES('evilslack.com','d','xoxd-decoy',X'');"
            "INSERT INTO cookies VALUES('.slack.com','other','xoxd-decoy',X'');",
            nullptr,
            nullptr,
            nullptr
        ) == SQLITE_OK
    );
    const auto result = slack::importBrowserSession("/test/google-chrome");
    CHECK(result.ok());
    CHECK_STR(result.cookie, "xoxd-fake-regular-session");
    CHECK(slack::readBrowserCookies(file::join(profile, "Cookies")).rows.size() == 1);
    sqlite3_close(db);
    CHECK_STR(slack::browserProfileDirectory(root), profile);
    REQUIRE(
        file::writeAtomic(
            file::join(root, "Local State"), R"({"profile":{"last_used":"../outside"}})"
        )
    );
    CHECK_STR(slack::browserProfileDirectory(root), file::join(root, "Default"));
}

TEST("regular browser: normal Chrome arguments and cancellation leaves the browser alive") {
    const std::string root = file::join(base::homeDir(), "fake-regular-browser");
    const std::string exe  = file::join(root, "google-chrome");
    const std::string args = file::join(root, "args"), pidFile = file::join(root, "pid");
    REQUIRE(
        file::writeAtomic(
            exe,
            "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$MSGA_TEST_BROWSER_ARGS\"\n"
            "echo $$ > \"$MSGA_TEST_BROWSER_PID\"\nexec /bin/sleep 60\n"
        )
    );
    REQUIRE(chmod(exe.c_str(), 0700) == 0);
    Env  path("PATH", root + ":" + base::env("PATH"));
    Env  argsEnv("MSGA_TEST_BROWSER_ARGS", args), pidEnv("MSGA_TEST_BROWSER_PID", pidFile);
    Env  profile("CHROME_USER_DATA_DIR", file::join(root, "empty-profile"));
    Env  enabled("MSGA_BROWSER_LOGIN", "1");
    auto app = plat::App::create();
    REQUIRE(app != nullptr);
    net::Client client(*app);
    {
        slack::BrowserLogin login(*app, client);
        login.start({}, {});
        for (int i = 0; i < 100 && !file::exists(pidFile); ++i)
            app->pump(10);
    }
    model::waitBackground();
    app->pump(0); // discarded importer callbacks must not touch the deleted login
    std::string command, pidText;
    REQUIRE(file::readAll(args, &command));
    REQUIRE(file::readAll(pidFile, &pidText));
    const int pid = std::atoi(pidText.c_str());
    CHECK_STR(command, "--new-tab\nhttps://slack.com/signin\n");
    REQUIRE(pid > 0);
    CHECK(kill(pid, 0) == 0);
    kill(pid, SIGTERM); // only our fake sleep, never a user's browser
    waitpid(pid, nullptr, 0);
    CHECK(!file::exists(file::join(root, "empty-profile")));
}
