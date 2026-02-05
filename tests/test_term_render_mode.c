/*
 * test_term_render_mode.c - Unit tests for terminal render mode helpers
 */

#include "test_framework.h"
#include "../src/term_render_mode.h"

TEST(term_parse_ascii) {
    int mode = -1;
    ASSERT_EQ_INT(term_parse_render_mode("ascii", &mode), 0);
    ASSERT_EQ_INT(mode, TERM_RENDER_ASCII);
    return 1;
}

TEST(term_parse_half_aliases) {
    int mode = -1;

    ASSERT_EQ_INT(term_parse_render_mode("half", &mode), 0);
    ASSERT_EQ_INT(mode, TERM_RENDER_HALF);

    ASSERT_EQ_INT(term_parse_render_mode("half-block", &mode), 0);
    ASSERT_EQ_INT(mode, TERM_RENDER_HALF);

    ASSERT_EQ_INT(term_parse_render_mode("halfblock", &mode), 0);
    ASSERT_EQ_INT(mode, TERM_RENDER_HALF);

    return 1;
}

TEST(term_parse_braille) {
    int mode = -1;
    ASSERT_EQ_INT(term_parse_render_mode("braille", &mode), 0);
    ASSERT_EQ_INT(mode, TERM_RENDER_BRAILLE);
    return 1;
}

TEST(term_parse_invalid) {
    int mode = TERM_RENDER_ASCII;
    ASSERT_EQ_INT(term_parse_render_mode("emoji", &mode), -1);
    ASSERT_EQ_INT(term_parse_render_mode("", &mode), -1);
    ASSERT_EQ_INT(term_parse_render_mode(NULL, &mode), -1);
    ASSERT_EQ_INT(term_parse_render_mode("ascii", NULL), -1);
    return 1;
}

TEST(term_name) {
    ASSERT_STR_EQ(term_render_mode_name(TERM_RENDER_ASCII), "ascii");
    ASSERT_STR_EQ(term_render_mode_name(TERM_RENDER_HALF), "half");
    ASSERT_STR_EQ(term_render_mode_name(TERM_RENDER_BRAILLE), "braille");

    /* Unknown values should degrade to ascii */
    ASSERT_STR_EQ(term_render_mode_name(-1), "ascii");
    ASSERT_STR_EQ(term_render_mode_name(999), "ascii");
    return 1;
}

TEST(term_cycle) {
    ASSERT_EQ_INT(term_cycle_render_mode(TERM_RENDER_ASCII), TERM_RENDER_HALF);
    ASSERT_EQ_INT(term_cycle_render_mode(TERM_RENDER_HALF), TERM_RENDER_BRAILLE);
    ASSERT_EQ_INT(term_cycle_render_mode(TERM_RENDER_BRAILLE), TERM_RENDER_ASCII);
    return 1;
}

TEST(term_cycle_invalid) {
    ASSERT_EQ_INT(term_cycle_render_mode(-1), TERM_RENDER_ASCII);
    ASSERT_EQ_INT(term_cycle_render_mode(999), TERM_RENDER_ASCII);
    return 1;
}

RUN_TESTS("TermRenderMode");
