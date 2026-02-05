/*
 * term_render_mode.c - Terminal render mode helpers
 */

#include "term_render_mode.h"
#include <string.h>

const char *term_render_mode_name(int mode) {
    switch (mode) {
        case TERM_RENDER_HALF:
            return "half";
        case TERM_RENDER_BRAILLE:
            return "braille";
        case TERM_RENDER_ASCII:
        default:
            return "ascii";
    }
}

int term_parse_render_mode(const char *s, int *mode_out) {
    if (!s || !mode_out) return -1;

    if (strcmp(s, "ascii") == 0) {
        *mode_out = TERM_RENDER_ASCII;
        return 0;
    }
    if (strcmp(s, "half") == 0 || strcmp(s, "half-block") == 0 || strcmp(s, "halfblock") == 0) {
        *mode_out = TERM_RENDER_HALF;
        return 0;
    }
    if (strcmp(s, "braille") == 0) {
        *mode_out = TERM_RENDER_BRAILLE;
        return 0;
    }

    return -1;
}

int term_cycle_render_mode(int mode) {
    if (mode < 0 || mode >= TERM_RENDER_COUNT) {
        return TERM_RENDER_ASCII;
    }
    return (mode + 1) % TERM_RENDER_COUNT;
}
