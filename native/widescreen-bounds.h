// Presentation-only visibility experiment. Callers must independently verify
// the edition, active module and render-only call site before using this plan.
#pragma once
#include <cstdint>
#include <limits>

namespace sf3 {
struct HorizontalBounds { int16_t left, right; };

// Use the renderer's actual width/origin: an odd number of extra columns is
// deliberately asymmetric. Never widen a partial window or infer a viewport
// from a menu rectangle. This helper neither changes guest RAM nor enables a hook.
inline bool expanded_visibility_bounds(bool enabled, int native_width,
                                       int presentation_width, int origin_x,
                                       HorizontalBounds original,
                                       HorizontalBounds& expanded) {
    expanded = original;
    if (!enabled || native_width <= 0 || native_width > 32768 ||
        presentation_width <= native_width || presentation_width > 32768)
        return false;
    const int extra = presentation_width - native_width;
    if (origin_x != extra / 2 || original.left != -native_width / 2 ||
        original.right != native_width - native_width / 2 - 1)
        return false;
    const int left = int(original.left) - origin_x;
    const int right = int(original.right) + extra - origin_x;
    if (left < std::numeric_limits<int16_t>::min() ||
        right > std::numeric_limits<int16_t>::max()) return false;
    expanded = {int16_t(left), int16_t(right)};
    return true;
}
}
