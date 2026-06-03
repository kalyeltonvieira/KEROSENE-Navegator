#include "kerosene.h"

#include <string.h>

int spark_jit_loop_budget(const char *source) {
    int loops = 0;
    const char *p = source ? source : "";
    while ((p = strstr(p, "for")) != NULL) {
        loops++;
        p += 3;
    }
    p = source ? source : "";
    while ((p = strstr(p, "while")) != NULL) {
        loops++;
        p += 5;
    }
    if (loops == 0) {
        return 4096;
    }
    if (loops < 4) {
        return 1024;
    }
    return 256;
}
