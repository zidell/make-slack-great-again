#include "app/screens/messages/image_zoom.h"

#include "app/screens/common/message_rules.h"
#include "app/screens/common/remote_images.h"
#include "app/screens/messages/image_cache.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace screens {

ImageZoom::ImageZoom(Context &ctx, Source src, std::function<ui::RectF()> fitted)
    : _ctx(ctx), _src(std::move(src)), _fitted(std::move(fitted)) {
    setVisible(false); // until zoomed
}

void ImageZoom::setStage(ui::RectF stage) {
    setFrame(stage);
    if (zoomed()) // kept in place, clamped to the new stage
        place(_rect.x + stage.x, _rect.y + stage.y);
}

ui::SizeF ImageZoom::natural() const {
    float w = float(_src.width), h = float(_src.height);
    if (w <= 0 || h <= 0) {
        int nw = 0, nh = 0;
        _ctx.images.naturalSize(_src.thumb, &nw, &nh);
        w = nw > 0 ? float(nw) : 400, h = nh > 0 ? float(nh) : 300;
    }
    return {w, h};
}

float ImageZoom::fitScale() const {
    return _fitted().w / natural().w;
}
float ImageZoom::maxScale() const {
    return std::max(fitScale(), kMaxZoom);
}

std::string ImageZoom::source() const {
    const std::string &p = _src.full.empty() ? _src.thumb : _src.full;
    if (!RemoteImages::isRemote(p))
        return p;
    return _ctx.remote ? _ctx.remote->cachedPath(p) : std::string();
}

// A still picture, bigger than fitted at its largest step, that is here.
bool ImageZoom::zoomable() const {
    return !_src.thumb.empty() && !isGifPath(_src.thumb) && maxScale() > fitScale() * 1.01f &&
           (zoomed() || !source().empty());
}

ui::RectF ImageZoom::shown() const {
    if (!zoomed())
        return _fitted();
    const ui::RectF f = frame();
    return {_rect.x + f.x, _rect.y + f.y, _rect.w, _rect.h};
}

uint8_t ImageZoom::viewerCursor(ui::PointF pos) const {
    if (_dragging)
        return uint8_t(plat::Cursor::Grabbing);
    if (zoomable() && frame().contains(pos) && shown().contains(pos))
        return uint8_t(_alt || _scale >= maxScale() ? plat::Cursor::ZoomOut : plat::Cursor::ZoomIn);
    return kCursorInherit;
}

bool ImageZoom::viewerEvent(const ui::Event &e) {
    // Option held: the magnifier zooms out (its press, its release, and the
    // modifiers each move carries).
    if (e.type == ui::EventType::KeyDown || e.type == ui::EventType::KeyUp ||
        e.type == ui::EventType::PointerMove || e.type == ui::EventType::PointerEnter) {
        if (const bool alt = (e.mods & plat::ModAlt) != 0; alt != _alt) {
            _alt = alt;
            refreshCursor();
        }
    }
    switch (e.type) {
    case ui::EventType::PointerDown:
        if (e.button != plat::Button::Left || !frame().contains(e.pos))
            return false;
        if (zoomable() && shown().contains(e.pos)) {
            _pressed  = true;
            _press    = e.pos;
            _pressAt  = {_rect.x, _rect.y};
            _dragging = false;
            return true;
        }
        if (zoomed()) {
            fit(); // the backdrop around a zoomed picture fits it again
            return true;
        }
        return false; // the viewer's: the backdrop closes it
    case ui::EventType::PointerMove:
        if (!_pressed)
            return false;
        if (zoomed()) {
            if (!_dragging && std::abs(e.pos.x - _press.x) + std::abs(e.pos.y - _press.y) > 4) {
                _dragging = true;
                refreshCursor();
            }
            if (_dragging)
                place(
                    frame().x + _pressAt.x + e.pos.x - _press.x,
                    frame().y + _pressAt.y + e.pos.y - _press.y
                );
        }
        return true;
    case ui::EventType::PointerUp:
        if (!_pressed)
            return false;
        if (!_dragging)
            step(e.pos, (e.mods & plat::ModAlt) != 0);
        _pressed = _dragging = false;
        refreshCursor();
        return true;
    case ui::EventType::PointerCancel:
        if (!_pressed)
            return false;
        _pressed = _dragging = false;
        return true;
    case ui::EventType::Magnify:
        if (!zoomable())
            return false;
        if (e.phase == plat::ScrollPhase::End) {
            _live = false; // drawn shrunk again
            update();
            return true;
        }
        _live = true; // straight from the decode: no shrink per event
        zoomTo(e.pos, (zoomed() ? _scale : fitScale()) * (1 + e.dx));
        return true;
    case ui::EventType::Scroll:
        if (!zoomed())
            return false;
        {
            const float k = e.precise ? 1.f : 50.f;
            place(frame().x + _rect.x - e.dx * k, frame().y + _rect.y - e.dy * k);
        }
        return true;
    default:
        return false;
    }
}

// One step in (or out) about `at`; at the largest step a click fits again.
void ImageZoom::step(ui::PointF at, bool out) {
    const float cur = zoomed() ? _scale : fitScale();
    if (!out && cur >= maxScale()) {
        fit();
        return;
    }
    zoomTo(at, out ? cur / kStep : cur * kStep);
}

// To `scale` (kept within fit…max; fitted at the bottom) about `at`.
void ImageZoom::zoomTo(ui::PointF at, float scale) {
    const float next = std::min(scale, maxScale());
    if (next <= fitScale() * 1.01f) {
        if (zoomed())
            fit();
        return;
    }
    if (!zoomed()) {
        const ui::SizeF n = natural();
        const float     d = std::min(
            {1.f,
             float(kDecodeSide) / std::max(n.w, n.h),
             std::sqrt(float(kDecodePixels) / (n.w * n.h))}
        );
        if (source().empty())
            return;
        load(int(std::lround(n.w * d)), int(std::lround(n.h * d)));
        setVisible(true);
    }
    // The picture's point under `at` stays there.
    const ui::RectF r  = shown();
    const float     ux = (at.x - r.x) / r.w, uy = (at.y - r.y) / r.h;
    _scale            = next;
    const ui::SizeF n = natural();
    place(at.x - ux * n.w * _scale, at.y - uy * n.h * _scale);
    refreshCursor();
}

void ImageZoom::fit() {
    _scale = 0;
    _live  = false;
    setVisible(false);
    refreshCursor();
}

// Kept covering the stage on an axis it overflows, centred on one it doesn't.
void ImageZoom::place(float x, float y) {
    const ui::RectF st = frame();
    const ui::SizeF n  = natural();
    const float     w = std::floor(n.w * _scale), h = std::floor(n.h * _scale);
    auto            clampAxis = [](float v, float lo, float len, float size) {
        if (size <= len)
            return std::floor(lo + (len - size) / 2);
        return std::round(std::clamp(v, lo + len - size, lo));
    };
    x     = clampAxis(x, st.x, st.w, w);
    y     = clampAxis(y, st.y, st.h, h);
    _rect = {x - st.x, y - st.y, w, h};
    update();
}

// At most once: the source at w×h physical px.
void ImageZoom::load(int w, int h) {
    if (_asked)
        return;
    _asked = true;
    _ctx.images.decodeOnce(
        {source(), w, h},
        [this, alive = std::weak_ptr<int>(_alive)](gfx::Bitmap b) {
            if (alive.expired() || b.width() <= 0)
                return;
            _bmp = std::move(b);
            update();
        },
        true
    );
}

void ImageZoom::refreshCursor() {
    if (window())
        window()->refreshCursor();
}

void ImageZoom::paint(gfx::Painter &p) {
    if (_bmp.width() <= 0)
        return;
    p.save();
    p.clipRect(bounds());
    p.fillRect(bounds(), ui::color(ui::C::ViewerBackdrop));
    // Shrunk once per zoom step (a smooth shrink of the whole source on every
    // paint would stall a drag); bilinear when magnified.
    const float        s  = windowScale();
    const int          dw = int(std::lround(_rect.w * s)), dh = int(std::lround(_rect.h * s));
    const gfx::Bitmap *b = &_bmp;
    if (!_live && dw < _bmp.width() && dh < _bmp.height()) {
        if (_shrunk.width() != dw || _shrunk.height() != dh)
            _shrunk = gfx::resize(_bmp.view(), dw, dh);
        b = &_shrunk;
    } else if (!_live) {
        _shrunk = {};
    }
    p.drawBitmap(b->view(), _rect, gfx::Sampling::Bilinear);
    p.restore();
}

} // namespace screens
