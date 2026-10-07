// The fork's conversation colours (channel_tint.h): the submenu above Leave,
// a pick kept per conversation and shown as the Text token while that
// conversation is open and on its sidebar name, "None" back to the theme's.
#include "app/fake/fake_backend.h"
#include "base/time.h"
#include "support/test.h"
#include "screens/shell/channel_tint.h"
#include "screens/shell/context_menus.h"
#include "screens/shell/settings.h"
#include "screens/shell/shell.h"

#ifdef MSGA_HAVE_MESSAGES
#include "app/screens/messages/image_cache.h"
#endif

#include <memory>

using namespace model;
using shell::ChannelTints;

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
    ConvRef                   conv(const char *id) const { return store.findConversation(id); }
    // The menu as it opens: chatItems plus the fork's submenu.
    std::vector<ui::MenuItem> menu(ConvRef c) {
        return sh->tints().withMenu(sh->menus().chatItems(c), c);
    }
};

std::string labels(const std::vector<ui::MenuItem> &items) {
    std::string out;
    for (const ui::MenuItem &it : items) {
        if (!out.empty())
            out += '|';
        out += it.separator ? std::string("-") : it.label;
        if (it.checked)
            out += "(on)";
    }
    return out;
}

const ui::MenuItem *colours(const std::vector<ui::MenuItem> &items) {
    for (const ui::MenuItem &m : items)
        if (!m.sub.empty())
            return &m;
    return nullptr;
}

} // namespace

TEST("channel tint: the submenu sits above Leave, or last in a DM's menu") {
    Harness    h;
    const auto eng = h.menu(h.conv("C0ENG"));
    CHECK_STR(
        labels(eng),
        "Star channel|-|Notify you about…|All new posts(on)|Just mentions|Mute and hide|-|"
        "Text color|-|Leave channel"
    );
    const ui::MenuItem &sub = eng[7];
    REQUIRE(sub.sub.size() == size_t(ChannelTints::kCount) + 2); // the colours, -, None
    for (int i = 0; i < ChannelTints::kCount; ++i) {
        CHECK(sub.sub[size_t(i)].swatch != 0);
        CHECK_FALSE(sub.sub[size_t(i)].checked);
    }
    CHECK(sub.sub.back().checked); // None
    CHECK_STR(labels(h.menu(h.conv("D0MIRA"))), "Star conversation|-|Mute|-|Text color");
}

TEST("channel tint: a pick is kept per conversation and colours its text while open") {
    Harness       h;
    const ConvRef design = h.conv("C0DESIGN"), eng = h.conv("C0ENG");
    REQUIRE(h.sh->current() == design);
    const ui::Color plain = ui::color(ui::C::MessageText);
    const ui::Color text  = ui::color(ui::C::Text);
    const bool      dark  = app().dark();
    CHECK(plain == text); // MessageText is Text unless a colour stands in

    h.sh->menus().run(ChannelTints::kFirstId + 3, design);
    CHECK(h.sh->tints().of(design) == 3);
    CHECK(h.sh->tints().of(eng) == -1);
    CHECK(ui::color(ui::C::MessageText) == ChannelTints::contentInk(3, dark));
    CHECK(ui::color(ui::C::ComposerIcon) == ChannelTints::composerIcon(3, dark, false));
    CHECK(ui::color(ui::C::ComposerIconActive) == ChannelTints::composerIcon(3, dark, true));
    CHECK(ui::color(ui::C::ComposerSend) == ChannelTints::sendFill(3, dark));
    CHECK(ui::color(ui::C::Accent) == ui::colorIn(ui::C::Accent, dark)); // elsewhere, the theme's
    CHECK(ui::colorIn(ui::C::MessageText, dark) == plain); // previews keep the theme's
    CHECK(ui::color(ui::C::Text) == text); // names, the header, the composer: the theme's
    REQUIRE(h.settings.channelTints.size() == 1);
    CHECK(colours(h.menu(design))->sub[3].checked);
    CHECK(bool(h.sh->sidebar().styleName)); // the sidebar names follow (styleName)

    h.sh->open(eng); // another conversation: the theme's
    pump();
    CHECK(ui::color(ui::C::MessageText) == plain);
    h.sh->menus().run(ChannelTints::kFirstId + 7, eng); // picked while open
    CHECK(ui::color(ui::C::MessageText) == ChannelTints::contentInk(7, dark));
    h.sh->open(design);
    pump();
    CHECK(ui::color(ui::C::MessageText) == ChannelTints::contentInk(3, dark));

    h.sh->menus().run(ChannelTints::kNoneId, design);
    CHECK(h.sh->tints().of(design) == -1);
    CHECK(ui::color(ui::C::MessageText) == plain);
    CHECK(ui::color(ui::C::ComposerIcon) == ui::colorIn(ui::C::ComposerIcon, dark));
    CHECK(ui::color(ui::C::ComposerSend) == ui::colorIn(ui::C::Accent, dark)); // the palette's
    REQUIRE(h.settings.channelTints.size() == 1);                              // eng's stays
}

TEST("channel tint: every colour reads at 4.5:1 on light and dark content") {
    for (bool dark : {false, true})
        for (int i = 0; i < ChannelTints::kCount; ++i) {
            const ui::Color ink = ChannelTints::contentInk(i, dark);
            CHECK(ChannelTints::contrast(ink, ui::colorIn(ui::C::Surface, dark)) >= 4.5);
        }
}

TEST("channel tint: the composer's icons read at 3:1 on its box, idle lighter than focused") {
    for (bool dark : {false, true})
        for (int i = 0; i < ChannelTints::kCount; ++i) {
            const ui::Color box  = ui::colorIn(ui::C::FormBg, dark);
            const ui::Color idle = ChannelTints::composerIcon(i, dark, false);
            const ui::Color on   = ChannelTints::composerIcon(i, dark, true);
            CHECK(ChannelTints::contrast(idle, box) >= 3.0);
            CHECK(ChannelTints::contrast(on, box) > ChannelTints::contrast(idle, box));
        }
}

TEST("channel tint: the send icon reads at 3:1 on every colour's fill") {
    for (bool dark : {false, true})
        for (int i = 0; i < ChannelTints::kCount; ++i)
            CHECK(
                ChannelTints::contrast(
                    ChannelTints::sendFill(i, dark), ui::colorIn(ui::C::AccentText, dark)
                ) >= 3.0
            );
}
