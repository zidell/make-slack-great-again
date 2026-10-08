// The document viewer (app/screens/messages/doc_viewer.h): which files it
// reads in place, and a Markdown document as headings and bodies.
#include "app/screens/messages/doc_viewer.h"
#include "app/screens/messages/message_dialogs.h"
#include "plat/testing.h"
#include "support/test.h"
#include "ui/ui.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

// The suite's own app (one per process: another suite's, when it made one).
ui::App &app() {
    if (ui::App *a = ui::App::instance())
        return *a;
    static std::unique_ptr<ui::App> a = [] {
        std::string err;
        auto        p = ui::App::create(&err);
        if (!p) {
            std::fprintf(stderr, "ui::App::create: %s\n", err.c_str());
            std::abort();
        }
        return p;
    }();
    return *a;
}

model::File file(std::string name, std::string mime) {
    model::File f;
    f.name = std::move(name);
    f.mime = std::move(mime);
    return f;
}

} // namespace

TEST("doc viewer: Markdown and plain text files, nothing else") {
    CHECK(screens::isDocFile(file("CMS_분석.md", "text/plain")));
    CHECK(screens::isDocFile(file("README.MARKDOWN", "")));
    CHECK(screens::isDocFile(file("notes", "text/markdown")));
    CHECK(screens::isDocFile(file("log.txt", "text/plain")));
    CHECK(screens::isDocFile(file("snippet", "text/plain")));
    CHECK_FALSE(screens::isDocFile(file("data.csv", "text/csv")));
    CHECK_FALSE(screens::isDocFile(file("page.html", "text/html")));
    CHECK_FALSE(screens::isDocFile(file("main.cpp", "text/plain"))); // code: its own extension
    CHECK_FALSE(screens::isDocFile(file("report.pdf", "application/pdf")));
}

TEST("doc viewer: headings split the bodies, fenced code keeps its #") {
    const auto parts = screens::splitMarkdownDoc(
        "# Title #\r\nintro line\n\n## Part\n| a | b |\n|---|---|\n| 1 | 2 |\n"
        "```\n# not a heading\n```\n####### seven\n"
    );
    CHECK(parts.size() == 4);
    CHECK(parts[0].heading == 1);
    CHECK_STR(parts[0].text, "Title");
    CHECK(parts[1].heading == 0);
    CHECK_STR(parts[1].text, "intro line");
    CHECK(parts[2].heading == 2);
    CHECK_STR(parts[2].text, "Part");
    CHECK(parts[3].heading == 0);
    CHECK_STR(
        parts[3].text, "| a | b |\n|---|---|\n| 1 | 2 |\n```\n# not a heading\n```\n####### seven"
    );
}

TEST("doc viewer: a click on the full table closes it, as a click opened it") {
    app(); // before the window
    plat::WindowDesc d;
    d.size   = {800, 600};
    auto win = std::make_unique<ui::Window>(d);
    REQUIRE(screens::showTableViewer(*win, {{"a", "b"}, {"1", "2"}}) != nullptr);
    for (int i = 0; i < 4; ++i)
        app().pump(1);
    REQUIRE(win->topPopup() != nullptr);
    auto *h = app().platform().testHooks();
    h->injectPointerMove(win->native(), {400, 300}); // the table's card, centred
    h->injectButton(win->native(), plat::Button::Left, true);
    h->injectButton(win->native(), plat::Button::Left, false);
    for (int i = 0; i < 4; ++i)
        app().pump(1);
    CHECK(win->topPopup() == nullptr);
}
