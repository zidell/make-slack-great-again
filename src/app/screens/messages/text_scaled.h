// The fork's text-size spacing: the message list's vertical gaps and the
// fixed heights of rows that hold text grow and shrink with the text size
// (Ctrl +/−, Settings → text size) instead of staying at their 100 % px.
// Line height already follows the font (text::LayoutOptions::lineHeight is a
// multiple of the size); these are the paddings around it.
//
// Not rounded to whole px, so each Ctrl +/- step moves them a little (a 3 px
// gap rounded stayed 3 px over several steps and looked fixed); the layout
// puts frames on the physical pixel grid anyway (View::snapPx).
//
// A Scaled reads as a float, so the original metric constants keep their
// names and call sites: only their declarations change. Rows are rebuilt when
// the scale changes (MessageList::styleChanged), so reading it at build time
// is enough.
#pragma once

#include "ui/view.h"

namespace screens {

inline float textScaled(float px) {
    const float s = ui::app() ? ui::app()->userTextScale() : 1.f;
    return px * s; // no rounding: layout snaps frames to the pixel grid
}

struct Scaled {
    float base;
    operator float() const { return textScaled(base); }
};

} // namespace screens
