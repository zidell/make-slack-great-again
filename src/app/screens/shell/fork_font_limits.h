#pragma once

namespace shell::fork {
// Leave room for larger text on Linux displays.
#if defined(__linux__)
inline constexpr int kFontPxMax = 30;
#else
inline constexpr int kFontPxMax = 18;
#endif
} // namespace shell::fork
