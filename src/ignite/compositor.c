#include "kerosene.h"

int compositor_visible(KRect rect, float y_offset, float viewport_h) {
    float y = rect.y + y_offset;
    return y + rect.h >= 0.0f && y <= viewport_h && rect.w > 0.0f && rect.h > 0.0f;
}

KRect compositor_offset_rect(KRect rect, float x_offset, float y_offset) {
    rect.x += x_offset;
    rect.y += y_offset;
    return rect;
}
