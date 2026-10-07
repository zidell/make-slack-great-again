// This fork's releases: GitHub Releases of zidell/make-slack-great-again,
// numbered <upstream MSGA_VERSION>.<MSGA_FORK_RELEASE> ("39.2") and published
// by scripts/fork-release.sh (macOS) / scripts/fork-release.ps1 (Windows).
// The updater compares one number, upstream * 100 + fork release, so a fork
// release outranks its base and the next upstream version outranks both.
//
// Only release builds (MSGA_UPDATES, set by those scripts) check: a local
// build would be replaced by the published one and lose what it has on top.
#pragma once

#ifdef MSGA_SELF_UPDATE
#include "app/update/updater.h"
#endif
#include "base/str.h"

#include <string>

#ifndef MSGA_FORK_RELEASE
#define MSGA_FORK_RELEASE 0
#endif

namespace forkrel {

constexpr const char *kReleases =
    "https://github.com/zidell/make-slack-great-again/releases/latest/download/";

// What the updater and the manifests compare.
constexpr int version(int upstream) {
    return upstream * 100 + MSGA_FORK_RELEASE;
}

// A version() as people read it: 3902 → "39.2", 3900 (a local build) → "39".
inline std::string label(int v) {
    const std::string base = str::number(int64_t(v / 100));
    return v % 100 ? base + "." + str::number(int64_t(v % 100)) : base;
}

#ifdef MSGA_SELF_UPDATE
// Points the updater at the fork's latest release; false for a build that
// doesn't check (a local one).
inline bool useReleases(update::Updater &u) {
#ifdef MSGA_UPDATES
    const std::string asset = update::asset();
    if (asset.empty())
        return false;
    u.setUrls(kReleases + update::manifestFor(asset), kReleases + asset);
    return true;
#else
    (void)u;
    return false;
#endif
}
#endif

} // namespace forkrel
