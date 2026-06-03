#include "kerosene.h"

int spark_compile_count_statements(const char *source) {
    int count = 0;
    int in_string = 0;
    char quote = 0;
    for (const char *p = source ? source : ""; *p; p++) {
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == quote) {
                in_string = 0;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            in_string = 1;
            quote = *p;
        } else if (*p == ';' || *p == '\n') {
            count++;
        }
    }
    return count;
}
