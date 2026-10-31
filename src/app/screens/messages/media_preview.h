#pragma once

#include "app/model/types.h"
#include "base/str.h"

namespace screens {
inline bool hasFilePreview(const model::File &f) {
    return f.hasPreview() || (str::startsWith(f.mime, "video/") && !f.thumb.empty());
}

inline const std::string &filePreviewPath(const model::File &f) {
    if (f.mime == "image/gif" && !f.thumb.empty())
        return f.thumb;
    return f.isImage() ? f.path : f.thumb;
}
} // namespace screens
