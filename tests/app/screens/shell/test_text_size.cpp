// The fork's text-size spacing in the shell: Ctrl +/- (App::setUserTextScale)
// re-measures the sidebar rows (their rowH()) and the conversation tabs at
// once, not at the next start.
#include "app/fake/fake_backend.h"
#include "base/time.h"
#include "support/test.h"
#include "screens/shell/header.h"
#include "screens/shell/settings.h"
#include "screens/shell/shell.h"
#include "screens/shell/sidebar.h"

#ifdef MSGA_HAVE_MESSAGES
#include "app/screens/messages/image_cache.h"
#endif

#include <memory>

using namespace model;

namespace {

ui::App &app() {
    static std::unique_ptr<ui::App> a = [] {
        std::string err;
        auto        p = ui::App::create(&err);
        if (!p)
            std::fprintf(stderr, "ui::App::create: %s\n", err.c_str());
        return p;
    }();
    return *a;
}

void pump(int n = 10) {
    for (int i = 0; i < n; ++i)
        app().pump(2);
}

struct Harness {
    Store             store;
    fake::FakeBackend backend{store, app().platform()};
#ifdef MSGA_HAVE_MESSAGES
    screens::ImageCache images{app().platform()};
    screens::Context    ctx{app(), store, backend, images, {}, {}, {}, {}, {}};
#else
    alignas(16) char noCache[16]{};
    screens::Context ctx{
        app(), store, backend, *reinterpret_cast<screens::ImageCache *>(noCache), {}, {}, {}, {}, {}
    };
#endif
    shell::Settings               settings;
    std::unique_ptr<ui::Window>   win;
    std::unique_ptr<shell::Shell> sh;

    Harness() {
        backend.setFixture(MSGA_TEST_ASSETS, base::fromLocal(2026, 9, 21, 16, 0));
        bool done = false;
        backend.connect([&](bool ok, const std::string &) { done = ok; });
        for (int i = 0; i < 200 && !done; ++i)
            app().pump(5);
        plat::WindowDesc d;
        d.size        = {1200, 800};
        d.decorations = plat::Decorations::Custom;
        win           = std::make_unique<ui::Window>(d);
        sh            = std::make_unique<shell::Shell>(ctx, *win, settings, std::string());
        sh->open(store.findConversation("C0DESIGN"));
        pump();
    }
    ConvRef conv(const char *id) const { return store.findConversation(id); }
};

} // namespace

TEST("text size: the sidebar rows take the new height at once") {
    Harness       h;
    auto         &sb   = h.sh->sidebar();
    const ConvRef mira = h.conv("D0MIRA");
    REQUIRE(sb.rowView(mira) != nullptr);
    const float rowH = sb.rowView(mira)->windowRect().h;
    app().setUserTextScale(20.f / 15.f);
    pump();
    REQUIRE(sb.rowView(mira) != nullptr);
    CHECK(sb.rowView(mira)->windowRect().h > rowH);
    app().setUserTextScale(1.f);
    pump();
    REQUIRE(sb.rowView(mira) != nullptr);
    CHECK(sb.rowView(mira)->windowRect().h == rowH);
}

TEST("text size: the conversation tabs are measured again") {
    Harness     h;
    ui::View   *tab = h.sh->tabs().tab(0);
    const float w   = tab->style().w;
    app().setUserTextScale(26.f / 15.f);
    pump();
    CHECK(tab->style().w > w);
    app().setUserTextScale(1.f);
    pump();
    CHECK(tab->style().w == w);
}
