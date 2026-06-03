#include "kerosene.h"

static int visible_child_count(KDomNode *node) {
    int count = 0;
    for (KDomNode *child = node->first_child; child; child = child->next) {
        if (child->style.display != K_DISPLAY_NONE) {
            count++;
        }
    }
    return count;
}

void cyclone_layout_flex_children_unused_anchor(void);

float cyclone_layout_flex_children(KDomNode *node, KRenderList *out, float x, float y, float w) {
    int count = visible_child_count(node);
    if (count <= 0) {
        return 0.0f;
    }

    if (!node->style.flex_row) {
        float cy = y;
        for (KDomNode *child = node->first_child; child; child = child->next) {
            cy += cyclone_layout_node(child, out, x, cy, w, node->href);
        }
        return cy - y;
    }

    float gap = 10.0f;
    float child_w = (w - gap * (float)(count - 1)) / (float)count;
    if (child_w < 80.0f) {
        child_w = w;
    }
    float cx = x;
    float max_h = 0.0f;
    for (KDomNode *child = node->first_child; child; child = child->next) {
        if (child->style.display == K_DISPLAY_NONE) {
            continue;
        }
        float h = cyclone_layout_node(child, out, cx, y, child_w, node->href);
        if (h > max_h) max_h = h;
        cx += child_w + gap;
    }
    return max_h;
}
