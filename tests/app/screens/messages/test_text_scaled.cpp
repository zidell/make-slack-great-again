// The fork's text-size spacing (text_scaled.h): the message list's gaps and
// text rows' heights follow the text size, step by step.
#include "app/screens/messages/rows.h"
#include "app/screens/messages/text_scaled.h"
#include "support/test.h"
#include "ui/ui.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace screens;

namespace {
ui::App &app() {
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
} // namespace

TEST("text scaled: every Ctrl +/- step moves even the small gaps") {
    app();
    float prev = 0;
    for (int px = 9; px <= 32; ++px) {
        app().setUserTextScale(float(px) / 15.f);
        const float gap = textScaled(3);
        CHECK(gap > prev); // a whole-px rounding kept 3 px over several steps
        prev = gap;
    }
    app().setUserTextScale(1.f);
    CHECK(textScaled(8) == 8.f);
    constexpr Scaled pad{8};
    app().setUserTextScale(20.f / 15.f);
    CHECK(float(pad) > 10.f);
    app().setUserTextScale(1.f);
}

TEST("text scaled: a kept day row takes the new height on a restyle") {
    app();
    DayRow row;
    const float h = row.style().h;
    app().setUserTextScale(20.f / 15.f);
    row.styleChanged();
    CHECK(row.style().h > h);
    app().setUserTextScale(1.f);
    row.styleChanged();
    CHECK(row.style().h == h);
}
