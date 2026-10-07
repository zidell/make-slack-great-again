// The fork's conversation colours: twelve to pick from in a conversation's
// context menu ("Text color"), kept per workspace and conversation on this
// device. The open conversation's colour stands in for the MessageText token
// (ui::setColorOverride), which only message text uses (the conversation's
// and its threads', and what is typed in their composers), and for the
// composers' icons and lit send button (ComposerIcon, …Active, ComposerSend); the conversation's
// name in the sidebar takes it too. Each colour has a deep shade for light backgrounds and a soft
// one for dark, picked by what it sits on.
#pragma once

#include "screens/common/context.h"
#include "ui/ui.h"

#include <functional>
#include <string>
#include <vector>

namespace shell {

struct Settings;
class Sidebar;

class ChannelTints {
public:
    static constexpr int kCount   = 12;
    static constexpr int kFirstId = 80;                // menu ids: kFirstId + colour
    static constexpr int kNoneId  = kFirstId + kCount; // "None"
    static bool          ownsId(int id) { return id >= kFirstId && id <= kNoneId; }
    // Colour `index` as text on a dark or a light background.
    static ui::Color     text(int index, bool onDark);
    // What the open conversation's text shows: text() made readable (4.5:1)
    // on the content background of that mode.
    static ui::Color     contentInk(int index, bool dark);
    // The composer's icons (idle, or focused) in it, on the composer's box.
    static ui::Color     composerIcon(int index, bool dark, bool focused);
    // The lit send button's fill in it (under its AccentText icon).
    static ui::Color     sendFill(int index, bool dark);
    static double        contrast(ui::Color a, ui::Color b); // WCAG ratio

    // `activeKey`: the open workspace's key (auth::WorkspaceRecord::key);
    // `save` writes the settings soon. Hooks the sidebar's row names.
    ChannelTints(
        screens::Context     &ctx,
        ui::Window           &win,
        Sidebar              &sidebar,
        Settings             &settings,
        const std::string    &activeKey,
        std::function<void()> save
    );
    ~ChannelTints();

    int                       of(model::ConvRef c) const; // its colour, -1 for none
    // A conversation's menu (Menus::chatItems) with the "Text color"
    // submenu above Leave, the conversation's colour checked. Added as the
    // menu opens, so chatItems and its tests stay the original's.
    std::vector<ui::MenuItem> withMenu(std::vector<ui::MenuItem> items, model::ConvRef c) const;
    // A menu pick for `c`: kept, and shown at once.
    void                      run(int id, model::ConvRef c);
    // The conversation now open (kNoConv: none): its colour, or the theme's.
    void                      show(model::ConvRef c);

private:
    void apply(int index);
    void styleName(model::ConvRef c, ui::Label &name, bool unread) const;

    screens::Context     &_ctx;
    ui::Window           &_win;
    Sidebar              &_sidebar;
    Settings             &_settings;
    const std::string    &_activeKey;
    std::function<void()> _save;
    model::ConvRef        _shown = model::kNoConv;
};

} // namespace shell
