#pragma once

#include "app/slack/local_import.h"
#include "app/slack/sqlite_reader.h"

namespace slack {

// Linux's regular Chromium profile. Read-only: no debugging flags, profile
// copies, browser termination or writes to the browser's preferences.
LocalImport importBrowserSession(const std::string &executable);
LocalImport
importChromiumProfile(const std::string &directory, const char *application, bool browser);
SqliteTable readBrowserCookies(const std::string &path);
std::string browserProfileDirectory(const std::string &root);

} // namespace slack
