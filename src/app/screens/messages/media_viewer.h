#pragma once

#include "app/screens/messages/context_fwd.h"

namespace screens {
// Native mini player where available. False leaves the existing action in place.
#ifdef __APPLE__
bool showMediaViewer(Context &ctx, const model::File &file);
#else
inline bool showMediaViewer(Context &, const model::File &) {
    return false;
}
#endif
} // namespace screens
