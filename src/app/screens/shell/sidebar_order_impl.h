// Included once, at the end of sidebar.cpp (it needs the row classes defined
// there): the fork's per-workspace section order (A-Z channels, drag to
// reorder, Option+Up/Down) and the folds kept per workspace. Kept out of
// sidebar.cpp so upstream changes there rarely meet it.
#pragma once

#include <cctype>
#include <unordered_map>

namespace shell {

// A row's press, drag and drop; false lets the row handle it as a click.
bool Sidebar::rowEvent(ConvRow *row, Event &e) {
    switch (e.type) {
    case EventType::PointerDown:
        if (e.button == plat::Button::Left)
            dragPress(row, e.windowPos);
        break;
    case EventType::PointerMove:
        if (dragMove(row, e.windowPos))
            return true;
        break;
    case EventType::PointerUp:
        if (dragEnd())
            return true;
        break;
    case EventType::PointerCancel:
        dragCancel();
        break;
    default:
        break;
    }
    return false;
}

// ── Order, drag and drop ────────────────────────────────────────────────────

void Sidebar::setOrder(int section, std::vector<std::string> ids) {
    if (section < 0 || section >= int(std::size(_order)) || _order[section] == ids)
        return;
    _order[section] = std::move(ids);
    rebuild();
}

void Sidebar::sortSection(int section, std::vector<ConvRef> &list) const {
    const auto                                  &store = _ctx.store();
    const std::vector<std::string>              &ord   = _order[section];
    std::unordered_map<std::string_view, size_t> pos;
    for (size_t i = 0; i < ord.size(); ++i)
        pos.emplace(ord[i], i);
    const bool byName = section <= 1;
    auto       rank   = [&](ConvRef c) {
        const auto it = pos.find(store.conversation(c).id);
        return it == pos.end() ? SIZE_MAX : it->second;
    };
    auto key = [&](ConvRef c) {
        std::string n = store.displayName(c);
        for (char &ch : n)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return n;
    };
    std::stable_sort(list.begin(), list.end(), [&](ConvRef a, ConvRef b) {
        const size_t ra = rank(a), rb = rank(b);
        if (ra != rb)
            return ra < rb;
        return byName && ra == SIZE_MAX && key(a) < key(b);
    });
}

std::vector<ConvRow *> Sidebar::shownRows(const SectionHeader *h, const ConvRow *except) const {
    std::vector<ConvRow *> out;
    for (ConvRow *r : h->rows)
        if (r != except && r->visible())
            out.push_back(r);
    return out;
}

void Sidebar::dragPress(ConvRow *row, PointF windowPos) {
    _dragRow   = row;
    _dragFrom  = windowPos;
    _dragging  = false;
    _dropIndex = -1;
}

bool Sidebar::dragMove(ConvRow *row, PointF windowPos) {
    if (!_dragRow || row != _dragRow)
        return false;
    if (!_dragging) {
        if (std::abs(windowPos.y - _dragFrom.y) < 6 || shownRows(row->section, row).empty())
            return false;
        _dragging = true;
        row->setCursor(plat::Cursor::Grabbing);
    }
    // Near the list's top or bottom edge it scrolls along.
    const RectF view = _scroll->windowRect();
    if (windowPos.y < view.y + 24)
        _scroll->scrollBy(-8);
    else if (windowPos.y > view.y + view.h - 24)
        _scroll->scrollBy(8);
    const auto rows = shownRows(row->section, row);
    int        idx  = 0;
    for (const ConvRow *r : rows) {
        const RectF rr = r->windowRect();
        if (windowPos.y > rr.y + rr.h / 2)
            ++idx;
    }
    if (idx != _dropIndex) {
        _dropIndex = idx;
        update();
    }
    return true;
}

bool Sidebar::dragEnd() {
    ConvRow   *row   = std::exchange(_dragRow, nullptr);
    const bool moved = std::exchange(_dragging, false);
    const int  idx   = std::exchange(_dropIndex, -1);
    if (_rebuildAfterDrag)
        rebuildSoon(false);
    if (!moved || !row)
        return false;
    row->setCursor(plat::Cursor::Arrow);
    update();
    dropAt(row, idx);
    return true;
}

void Sidebar::dragCancel() {
    if (_dragRow && _dragging)
        _dragRow->setCursor(plat::Cursor::Arrow);
    _dragRow   = nullptr;
    _dragging  = false;
    _dropIndex = -1;
    if (_rebuildAfterDrag)
        rebuildSoon(false);
    update();
}

bool Sidebar::moveRow(ConvRef conv, int index) {
    ConvRow *r = rowFor(conv);
    if (!r || !r->section || r->section->kind >= int(std::size(_order)))
        return false;
    dropAt(r, index);
    return true;
}

// The section's rows with `row` placed before the shown row now at `index`
// (after the last shown one past the end); its ids, then any saved ones it
// doesn't list now (channels outside the relevant days) keep their order.
void Sidebar::dropAt(ConvRow *row, int index) {
    SectionHeader *h = row->section;
    const int      s = h->kind;
    if (s >= int(std::size(_order)))
        return;
    const auto shown = shownRows(h, row);
    index            = std::clamp(index, 0, int(shown.size()));
    std::vector<ConvRow *> all;
    for (ConvRow *r : h->rows)
        if (r != row)
            all.push_back(r);
    auto at = index < int(shown.size()) ? std::find(all.begin(), all.end(), shown[size_t(index)])
                                        : std::find(all.begin(), all.end(), shown.back()) + 1;
    all.insert(at, row);
    const auto              &store = _ctx.store();
    std::vector<std::string> ids;
    for (const ConvRow *r : all)
        ids.push_back(store.conversation(r->conv).id);
    for (const std::string &id : _order[s])
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    if (ids == _order[s])
        return;
    _order[s] = ids;
    if (onOrderChanged)
        onOrderChanged(s, ids);
    rebuildSoon(false); // not from inside the row's own event
}

void Sidebar::paintOver(gfx::Painter &p) {
    if (!_dragging || !_dragRow || _dropIndex < 0)
        return;
    const auto rows = shownRows(_dragRow->section, _dragRow);
    if (rows.empty())
        return;
    const RectF r   = _dropIndex < int(rows.size()) ? rows[size_t(_dropIndex)]->windowRect()
                                                    : rows.back()->windowRect();
    const float y   = mapFromWindow({0, _dropIndex < int(rows.size()) ? r.y : r.y + r.h}).y;
    const float top = mapFromWindow({0, _scroll->windowRect().y}).y;
    if (y < top || y > top + _scroll->height()) // scrolled out of the list
        return;
    p.fillRoundRect({kPill + 4, y - 1, width() - 2 * kPill - 8, 2}, 1, color(C::Accent));
}

std::vector<ConvRef> Sidebar::shownConversations() const {
    std::vector<ConvRef> out;
    for (const ConvRow *r : _rows)
        if (r->visible())
            out.push_back(r->conv);
    return out;
}

ConvRef Sidebar::adjacentConversation(int dir, bool unreadOnly) const {
    std::vector<const ConvRow *> rows;
    for (const ConvRow *r : _rows)
        if (r->visible())
            rows.push_back(r);
    int i = -1;
    for (size_t k = 0; k < rows.size(); ++k)
        if (rows[k]->conv == _selected)
            i = int(k);
    if (i < 0)
        i = dir > 0 ? -1 : int(rows.size());
    for (i += dir; i >= 0 && i < int(rows.size()); i += dir)
        if (!unreadOnly || rows[size_t(i)]->unread || rows[size_t(i)]->count > 0)
            return rows[size_t(i)]->conv;
    return kNoConv;
}

uint8_t Sidebar::collapsedMask() const {
    uint8_t m = 0;
    for (size_t k = 0; k < std::size(_collapsed); ++k)
        if (_collapsed[k])
            m |= uint8_t(1u << k);
    for (const SectionHeader *h : _sections) { // the live state, ahead of a rebuild
        m &= uint8_t(~(1u << h->kind));
        if (h->collapsed)
            m |= uint8_t(1u << h->kind);
    }
    return m;
}

void Sidebar::setCollapsedMask(uint8_t mask) {
    for (size_t k = 0; k < std::size(_collapsed); ++k)
        _collapsed[k] = (mask >> k) & 1;
    for (SectionHeader *h : _sections) {
        h->collapsed = _collapsed[h->kind];
        applyCollapse(h);
    }
}

} // namespace shell
