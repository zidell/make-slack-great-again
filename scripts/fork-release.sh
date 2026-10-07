#!/usr/bin/env bash
# Publishes this fork's macOS release to GitHub Releases
# (zidell/make-slack-great-again), where the fork's builds look for updates
# (src/app/update/fork_release.h). Windows: scripts\fork-release.ps1, run on
# the same commit, adds its .exe to the same release.
#
# The release is v<MSGA_VERSION>.<N>: HEAD already tagged → that release
# (the other platform's half), otherwise the next N on this MSGA_VERSION
# (1 after an upstream version bump), tagged and pushed here.
#
# Uploads msga-macos-arm64.dmg and msga-macos-arm64.manifest
# ({"version": MSGA_VERSION * 100 + N, "sha256": …}), the names the updater
# asks for. Signed with credentials.cmake's MSGA_CODESIGN_IDENTITY, not
# notarized: a first install from the browser needs "Open Anyway" once;
# updates the app downloads itself carry no quarantine.
#
# Usage: scripts/fork-release.sh    (master, clean, pushed to origin)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-fork-release"
DIST_DIR="${BUILD_DIR}/dist"
REPO="zidell/make-slack-great-again"
ASSET="msga-macos-arm64"
NPROC="$(sysctl -n hw.ncpu)"

die() { echo "fork-release: $*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]] || die "macOS on Apple silicon only"
cd "$PROJECT_ROOT"
[[ "$(git branch --show-current)" == "master" ]] || die "not on master"
[[ -z "$(git status --porcelain)" ]] || die "uncommitted changes"
git fetch -q origin master --tags
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/master)" ]] || die "HEAD is not origin/master (push first)"
grep -q 'MSGA_CODESIGN_IDENTITY "[^"-]' credentials.cmake 2>/dev/null ||
    die "credentials.cmake has no MSGA_CODESIGN_IDENTITY (Developer ID)"

BASE="$(sed -nE 's/^set\(MSGA_VERSION ([0-9]+)\).*/\1/p' version.cmake)"
[[ -n "$BASE" ]] || die "no MSGA_VERSION in version.cmake"

# The release: HEAD's tag, or the next one on this base.
TAG="$(git tag --points-at HEAD --list "v${BASE}.*" | sort -V | tail -1)"
if [[ -z "$TAG" ]]; then
    LAST="$(git tag --list "v${BASE}.*" | sed "s/^v${BASE}\.//" | sort -n | tail -1)"
    N=$(( ${LAST:-0} + 1 ))
    (( N < 100 )) || die "v${BASE}.${N}: the updater's number only has room for 99"
    TAG="v${BASE}.${N}"
    PREV="$(git tag --list 'v*.*' --sort=-v:refname | head -1)"
    git tag -a "$TAG" -m "msga ${BASE}.${N}"
    git push -q origin "$TAG"
    echo "tagged ${TAG}"
else
    N="${TAG#v${BASE}.}"
    PREV=""
fi
VERSION=$(( BASE * 100 + N ))

# The release flags of scripts/release.sh, plus the fork's release number and
# update checks. Reconfigured every time (cached options).
if [[ -d "$BUILD_DIR" && ! -f "${BUILD_DIR}/build.ninja" ]]; then
    rm -rf "$BUILD_DIR"
fi
COMPILERS=()
if [[ -z "${CC:-}${CXX:-}" ]] && command -v clang >/dev/null 2>&1; then
    COMPILERS=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
fi
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja "${COMPILERS[@]+"${COMPILERS[@]}"}" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DMSGA_DEMO=OFF \
    -DMSGA_BUILD_TESTS=OFF \
    -DMSGA_FORK_RELEASE="$N" \
    -DMSGA_UPDATES=ON
echo '*' >"${BUILD_DIR}/.gitignore" # all of it generated
cmake --build "$BUILD_DIR" --target msga --parallel "$NPROC"
codesign --verify --strict "${BUILD_DIR}/msga.app" || die "msga.app is not validly signed"

# The DMG: msga.app beside an Applications link, signed like the app.
IDENTITY="$(sed -nE 's/^set\(MSGA_CODESIGN_IDENTITY "([^"]+)"\).*/\1/p' credentials.cmake)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ditto "${BUILD_DIR}/msga.app" "${STAGE}/msga.app"
ln -s /Applications "${STAGE}/Applications"
mkdir -p "$DIST_DIR"
DMG="${DIST_DIR}/${ASSET}.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname msga -srcfolder "$STAGE" -format UDZO "$DMG"
codesign --force --sign "$IDENTITY" "$DMG"

SHA="$(shasum -a 256 "$DMG" | cut -d' ' -f1)"
MANIFEST="${DIST_DIR}/${ASSET}.manifest"
printf '{"version":%d,"sha256":"%s"}\n' "$VERSION" "$SHA" >"$MANIFEST"

if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    gh release upload "$TAG" "$DMG" "$MANIFEST" -R "$REPO" --clobber
else
    NOTES="$(git log --no-merges --format='- %s' ${PREV:+"${PREV}..${TAG}"} -n 50)"
    gh release create "$TAG" "$DMG" "$MANIFEST" -R "$REPO" --latest \
        --title "msga ${BASE}.${N}" --notes "$NOTES"
fi
echo "published ${TAG} (${VERSION}): ${ASSET}.dmg, ${ASSET}.manifest"
