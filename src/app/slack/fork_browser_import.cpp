#include "app/slack/fork_browser_import.h"

#include "base/file.h"
#include "base/json.h"
#include "base/process.h"
#include "base/str.h"

#include <sqlite3.h>

namespace slack {

SqliteTable readBrowserCookies(const std::string &path) {
    SqliteTable result;
    sqlite3    *db = nullptr;
    // SQLite's read transaction provides a coherent snapshot even when Chrome
    // has uncheckpointed WAL frames. Never request a writable connection.
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        result.error = SqliteTable::Error::Open;
        return result;
    }
    sqlite3_busy_timeout(db, 200);
    sqlite3_stmt *statement = nullptr;
    const int     prepared  = sqlite3_prepare_v2(
        db,
        "SELECT host_key,name,value,encrypted_value FROM cookies "
        "WHERE name='d' AND host_key IN ('.slack.com','slack.com')",
        -1,
        &statement,
        nullptr
    );
    if (prepared != SQLITE_OK) {
        if (statement)
            sqlite3_finalize(statement);
        sqlite3_close(db);
        result.error = SqliteTable::Error::Corrupt;
        return result;
    }
    result.columns = {"host_key", "name", "value", "encrypted_value"};
    int status     = SQLITE_OK;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
        std::vector<Cell> row;
        for (int i = 0; i < 4; ++i) {
            const void *value = sqlite3_column_blob(statement, i);
            const int   bytes = sqlite3_column_bytes(statement, i);
            Cell        cell;
            cell.v      = value ? std::string(static_cast<const char *>(value), size_t(bytes))
                                : std::string();
            cell.isBlob = i == 3;
            row.push_back(std::move(cell));
        }
        result.rows.push_back(std::move(row));
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    if (status != SQLITE_DONE) {
        result.rows.clear();
        result.error = status == SQLITE_BUSY || status == SQLITE_LOCKED
                           ? SqliteTable::Error::Busy
                           : SqliteTable::Error::Corrupt;
    }
    return result;
}

std::string browserProfileDirectory(const std::string &root) {
    std::string    profile = "Default", state;
    json::Document doc;
    if (file::readAll(file::join(root, "Local State"), &state) && doc.parse(state, nullptr)) {
        const std::string last(doc.root()["profile"]["last_used"].str());
        // A profile name is one child directory, never a path supplied by a URL.
        if (!last.empty() && last != "." && last != ".." &&
            last.find_first_of("/\\") == std::string::npos && last.find('\0') == std::string::npos)
            profile = last;
    }
    return file::join(root, profile);
}

LocalImport importBrowserSession(const std::string &executable) {
    const std::string name(file::baseName(executable));
    std::string       config = base::env("CHROME_CONFIG_HOME");
    if (config.empty())
        config = base::env("XDG_CONFIG_HOME");
    if (config.empty())
        config = file::join(base::homeDir(), ".config");
    std::string directory = "google-chrome", application = "chrome";
    if (name.find("chromium") != std::string::npos) {
        directory   = "chromium";
        application = "chromium";
    } else if (name.find("brave") != std::string::npos) {
        directory   = "BraveSoftware/Brave-Browser";
        application = "brave";
    } else if (name.find("edge") != std::string::npos) {
        directory   = "microsoft-edge";
        application = "microsoft-edge";
    } else if (name.find("vivaldi") != std::string::npos) {
        directory   = "vivaldi";
        application = "vivaldi";
    }
    std::string root = base::env("CHROME_USER_DATA_DIR");
    if (root.empty())
        root = file::join(config, directory);
    return importChromiumProfile(browserProfileDirectory(root), application.c_str(), true);
}

} // namespace slack
