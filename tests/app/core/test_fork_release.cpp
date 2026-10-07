// The fork's releases (app/update/fork_release.h): the version numbers the
// updater compares and shows, and where it looks for them.
#include "app/update/fork_release.h"
#include "support/test.h"

TEST("fork release: one number per release, upstream's version first") {
    CHECK(forkrel::version(39) == 3900 + MSGA_FORK_RELEASE);
    CHECK(forkrel::version(40) > forkrel::version(39)); // the next upstream outranks
    CHECK_STR(forkrel::label(3902), "39.2");
    CHECK_STR(forkrel::label(3900), "39");
    CHECK_STR(forkrel::label(4011), "40.11");
}

TEST("fork release: the fork's latest GitHub release, msga's asset names") {
    // As scripts/fork-release.* upload them.
    CHECK_STR(
        std::string(forkrel::kReleases) + update::manifestFor("msga-macos-arm64.dmg"),
        "https://github.com/zidell/make-slack-great-again/releases/latest/download/"
        "msga-macos-arm64.manifest"
    );
}
