#pragma once

#include "plat/plat.h"

#include <string_view>

// Fork: no menu bar (NSStatusItem) icon on macOS. The Dock icon already
// carries the unread badge and brings the window back (a Dock click is a
// reopen -> InstanceActivated -> restore), so the window still hides on close
// instead of quitting. Other backends (and the headless tests) keep the tray.
namespace shell::fork {
inline bool noMenuBarTray(const plat::App &app) {
    return std::string_view(app.backendName()) == "cocoa";
}
} // namespace shell::fork
