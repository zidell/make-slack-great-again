#include "app/screens/messages/message_list.h"

#include "app/screens/common/message_rules.h"
#include "base/i18n.h"
#include "base/str.h"
#include "ui/controls.h"

#include <algorithm>
#include <cmath>
#include <utility>

// Picking (bulk delete): MessageList's part of it (message_list.h).

namespace screens {

using i18n::arg;
using i18n::tr;
using ui::C;
using ui::Font;

namespace {

// The pick bar's card: the hover toolbar's look (ToolbarCard in
// message_list.cpp) — four faint shadow halos, surface.raised, a hairline.
class PickBarCard final : public ui::View {
public:
    PickBarCard() {
        setPaintOutset(6);
        style().row().padding(8, 6).spacing(4).items(ui::Align::Center);
    }
    void paint(gfx::Painter &p) override {
        const ui::RectF b = bounds();
        for (int i = 4; i >= 1; --i) {
            const float k = float(i);
            p.fillRoundRect(
                {b.x - k, b.y - k, b.w + 2 * k, b.h + 2 * k + 1},
                8 + k,
                gfx::Color(uint32_t(2 + (4 - i) * 2) << 24)
            );
        }
        p.fillRoundRect(b, 8, ui::color(C::FormBg));
        p.strokeRoundRect(b, 8, 1, ui::color(C::FormDivider));
        View::paint(p);
    }
    bool onEvent(ui::Event &e) override { return e.type == ui::EventType::PointerDown; }
};

} // namespace

void MessageList::makePickBar() {
    // The pick bar (picking): "N selected", Cancel, Delete.
    _pickBar   = add<PickBarCard>();
    _pickCount = _pickBar->add<ui::Label>("", Font::Small, C::TextMuted);
    _pickCount->setMaxLines(1);
    _pickCount->style().margins(2, 0, 6, 0);
    auto *cancel = _pickBar->add<ui::Button>(
        tr("Cancel"), ui::Button::Kind::Secondary, ui::Button::Form::Small
    );
    cancel->onClick = [this] { stopPicking(); };
    _pickDelete =
        _pickBar->add<ui::Button>(tr("Delete"), ui::Button::Kind::Danger, ui::Button::Form::Small);
    _pickDelete->onClick = [this] { deletePicked(); };
    _pickBar->setVisible(false);
}

void MessageList::placePickBar() {
    if (!_picking)
        return;
    const float     w = width(), h = height();
    const ui::SizeF sz = _pickBar->measure(ui::kInf, ui::kInf); // bottom right, over the list
    _pickBar->setFrame(
        {std::floor(w - 12 - sz.w), std::floor(h - _typingH - 12 - sz.h), sz.w, sz.h}
    );
}

bool MessageList::canDelete(const model::Message &m) const {
    // My own messages, or any as a workspace admin — any at all in an agent
    // session (deleteAnyMessage) — and only when the backend can take this
    // one now (not while the session is working).
    const Store &st    = _ctx.store;
    const bool   mine  = m.user == st.me && m.user != model::kNoUser;
    const bool   admin = st.me != model::kNoUser && st.user(st.me).admin;
    return !m.pending && (_ctx.backend.isAgentSession(_conv) || mine || admin) &&
           _ctx.backend.canDeleteMessage(_conv, m.ts);
}

// ── Picking ─────────────────────────────────────────────────────────────────

void MessageList::startPicking(Ts ts) {
    const model::Message *m = message(ts);
    if (_picking || !m || isSystem(*m))
        return;
    clearSelection();
    hideToolbar();
    _fileView = nullptr;
    _fileBar->setVisible(false);
    _picking = true;
    _picked.clear();
    _pickAnchor = 0;
    pickClicked(ts, false);
    _pickBar->setVisible(true);
    if (window()) // Escape reaches the list
        window()->setFocus(_list);
    updatePickBar();
}

void MessageList::pickClicked(Ts ts, bool range) {
    if (!_picking)
        return;
    const auto pickable = [&](Ts t) {
        const model::Message *m = message(t);
        return m && !isSystem(*m) && canDelete(*m);
    };
    const int to   = itemIndex(ts);
    const int from = range && _pickAnchor ? itemIndex(_pickAnchor) : -1;
    if (from >= 0 && to >= 0) { // the range, added from the anchor on, up to the cap
        const int step = to >= from ? 1 : -1;
        for (int i = from; i != to + step && _picked.size() < kMaxPicked; i += step) {
            const Item &it = _items[size_t(i)];
            if (it.kind == ItemKind::Message && pickable(it.ts) && !has(_picked, it.ts))
                _picked.push_back(it.ts);
        }
    } else if (has(_picked, ts)) {
        std::erase(_picked, ts);
    } else if (pickable(ts) && _picked.size() < kMaxPicked) {
        _picked.push_back(ts);
    }
    _pickAnchor = ts;
    updatePickBar();
    repaintRows();
}

void MessageList::stopPicking() {
    if (!_picking)
        return;
    _picking = false;
    _picked.clear();
    _pickAnchor = 0;
    _pickBar->setVisible(false);
    repaintRows();
}

void MessageList::deletePicked() {
    // No dialog: the picks were deliberate. Oldest first; the backend sends
    // them to Slack one at a time.
    std::vector<Ts> ts = _picked;
    std::sort(ts.begin(), ts.end());
    const ConvRef conv = _conv;
    stopPicking();
    for (Ts t : ts)
        if (_ctx.store().findMessage(conv, t))
            _ctx.backend.remove(conv, t);
}

void MessageList::updatePickBar() {
    std::erase_if(_picked, [&](Ts t) { return itemIndex(t) < 0; });
    _pickCount->setText(
        _picked.empty() ? std::string(tr("Select messages to delete"))
        : _picked.size() >= kMaxPicked
            ? arg(tr("%1 selected (the most at once)"), str::number(int64_t(_picked.size())))
            : i18n::trn("%n selected", "%n selected", int64_t(_picked.size()))
    );
    _pickDelete->setLabel(
        _picked.empty() ? std::string(tr("Delete"))
                        : arg(tr("Delete %1"), str::number(int64_t(_picked.size())))
    );
    _pickDelete->setEnabled(!_picked.empty());
    layout();
}

void MessageList::repaintRows() {
    for (int i = 0; i < int(_items.size()); ++i)
        if (ui::View *v = _list->viewFor(i))
            v->update();
}

} // namespace screens
