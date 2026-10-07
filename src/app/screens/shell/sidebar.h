// The conversation sidebar:
// uniform 30 px rows under a 6 px top inset — the "Threads", "Saved
// messages" and "Scheduled messages" entries, then the Starred / Channels /
// Direct messages / Agents & apps sections (a click on a header collapses
// it, hiding every row; hovering shows the chevron that says what a click
// does; the Direct messages header has a "+" on hover), "N more channels"
// for the ones outside the relevant-days window, "Add channels", and the
// footer (avatar + menu, presence toggle). Over the nav gradient.
//
// An agent workspace (Capabilities::agentSessions, Claude Code) calls the
// direct messages "Sessions" and ends them with "Add sessions" (its "+" and
// that row open the find-or-create menu), and lists the team
// (Backend::agentRoles) in a "Team" section under them: a teammate's row
// opens its page, its dot is its user's presence, its "+" adds a teammate.
//
// Rows are live: a Store observer restyles a row when its counts change —
// semibold for unread, a red count for DM unreads and channel mentions, a
// blue dot for other unread in an "All new posts" channel; muted
// conversations and ones quiet for over 30 days show no badge.
#pragma once

#include "screens/common/context.h"
#include "screens/shell/avatars.h"
#include "ui/ui.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace shell {

// The notification window: a message older than this never notifies, and a
// conversation idle for longer neither badges nor counts.
inline constexpr int64_t kMaxNotifyAgeSecs = 30 * 86400; // 30 days

// A workspace's share of the tray dot, the launcher badge and its rail
// tile's dot: `important` = unread direct
// messages + mentions (an agent workspace: only what needs you, its
// answers), `unread` = other unread activity in channels on "All new posts"
// (`fallback`: the global level). Muted, "Nothing", left and closed
// conversations, and ones idle past the notification window, count for
// neither.
struct Attention {
    int  important = 0;
    bool unread    = false;
    int  dot() const { return important > 0 ? 2 : unread ? 1 : 0; } // 0, 1 blue, 2 red
    bool operator==(const Attention &) const = default;
};
Attention
workspaceAttention(const model::Store &store, model::NotifyLevel fallback, int64_t nowSecs);

// The liveness filter, shared by the sidebar and the
// quick switcher: a DM outlives its peer (the service never prunes it), so
// one whose peer was deactivated or never resolves to a name is not listed.
// A peer not loaded yet (a placeholder) is let through.
bool deadDm(const model::Store &store, const model::Conversation &c);

class ConvRow;
class Menus;
class SectionHeader;
class SidebarFooter;
class TeammateRow;

class Sidebar : public ui::View {
public:
    Sidebar(screens::Context &ctx, Avatars &avatars);
    ~Sidebar() override;

    // The Settings → Appearance / Notifications choices the list follows.
    struct Filters {
        int                relevantDays   = 14;
        bool               showAgentsApps = true, unreadsOnly = false;
        bool               highlightMentionsOnly = true; // "Highlight mentions-only channels…"
        model::NotifyLevel defaultLevel          = model::NotifyLevel::All;
        bool               operator==(const Filters &) const = default;
    };
    void setFilters(const Filters &f);
    // Visit stamps: when each conversation was last opened here (or
    // seen unread), which keeps it listed for the relevant days. The shell
    // persists it (Settings::visitedAt) whenever onVisitedChanged fires.
    using VisitStamps = std::unordered_map<std::string, int64_t>; // conv id → epoch secs

    void                  setVisited(VisitStamps stamps);
    const VisitStamps    &visited() const { return _visited; }
    void                  clearVisited(); // Settings → "Clear state"
    std::function<void()> onVisitedChanged;

    void                        rebuild();                   // from the Store (roster changed)
    void                        select(model::ConvRef conv); // highlight only (kNoConv clears)
    model::ConvRef              selected() const { return _selected; }
    // Conversations in on-screen order (the quick switcher's empty query).
    std::vector<model::ConvRef> order() const;

    // What a row shows (tests, and the tray/badge code).
    struct RowState {
        bool exists = false, visible = false, bold = false, dot = false, selected = false;
        int  badge  = 0;
        int  huddle = 0; // a live huddle's pill: its participant count (1 when none listed)
    };
    RowState                 rowState(model::ConvRef conv) const;
    // Tests: how many times the rows were rebuilt, and a row's identity (the
    // view object; null: no row), to prove a change restyled in place.
    int                      rebuildCount() const { return _rebuilds; }
    const ui::View          *rowView(model::ConvRef conv) const;
    // Sum the app badge shows: mentions + unread direct messages.
    int                      attentionCount() const;
    // The "N more channels" row's N (0: no such row).
    int                      hiddenChannels() const { return _hiddenChannels; }
    // Titles top to bottom: the visible nav entries, then the sections.
    std::vector<std::string> sectionTitles() const;

    // ── The fork's order and folds (sidebar_order_impl.h) ──
    // The collapsed sections, a bit per kind (1 starred, 2 channels, 4 DMs /
    // sessions, 8 apps, 16 team). The shell keeps them per workspace: it sets
    // the open workspace's, and a header click reports the new mask.
    uint8_t                           collapsedMask() const;
    void                              setCollapsedMask(uint8_t mask);
    std::function<void(uint8_t mask)> onCollapsedChanged;
    // Each section's order (0 Starred, 1 Channels, 2 DMs / Sessions, 3 Agents
    // & apps): the ids listed first, in that order, then the rest — A to Z by
    // name in Starred and Channels (as Slack sorts them), the server's order
    // in the others. Dragging a row reorders its section and reports the
    // section's new order; the shell keeps them per workspace, on this device.
    void setOrder(int section, std::vector<std::string> ids); // rebuilds
    std::function<void(int section, const std::vector<std::string> &ids)> onOrderChanged;
    // Moves a conversation's row to `index` among its section's shown rows,
    // as a drop there does (tests). False: no such row.
    bool                        moveRow(model::ConvRef conv, int index);
    // Option+Up/Down: the conversation shown above / below the open one
    // (unreadOnly: the nearest unread one); kNoConv at the end. With none
    // open, from the top (down) or the bottom (up).
    model::ConvRef              adjacentConversation(int dir, bool unreadOnly) const;
    // Tests: the conversations listed, top to bottom (collapsed ones left out).
    std::vector<model::ConvRef> shownConversations() const;
    void                        paintOver(gfx::Painter &p) override; // the drop line

    // Collapses or expands a section by title (tests; a click does the same).
    bool toggleSection(std::string_view title);
    void showAllChannels(); // the "N more channels" row

    std::function<void()>                    onThreads;       // the Threads entry
    std::function<void()>                    onSavedMessages; // the Saved messages entry
    std::function<void()>                    onScheduled;     // the Scheduled messages entry
    std::function<void()>                    onFindChannel;   // "Add channels" → Find a channel
    std::function<void()>                    onCreateChannel; // "Add channels" → Create a channel
    std::function<void()>                    onBrowsePeople;  // the Direct messages header's "+"
    // Agent workspace: the Sessions "+" and "Add sessions", a
    // teammate's row, the Team header's "+".
    std::function<void(ui::PointF at)>       onSessionMenu;
    std::function<void(const std::string &)> onTeammate;
    std::function<void()>                    onAddTeammate;
    // Highlights a teammate's row as the open page ("" clears it); no
    // conversation is highlighted meanwhile.
    void                                     selectTeammate(const std::string &role);
    const std::string                       &selectedTeammate() const { return _selectedTeammate; }
    // The entries over the sections, each an overview page: Threads, Saved
    // messages (shown while something is saved, Store::hasSaved), Scheduled
    // messages (while something is scheduled, Store::hasScheduled).
    enum class Nav : uint8_t { None, Threads, Saved, Scheduled };
    // Highlights an entry as the open page (None: none); opening a
    // conversation or a teammate clears it.
    void selectNav(Nav n);
    Nav  selectedNav() const { return _navSelected; }
    bool threadsUnread() const; // the Threads entry is bright (Store::unreadThreads)
    // A click on a row's huddle pill (tests): false when it shows none.
    bool joinHuddle(model::ConvRef conv);
    bool scheduledShown() const; // tests
    // The Team section's rows, top to bottom (tests): role ids.
    std::vector<std::string> teammates() const;
    // A teammate row's look (tests): bold, selected, its avatar's presence.
    struct TeammateState {
        bool exists = false, bold = false, selected = false, visible = false;
        int  presence = 0; // Avatar::Presence
    };
    TeammateState  teammateState(const std::string &role) const;
    int            conversationPresence(model::ConvRef conv) const; // Avatar::Presence of a DM row
    // Context menus (right click, long press, Menu key): a conversation's on
    // its row.
    void           setMenus(Menus *m) { _menus = m; }
    SidebarFooter &footer() { return *_footer; }

    void paint(gfx::Painter &p) override; // the nav gradient

private:
    friend class ConvRow;
    friend class Menus; // the notify section ticks level()
    friend class SectionHeader;
    friend class TeammateRow;
    void               refresh(model::ConvRef conv); // one row from the Store
    void               refreshAll();
    void               refreshSections();
    void               refreshNav(); // which of the entries show
    ConvRow           *rowFor(model::ConvRef conv) const;
    ui::View          *navRow(Nav n) const;
    void               showNavSelection(); // the entries' checked look
    // The unread / effective level (Settings' default) / badge rules.
    bool               paintsUnread(const model::Conversation &c) const;
    model::NotifyLevel level(const model::Conversation &c) const;
    bool               muted(const model::Conversation &c) const;
    bool               isApp(const model::Conversation &c) const;
    bool               relevant(model::ConvRef c) const;
    void               applyCollapse(SectionHeader *h);
    // reveal: scroll the selected row into view afterwards.
    void               rebuildSoon(bool reveal = true);
    void               usersSoon();
    void               sectionsSoon(); // refreshSections + refreshTeammates, coalesced
    uint64_t           userShape() const;
    void               noteShape(uint64_t shape); // a userShape() and what it was taken at
    void               addChannelsMenu(ui::View *row);
    void               refreshTeammates();

    // The fork's order, drag and drop (sidebar_order_impl.h).
    void                   sortSection(int section, std::vector<model::ConvRef> &list) const;
    bool                   rowEvent(ConvRow *row, ui::Event &e); // true: a drag took it
    void                   dragPress(ConvRow *row, ui::PointF windowPos);
    bool                   dragMove(ConvRow *row, ui::PointF windowPos);
    bool                   dragEnd();
    void                   dragCancel();
    std::vector<ConvRow *> shownRows(const SectionHeader *h, const ConvRow *except) const;
    void                   dropAt(ConvRow *row, int index);

    screens::Context                      &_ctx;
    Avatars                               &_avatars;
    ui::ScrollView                        *_scroll = nullptr;
    ui::View                              *_items  = nullptr;
    SidebarFooter                         *_footer = nullptr;
    Menus                                 *_menus  = nullptr;
    std::vector<ConvRow *>                 _rows;
    std::vector<ConvRow *>                 _rowOf; // by ConvRef: the row, else null
    std::vector<TeammateRow *>             _teamRows;
    std::vector<model::Backend::AgentRole> _team; // as last listed (rebuild)
    std::string                            _selectedTeammate;
    std::vector<SectionHeader *>           _sections;
    std::vector<ui::View *>                _nav; // Threads, Saved messages, Scheduled messages
    std::vector<const char *>              _navTitles;
    ui::View                *_savedRow = nullptr, *_threadsRow = nullptr, *_scheduledRow = nullptr;
    Nav                      _navSelected = Nav::None;
    VisitStamps              _visited; // opened here (visit stamps)
    Filters                  _filters;
    model::ConvRef           _selected        = model::kNoConv;
    model::Store::ObserverId _observer        = 0;
    std::shared_ptr<int>     _alive           = std::make_shared<int>(0);
    plat::TimerId            _rebuildTimer    = 0;
    plat::TimerId            _usersTimer      = 0;
    plat::TimerId            _sectionsTimer   = 0;
    uint64_t                 _userShape       = 0; // userShape() at the last rebuild
    // The Store and its profile / text revisions _userShape was taken at.
    const model::Store      *_shapeStore      = nullptr;
    uint64_t                 _shapeProfileRev = 0, _shapeTextRev = 0;
    model::UserRef           _shapeMe         = model::kNoUser;
    int                      _rebuilds        = 0;
    bool                     _revealOnRebuild = false;
    int                      _hiddenChannels  = 0;
    bool                     _collapsed[5]    = {}; // starred, channels, DMs, apps, team
    bool                     _showAllChannels = false;

    // The fork's order, drag and drop.
    std::vector<std::string> _order[4];          // setOrder
    ConvRow                 *_dragRow = nullptr; // pressed, maybe dragging
    ui::PointF               _dragFrom;
    bool                     _dragging         = false;
    bool                     _rebuildAfterDrag = false;
    int                      _dropIndex        = -1;
};

} // namespace shell
