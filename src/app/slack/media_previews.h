#pragma once

#include "app/model/types.h"
#include "base/json.h"
#include "base/str.h"

namespace slack::mapjson {
// Keep the playable URL intact. The existing thumbnail field is also cached,
// so video posters and GIF previews survive restarts without a schema change.
inline void readMediaPreview(const json::Value &o, model::File &f) {
    const bool video = str::startsWith(f.mime, "video/");
    if (!video && f.mime != "image/gif")
        return;
    constexpr const char *keys[] = {
        "thumb_720",
        "thumb_800",
        "thumb_960",
        "thumb_1024",
        "thumb_480",
        "thumb_360",
        "thumb_video",
        "thumb_160",
        "deanimate",
        "deanimate_gif"
    };
    for (const char *key : keys) {
        const auto url = o[key].str();
        if (url.empty())
            continue;
        f.thumb = std::string(url);
        if (f.width <= 0 || f.height <= 0) {
            const int w = int(o[std::string(key) + "_w"].integer());
            const int h = int(o[std::string(key) + "_h"].integer());
            if (w > 0 && h > 0) {
                f.width  = w;
                f.height = h;
            }
        }
        return;
    }
}
} // namespace slack::mapjson
