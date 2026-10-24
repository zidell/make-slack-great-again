#pragma once

#include "app/model/types.h"
#include "ui/view.h"

namespace screens {

// Slack's short fields share a row in source order; a long field ends a pair.
// Keep the renderer outside the upstream message row implementation.
template <typename Render>
void buildAttachmentFields(
    ui::View *parent, const std::vector<model::AttachmentField> &fields, Render render
) {
    for (size_t i = 0; i < fields.size();) {
        const size_t count = fields[i].shortField && i + 1 < fields.size() &&
                                     fields[i + 1].shortField
                                 ? 2
                                 : 1;
        auto *row = parent->add<ui::View>();
        row->style().row().spacing(16).margins(0, 4, 0, 0);
        for (size_t j = 0; j < count; ++j) {
            auto *cell = row->add<ui::View>();
            cell->style().flex(1).spacing(3);
            render(cell, fields[i++]);
        }
    }
}

} // namespace screens
