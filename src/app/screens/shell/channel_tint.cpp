#include "screens/shell/channel_tint.h"

#include "base/i18n.h"
#include "screens/shell/context_menus.h"
#include "screens/shell/settings.h"
#include "screens/shell/sidebar.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using i18n::tr;
using model::ConvRef;
using model::kNoConv;
using ui::C;
using ui::Color;

namespace shell {

namespace {

// On light: deep shades that read like black text. On dark: soft ones
// that read like the default light grey.
constexpr struct {
    const char *name;
    Color       onLight, onDark;
} kColors[ChannelTints::kCount] = {
    {N_("Red"), 0xffc43c3c, 0xfff54141},
    {N_("Orange"), 0xffc0601a, 0xfff59741},
    {N_("Yellow"), 0xff9a7b00, 0xfff5cc41},
    {N_("Lime"), 0xff5f8a12, 0xffbbf541},
    {N_("Green"), 0xff22863a, 0xff41f577},
    {N_("Teal"), 0xff13877a, 0xff41f5db},
    {N_("Sky"), 0xff0e7fae, 0xff41b9f5},
    {N_("Blue"), 0xff2f5fd0, 0xff6e96f7},
    {N_("Indigo"), 0xff5145c8, 0xff786ef7},
    {N_("Purple"), 0xff8a3fbf, 0xffb141f5},
    {N_("Pink"), 0xffc2357f, 0xfff54197},
    {N_("Gray"), 0xff5d6670, 0xff8e99a8},
};

// The open conversation's colour, read by the colour hook (-1: none).
int                   g_index       = -1;
// The sidebar's names carry raw colours picked for its background: when that
// turns from light to dark or back (theme, palette), they are restyled.
int                   g_sidebarDark = -1;
std::function<void()> g_sidebarFlipped;

bool isDark(Color c) {
    const int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    return r * 299 + g * 587 + b * 114 < 128 * 1000;
}

// WCAG relative luminance and contrast ratio.
double luminance(Color c) {
    auto ch = [](int v) {
        const double x = v / 255.0;
        return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch((c >> 16) & 0xff) + 0.7152 * ch((c >> 8) & 0xff) + 0.0722 * ch(c & 0xff);
}

double contrastRatio(Color a, Color b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// `ink` with its HSL lightness moved away from `bg` until the two reach
// `kMin` (WCAG AA: 4.5:1 for body text, 3:1 for icons); hue and saturation stay.
Color readable(Color ink, Color bg, double kMin = 4.5) {
    if (contrastRatio(ink, bg) >= kMin)
        return ink;
    const double r = ((ink >> 16) & 0xff) / 255.0, g = ((ink >> 8) & 0xff) / 255.0,
                 b  = (ink & 0xff) / 255.0;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double       h = 0, s = 0, l = (mx + mn) / 2;
    if (mx > mn) {
        const double d = mx - mn;
        s              = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
        h /= 6;
    }
    auto hue = [](double p, double q, double t) {
        t += t < 0 ? 1 : t > 1 ? -1 : 0;
        return t < 1. / 6   ? p + (q - p) * 6 * t
               : t < 0.5    ? q
               : t < 2. / 3 ? p + (q - p) * (2. / 3 - t) * 6
                            : p;
    };
    auto rgb = [&](double ll) {
        const double q = ll < 0.5 ? ll * (1 + s) : ll + s - ll * s, p = 2 * ll - q;
        auto         v = [](double x) { return Color(std::lround(std::clamp(x, 0., 1.) * 255)); };
        return 0xff000000 | v(hue(p, q, h + 1. / 3)) << 16 | v(hue(p, q, h)) << 8 |
               v(hue(p, q, h - 1. / 3));
    };
    const double step = isDark(bg) ? 0.01 : -0.01;
    Color        out  = ink;
    for (double ll = l + step; ll >= 0 && ll <= 1 && contrastRatio(out, bg) < kMin; ll += step)
        out = rgb(ll);
    return out;
}

Color tintColor(C c, bool dark) {
    if (c == C::Sidebar) { // asked as the sidebar paints
        const int now = isDark(ui::colorIn(c, dark));
        if (g_sidebarDark >= 0 && now != g_sidebarDark && g_sidebarFlipped)
            if (ui::App *a = ui::App::instance())
                a->addTimer(0, false, [] {
                    if (g_sidebarFlipped)
                        g_sidebarFlipped();
                });
        g_sidebarDark = now;
        return 0;
    }
    if (g_index < 0)
        return 0;
    // Asked on every paint: kept per token, colour and mode.
    static Color cache[4][2][ChannelTints::kCount] = {};
    const int    k                                 = c == C::MessageText          ? 0
                                                     : c == C::ComposerIcon       ? 1
                                                     : c == C::ComposerIconActive ? 2
                                                     : c == C::ComposerSend       ? 3
                                                                                  : -1;
    if (k < 0)
        return 0;
    Color &v = cache[k][dark][g_index];
    if (!v)
        v = k == 0   ? ChannelTints::contentInk(g_index, dark)
            : k == 3 ? ChannelTints::sendFill(g_index, dark)
                     : ChannelTints::composerIcon(g_index, dark, k == 2);
    return v;
}

// `a` moved `t` of the way to `b`.
Color mix(Color a, Color b, double t) {
    Color out = 0xff000000;
    for (int s = 0; s < 24; s += 8)
        out |= Color(std::lround(((a >> s) & 0xff) * (1 - t) + ((b >> s) & 0xff) * t)) << s;
    return out;
}

std::string prefix(const std::string &key, const std::string &id) {
    return key + "/" + id + "=";
}

} // namespace

Color ChannelTints::text(int index, bool onDark) {
    return onDark ? kColors[index].onDark : kColors[index].onLight;
}

Color ChannelTints::contentInk(int index, bool dark) {
    return readable(text(index, dark), ui::colorIn(C::Surface, dark));
}

// As the theme's: the focused icons as strong as the text, the idle ones
// toned toward the box, both kept at 3:1 on it.
Color ChannelTints::composerIcon(int index, bool dark, bool focused) {
    const Color bg  = ui::colorIn(C::FormBg, dark);
    const Color ink = text(index, dark);
    return readable(focused ? ink : mix(ink, bg, 0.4), bg, 3.0);
}

// The deep shade in either mode, as the theme's accent: the send icon
// (AccentText) on it kept at 3:1.
Color ChannelTints::sendFill(int index, bool dark) {
    return readable(text(index, false), ui::colorIn(C::AccentText, dark), 3.0);
}

double ChannelTints::contrast(Color a, Color b) {
    return contrastRatio(a, b);
}

ChannelTints::ChannelTints(
    screens::Context     &ctx,
    ui::Window           &win,
    Sidebar              &sidebar,
    Settings             &settings,
    const std::string    &activeKey,
    std::function<void()> save
)
    : _ctx(ctx), _win(win), _sidebar(sidebar), _settings(settings), _activeKey(activeKey),
      _save(std::move(save)) {
    ui::setColorOverride(tintColor);
    _sidebar.styleName = [this](ConvRef c, ui::Label &name, bool unread) {
        styleName(c, name, unread);
    };
    g_sidebarFlipped = [this] {
        if (!_settings.channelTints.empty())
            _sidebar.rebuild();
    };
}

ChannelTints::~ChannelTints() {
    g_index = -1;
    ui::setColorOverride(nullptr);
    _sidebar.styleName = nullptr;
    g_sidebarFlipped   = nullptr;
    g_sidebarDark      = -1;
}

int ChannelTints::of(ConvRef c) const {
    if (c == kNoConv || c >= _ctx.store().conversationCount())
        return -1;
    const std::string p = prefix(_activeKey, _ctx.store().conversation(c).id);
    for (const std::string &e : _settings.channelTints)
        if (e.starts_with(p)) {
            const int i = std::atoi(e.c_str() + p.size());
            return i >= 0 && i < kCount ? i : -1;
        }
    return -1;
}

std::vector<ui::MenuItem> ChannelTints::withMenu(std::vector<ui::MenuItem> items, ConvRef c) const {
    if (items.empty())
        return items;
    const int    current = of(c);
    const bool   dark    = isDark(ui::color(C::MenuBg));
    ui::MenuItem sub;
    sub.label = tr("Text color");
    for (int i = 0; i < kCount; ++i) {
        ui::MenuItem m;
        m.id      = kFirstId + i;
        m.label   = tr(kColors[i].name);
        m.swatch  = text(i, dark);
        m.checked = i == current;
        sub.sub.push_back(std::move(m));
    }
    sub.sub.push_back(ui::MenuItem::separatorItem());
    ui::MenuItem none;
    none.id      = kNoneId;
    none.label   = tr("None");
    none.checked = current < 0;
    none.enabled = current >= 0;
    sub.sub.push_back(std::move(none));
    // Its own group above Leave, else last: the items above keep their
    // places (the original's keyboard paths and tests).
    auto leave = std::find_if(items.begin(), items.end(), [](const ui::MenuItem &m) {
        return m.id == Menus::kLeave;
    });
    if (leave != items.end() && leave != items.begin() && (leave - 1)->separator) {
        leave = items.insert(leave - 1, ui::MenuItem::separatorItem());
        items.insert(leave + 1, std::move(sub));
    } else {
        ui::addMenuSeparator(items);
        items.push_back(std::move(sub));
    }
    return items;
}

void ChannelTints::run(int id, ConvRef c) {
    if (!ownsId(id) || c == kNoConv || c >= _ctx.store().conversationCount())
        return;
    const std::string p = prefix(_activeKey, _ctx.store().conversation(c).id);
    std::erase_if(_settings.channelTints, [&](const std::string &e) { return e.starts_with(p); });
    if (id != kNoneId)
        _settings.channelTints.push_back(p + std::to_string(id - kFirstId));
    if (_save)
        _save();
    _sidebar.rebuild(); // the row's name
    if (c == _shown)
        apply(of(c));
}

void ChannelTints::show(ConvRef c) {
    _shown = c;
    apply(of(c));
}

void ChannelTints::apply(int index) {
    if (index == g_index)
        return;
    g_index = index;
    // Text resolves into the layouts as they build: restyle rebuilds them.
    if (ui::App *a = ui::App::instance())
        a->restyle();
}

// The row's own weight (bold when unread), in the colour: full when unread,
// a quarter toward the background when read, as the sidebar dims read rows;
// either kept readable on the sidebar's own background (which can be dark
// over light content).
void ChannelTints::styleName(ConvRef c, ui::Label &name, bool unread) const {
    const int i = of(c);
    if (i < 0)
        return;
    text::Style st  = ui::font(unread ? ui::Font::BodySemibold : ui::Font::Body);
    const Color bg  = ui::color(C::Sidebar);
    const Color ink = text(i, isDark(bg));
    st.color        = readable(unread ? ink : mix(ink, bg, 0.25), bg);
    text::AttributedText t;
    t.append(name.text(), st);
    name.setRichText(std::move(t));
}

} // namespace shell
