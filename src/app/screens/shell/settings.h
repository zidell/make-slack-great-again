// The shell's persisted preferences: window geometry, the theme and the
// Settings dialog's choices (app/screens/settings), in
// <configDir>/settings.json (app/identity.h; tests and the demo recorder
// point HOME/XDG_CONFIG_HOME at a throwaway directory), written owner-only.
// The first start imports the settings of earlier versions (app/legacy).
// The secrets — API keys, the Slack app secret and token, the GIPHY key —
// are not in the file but in the secret store (base/secret.h), unless that
// store refuses them.
//
// Plain values are listed in tables in settings.cpp (JSON key → member), so
// a new setting is one line there and one member here.
#pragma once

#include "ui/ui.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shell {

// An AI provider (Settings → AI assistance). The two presets always exist
// (id "anthropic", "openai"); a preset is connected once it has a key.
// Custom OpenAI-compatible servers have an id "custom-N" and a URL.
struct AiProvider {
    std::string id, name, url, key, model, sttModel;
    bool        preset() const { return id == "anthropic" || id == "openai"; }
    bool        connected() const { return preset() ? !key.empty() : !url.empty(); }
};

struct Settings {
    Settings(); // the two AI presets

    int           width = 1200, height = 800; // logical
    bool          hasPosition = false;
    int           x = 0, y = 0;
    bool          maximized   = false;
    ui::ThemeMode theme       = ui::ThemeMode::System;
    int           threadWidth = 360;  // thread panel width, px
    bool          closeToTray = true; // the window's close hides it to the tray

    // ── Appearance ──────────────────────────────────────────────────────────
    int                      paletteLight = 0, paletteDark = 1; // ui::Palette per content mode
    ui::CustomPalette        custom;
    // The body text size in px (App::setUserTextScale, relative to Body's
    // 15 px); Settings and Cmd/Ctrl +/-/0 change it.
    static constexpr int     kFontPxMin = 13, kFontPxMax = 18, kFontPxDefault = 15;
    int                      fontSize = kFontPxDefault;
    std::string              language = "system"; // "system", "en", "ja"
    bool                     use24h = false, threadsInline = false, linkPreviews = true;
    // use24h came from the file; without it the clock follows the language
    // (Japanese convention is 24-hour).
    bool                     use24hSaved    = false;
    bool                     ctrlEnterSends = false, spellCheck = false;
    // The spell checker's languages (the spell backend's
    // codes, "en_US" / "en-US"); empty = the system language's.
    std::vector<std::string> spellLanguages;
    int                      relevantDays = 14; // sidebar: conversations active in the last N days
    // People's names: 0 as set in the service, 1 full names, 2 display names
    // (model::Backend::NamesMode).
    int                      names        = 0;
    bool                     showAgentsApps = true, unreadsOnly = false;
    bool                     animateEmoji = true, animateMedia = true;
    bool                     customTrayIcon = false;
    // Claude Code workspaces: the footer's zen toggle (hide the tool-call
    // cards), per workspace: the keys of the
    // workspaces with it on ("claude-code:local", auth::WorkspaceRecord::key).
    std::vector<std::string> zenWorkspaces;
    // The sidebar's collapsed sections, per workspace: "<key>=<mask>", one bit
    // per section kind (Sidebar::collapsedMask); a workspace not listed has
    // every section open.
    std::vector<std::string> collapsedSections;
    // The sidebar's own order, per workspace and section, dragged into place
    // on this device: "<key>#<section>=<conversation id>,<id>,…".
    std::vector<std::string> sidebarOrder;
    std::string              trayIconPath; // the picture for the custom tray icon

    // ── Composer state ──────────────────────────────────────────────────────
    std::vector<std::string> emojiRecent;       // picked emoji names, newest first
    int                      emojiSkinTone = 0; // 0 default, 2-6
    std::string              lastAttachDir;     // the attach chooser's last folder

    // ── Notifications ───────────────────────────────────────────────────────
    bool        notifications = true;
    int         notifyLevel   = 0; // 0 all new messages, 1 DMs and mentions only
    bool        notifyHuddles = true, boldMentionsOnly = true, notifySound = true;
    std::string soundId = "bundled:notify";

    // ── AI assistance ───────────────────────────────────────────────────────
    std::vector<AiProvider> ai;            // presets first
    std::string             aiDefault;     // the provider the assistant uses
    std::string             aiLanguage;    // "" = follow the app language
    std::string             voiceGlossary; // one term per line
    bool                    voiceCleanup = true;

    // ── Storage / System ────────────────────────────────────────────────────
    int         cacheLimitMb    = 250;
    bool        autoUpdates     = true;
    int64_t     lastUpdateCheck = 0; // unix seconds, 0 = never
    bool        minimizeToTray  = false;
    int         presence        = 0;    // 0 while running, 1 while using, 2 official apps
    bool        slackSession    = true; // false = app keys (OAuth + Socket Mode)
    std::string slackClientId, slackClientSecret, slackAppToken, giphyKey;
    // The sidebar's visit stamps: conversation id →
    // when it was last opened here (epoch secs); "Clear state" empties it.
    std::vector<std::pair<std::string, int64_t>> visitedAt;
    bool trayMonochrome = true; // the custom tray icon turned monochrome

    // ── Claude Code ─────────────────────────────────────────────────────────
    // The folder the last session started in (claudeCode/lastDir; "" = home),
    // the recently used ones, newest first (claudeCode/recentDirs, see
    // recent_folders.h), and each teammate's own pick (claudeCode/lastDir/<id>).
    struct RecentDir {
        std::string path;
        int64_t     usedAt = 0; // epoch seconds
    };
    std::string                                      claudeLastDir;
    std::vector<RecentDir>                           claudeRecentDirs;
    std::vector<std::pair<std::string, std::string>> claudeTeammateDirs; // role id → folder

    float                    fontScale() const { return float(fontSize) / float(kFontPxDefault); }
    // The palettes and custom palette into the toolkit (ui::setPalette …).
    void                     applyPalettes() const;
    bool                     zenMode(std::string_view workspaceKey) const;
    void                     setZenMode(const std::string &workspaceKey, bool on);
    uint8_t                  collapsedMask(std::string_view workspaceKey) const;
    void                     setCollapsedMask(const std::string &workspaceKey, uint8_t mask);
    std::vector<std::string> sidebarOrderOf(std::string_view workspaceKey, int section) const;
    void                     setSidebarOrder(
        const std::string &workspaceKey, int section, const std::vector<std::string> &ids
    );
    AiProvider        *provider(std::string_view id);
    const AiProvider  *provider(std::string_view id) const;
    // The language AI features answer in: the one picked, else the app
    // language's — the OS locale's language for "system", so "sv" for a
    // Swedish desktop.
    std::string        effectiveAiLanguage() const;
    // The GIPHY key searches use: the user's own, else the build's
    // (MSGA_GIPHY_KEY in credentials.cmake; "" when the build has none).
    std::string        effectiveGiphyKey() const;
    static std::string buildGiphyKey();

    // Missing or broken files leave the defaults.
    static Settings    load(const std::string &path);
    bool               save(const std::string &path) const; // atomic, owner-only, now
    // save() with the file written on a worker: the text is made here (the
    // secrets that changed go to the store first), and the writes to one
    // path land in call order, a later save() included: an older text never
    // replaces a newer one. A warning in the log when it fails.
    void               saveInBackground(plat::App &app, const std::string &path) const;
    // The file's text (the secrets that changed go to the store on the way).
    std::string        toFile() const;
    // <configDir>/settings.json, or "" when the OS gives no config dir.
    static std::string defaultPath(plat::App &app);
};

} // namespace shell
