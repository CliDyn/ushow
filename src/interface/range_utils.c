/*
 * range_utils.c - Pure logic utilities for range manipulation
 *
 * No X11 dependency - can be used in tests and non-GUI code.
 */

#include "range_utils.h"
#include <math.h>
#include <stdio.h>

void range_compute_symmetric(float cur_min, float cur_max,
                             float *new_min, float *new_max) {
    float biggest = fabsf(cur_min) > fabsf(cur_max) ? fabsf(cur_min) : fabsf(cur_max);
    *new_min = -biggest;
    *new_max = biggest;
}

int range_parse_value(const char *str, float *value) {
    if (!str || !value) return 0;
    float v;
    if (sscanf(str, "%g", &v) != 1) return 0;
    *value = v;
    return 1;
}
