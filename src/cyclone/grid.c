#include "kerosene.h"

float cyclone_layout_grid_children(KDomNode *node, KRenderList *out, float x, float y, float w) {
    int cols = node->style.grid_columns > 0 ? node->style.grid_columns : 1;
    if (w < 560.0f && cols > 2) cols = 2;
    if (w < 360.0f) cols = 1;

    float gap = 12.0f;
    float cell_w = (w - gap * (float)(cols - 1)) / (float)cols;
    int col = 0;
    float row_y = y;
    float row_h = 0.0f;
    for (KDomNode *child = node->first_child; child; child = child->next) {
        if (child->style.display == K_DISPLAY_NONE) {
            continue;
        }
        float cx = x + (cell_w + gap) * (float)col;
        float h = cyclone_layout_node(child, out, cx, row_y, cell_w, node->href);
        if (h > row_h) row_h = h;
        col++;
        if (col >= cols) {
            col = 0;
            row_y += row_h + gap;
            row_h = 0.0f;
        }
    }
    if (col != 0) {
        row_y += row_h;
    }
    return row_y - y;
}
