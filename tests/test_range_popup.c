/*
 * test_range_popup.c - Unit tests for range popup logic
 *
 * Tests the pure logic functions (symmetric computation, value parsing)
 * that don't require an X11 display.
 */

#include "test_framework.h"
#include "../src/interface/range_utils.h"
#include <float.h>

#define EPSILON 1e-6f

/* ========== range_compute_symmetric tests ========== */

/* Basic symmetric: positive range [2, 10] -> [-10, 10] */
TEST(symmetric_positive_range) {
    float new_min, new_max;
    range_compute_symmetric(2.0f, 10.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -10.0f, EPSILON);
    ASSERT_NEAR(new_max, 10.0f, EPSILON);
    return 1;
}

/* Symmetric: negative range [-10, -2] -> [-10, 10] */
TEST(symmetric_negative_range) {
    float new_min, new_max;
    range_compute_symmetric(-10.0f, -2.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -10.0f, EPSILON);
    ASSERT_NEAR(new_max, 10.0f, EPSILON);
    return 1;
}

/* Symmetric: range spanning zero [-3, 5] -> [-5, 5] */
TEST(symmetric_spanning_zero) {
    float new_min, new_max;
    range_compute_symmetric(-3.0f, 5.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -5.0f, EPSILON);
    ASSERT_NEAR(new_max, 5.0f, EPSILON);
    return 1;
}

/* Symmetric: min has larger abs [-8, 5] -> [-8, 8] */
TEST(symmetric_min_larger_abs) {
    float new_min, new_max;
    range_compute_symmetric(-8.0f, 5.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -8.0f, EPSILON);
    ASSERT_NEAR(new_max, 8.0f, EPSILON);
    return 1;
}

/* Symmetric: already symmetric [-5, 5] -> [-5, 5] */
TEST(symmetric_already_symmetric) {
    float new_min, new_max;
    range_compute_symmetric(-5.0f, 5.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -5.0f, EPSILON);
    ASSERT_NEAR(new_max, 5.0f, EPSILON);
    return 1;
}

/* Symmetric: both zero [0, 0] -> [0, 0] */
TEST(symmetric_both_zero) {
    float new_min, new_max;
    range_compute_symmetric(0.0f, 0.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, 0.0f, EPSILON);
    ASSERT_NEAR(new_max, 0.0f, EPSILON);
    return 1;
}

/* Symmetric: one is zero [0, 7] -> [-7, 7] */
TEST(symmetric_min_zero) {
    float new_min, new_max;
    range_compute_symmetric(0.0f, 7.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -7.0f, EPSILON);
    ASSERT_NEAR(new_max, 7.0f, EPSILON);
    return 1;
}

/* Symmetric: equal magnitude [-5, 5] stays same */
TEST(symmetric_equal_magnitude) {
    float new_min, new_max;
    range_compute_symmetric(-5.0f, 5.0f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -5.0f, EPSILON);
    ASSERT_NEAR(new_max, 5.0f, EPSILON);
    return 1;
}

/* Symmetric: very small values */
TEST(symmetric_small_values) {
    float new_min, new_max;
    range_compute_symmetric(-1e-10f, 2e-10f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -2e-10f, 1e-16f);
    ASSERT_NEAR(new_max, 2e-10f, 1e-16f);
    return 1;
}

/* Symmetric: very large values */
TEST(symmetric_large_values) {
    float new_min, new_max;
    range_compute_symmetric(-1e20f, 5e19f, &new_min, &new_max);
    ASSERT_NEAR(new_min, -1e20f, 1e14f);
    ASSERT_NEAR(new_max, 1e20f, 1e14f);
    return 1;
}

/* Symmetric: result is always symmetric about zero */
TEST(symmetric_result_always_symmetric) {
    float new_min, new_max;

    /* Test multiple cases */
    float test_mins[] = {-3.0f, 0.0f, 1.0f, -100.0f, -0.5f};
    float test_maxs[] = {7.0f, 4.0f, 9.0f, 50.0f, 0.1f};

    for (int i = 0; i < 5; i++) {
        range_compute_symmetric(test_mins[i], test_maxs[i], &new_min, &new_max);
        ASSERT_NEAR(new_min, -new_max, EPSILON);
        ASSERT_GE(new_max, 0.0f);
    }
    return 1;
}

/* Symmetric: result covers original range */
TEST(symmetric_covers_original_range) {
    float new_min, new_max;

    float test_mins[] = {-3.0f, 0.0f, 1.0f, -100.0f, -0.5f};
    float test_maxs[] = {7.0f, 4.0f, 9.0f, 50.0f, 0.1f};

    for (int i = 0; i < 5; i++) {
        range_compute_symmetric(test_mins[i], test_maxs[i], &new_min, &new_max);
        ASSERT_LE(new_min, test_mins[i]);
        ASSERT_GE(new_max, test_maxs[i]);
    }
    return 1;
}

/* ========== range_parse_value tests ========== */

/* Parse integer */
TEST(parse_value_integer) {
    float val = 0.0f;
    ASSERT_EQ_INT(range_parse_value("42", &val), 1);
    ASSERT_NEAR(val, 42.0f, EPSILON);
    return 1;
}

/* Parse float */
TEST(parse_value_float) {
    float val = 0.0f;
    ASSERT_EQ_INT(range_parse_value("3.14", &val), 1);
    ASSERT_NEAR(val, 3.14f, 0.001f);
    return 1;
}

/* Parse negative */
TEST(parse_value_negative) {
    float val = 0.0f;
    ASSERT_EQ_INT(range_parse_value("-7.5", &val), 1);
    ASSERT_NEAR(val, -7.5f, EPSILON);
    return 1;
}

/* Parse scientific notation */
TEST(parse_value_scientific) {
    float val = 0.0f;
    ASSERT_EQ_INT(range_parse_value("1.5e3", &val), 1);
    ASSERT_NEAR(val, 1500.0f, EPSILON);
    return 1;
}

/* Parse negative scientific notation */
TEST(parse_value_negative_scientific) {
    float val = 0.0f;
    ASSERT_EQ_INT(range_parse_value("-2.5e-4", &val), 1);
    ASSERT_NEAR(val, -2.5e-4f, 1e-8f);
    return 1;
}

/* Parse zero */
TEST(parse_value_zero) {
    float val = 99.0f;
    ASSERT_EQ_INT(range_parse_value("0", &val), 1);
    ASSERT_NEAR(val, 0.0f, EPSILON);
    return 1;
}

/* Parse with leading whitespace */
TEST(parse_value_whitespace) {
    float val = 0.0f;
    ASSERT_EQ_INT(range_parse_value("  5.0", &val), 1);
    ASSERT_NEAR(val, 5.0f, EPSILON);
    return 1;
}

/* Parse empty string fails */
TEST(parse_value_empty) {
    float val = 99.0f;
    ASSERT_EQ_INT(range_parse_value("", &val), 0);
    ASSERT_NEAR(val, 99.0f, EPSILON);  /* unchanged */
    return 1;
}

/* Parse non-numeric string fails */
TEST(parse_value_invalid) {
    float val = 99.0f;
    ASSERT_EQ_INT(range_parse_value("abc", &val), 0);
    ASSERT_NEAR(val, 99.0f, EPSILON);  /* unchanged */
    return 1;
}

/* Parse NULL string fails */
TEST(parse_value_null_string) {
    float val = 99.0f;
    ASSERT_EQ_INT(range_parse_value(NULL, &val), 0);
    ASSERT_NEAR(val, 99.0f, EPSILON);  /* unchanged */
    return 1;
}

/* Parse NULL value pointer fails */
TEST(parse_value_null_value) {
    ASSERT_EQ_INT(range_parse_value("5.0", NULL), 0);
    return 1;
}

/* ========== Constants tests ========== */

/* RANGE_POPUP_OK and RANGE_POPUP_CANCEL are distinct */
TEST(constants_distinct) {
    ASSERT_TRUE(RANGE_POPUP_OK != RANGE_POPUP_CANCEL);
    ASSERT_EQ_INT(RANGE_POPUP_OK, 1);
    ASSERT_EQ_INT(RANGE_POPUP_CANCEL, 0);
    return 1;
}

RUN_TESTS("Range Popup")
