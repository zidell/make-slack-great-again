// The file viewer's zoom (FileViewer in message_list.cpp, which forwards its
// events and cursor here). A still picture zooms: a click on it steps in
// (Option-click, or a click at the largest step, steps out / fits it again)
// about the point clicked, a pinch zooms about the pointer; zoomed, a drag or
// scrolling pans it and a click on the backdrop fits it again. The magnifier
// cursor shows over it (zoom-out with Option held).
//
// The zoomed picture is decoded once near its natural size from the local
// file (not into the shared ImageCache: one large bitmap would evict all of
// it) and painted by this view, which the viewer frames over the area under
// its bar. Nothing paints until it is in, so the fitted picture stays.
#pragma once

#include "app/screens/messages/context_fwd.h"
#include "gfx/gfx.h"
#include "ui/ui.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace screens {

class ImageZoom final : public ui::View {
public:
    struct Source {
        std::string thumb;                 // what the viewer shows at once
        std::string full;                  // the original over it ("" none)
        int         width = 0, height = 0; // the file's natural size (0: ask the header)
    };
    // `fitted`: the viewer's picture rect as fitted (viewer coordinates).
    ImageZoom(Context &ctx, Source src, std::function<ui::RectF()> fitted);

    // The viewer's area under its bar (viewer coordinates): this view's frame.
    void    setStage(ui::RectF stage);
    // Every viewer event, in its coordinates, before its own handling: true
    // when the zoom used it (a press it took is captured by the viewer).
    bool    viewerEvent(const ui::Event &e);
    // The magnifier (or the grabbing hand) at `pos`; kCursorInherit elsewhere.
    uint8_t viewerCursor(ui::PointF pos) const;
    bool    zoomed() const { return _scale > 0; }

    void paint(gfx::Painter &p) override;

private:
    static constexpr float kStep = 2, kMaxZoom = 4; // × natural size at most
    // Longest side and pixels of the zoomed decode (memory: ~160 MB at most).
    static constexpr int   kDecodeSide = 8192, kDecodePixels = 40 << 20;

    ui::SizeF   natural() const;
    float       fitScale() const;
    float       maxScale() const;
    std::string source() const; // the local file the zoom decodes, "" not here yet
    bool        zoomable() const;
    ui::RectF   shown() const; // the picture now (viewer coordinates)
    void        step(ui::PointF at, bool out);
    void        zoomTo(ui::PointF at, float scale);
    void        fit();
    void        place(float x, float y); // the zoomed picture's top-left (viewer coords)
    void        load(int w, int h);
    void        refreshCursor();

    Context                   &_ctx;
    Source                     _src;
    std::function<ui::RectF()> _fitted;
    std::shared_ptr<int>       _alive = std::make_shared<int>(); // decodeOnce outliving us
    gfx::Bitmap                _bmp, _shrunk;
    ui::RectF                  _rect;            // the zoomed picture, local
    float                      _scale = 0;       // logical px per natural px while zoomed, 0 fitted
    ui::PointF                 _press, _pressAt; // the press, and the picture's origin then
    bool _asked = false, _live = false, _pressed = false, _dragging = false, _alt = false;
};

} // namespace screens
