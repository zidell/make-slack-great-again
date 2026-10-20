#include "screens/shell/settings.h"
#include "base/file.h"
#include "base/process.h"
#include "support/test.h"

TEST("fork font: nine pixels survives save and reload, and smaller values clamp") {
    const std::string path = file::join(base::homeDir(), "font-limits/settings.json");
    CHECK(shell::Settings::kFontPxMin == 9);
    shell::Settings settings;
    settings.fontSize = 9;
    settings.save(path);
    CHECK(shell::Settings::load(path).fontSize == 9);
    REQUIRE(file::writeAtomic(path, R"({"fontPx": 1})"));
    CHECK(shell::Settings::load(path).fontSize == 9);
}
