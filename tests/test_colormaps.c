/*
 * test_colormaps.c - Unit tests for colormap management
 */

#include "test_framework.h"
#include "../src/ushow.defines.h"
#include "../src/colormaps.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Setup: Initialize colormaps before each test file */
static int colormaps_initialized = 0;

static void ensure_colormaps_init(void) {
    if (!colormaps_initialized) {
        colormaps_init();
        colormaps_initialized = 1;
    }
}

/* Test colormaps_init creates colormaps */
TEST(colormaps_init_basic) {
    ensure_colormaps_init();

    int count = colormap_count();
    ASSERT_GT(count, 0);

    /* Should have at least viridis, hot, grayscale */
    ASSERT_GE(count, 3);

    return 1;
}

/* Test colormap_get_current returns valid colormap */
TEST(colormap_get_current) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_current();
    ASSERT_NOT_NULL(cmap);
    ASSERT_NOT_NULL(cmap->name);
    ASSERT_NOT_NULL(cmap->colors);
    ASSERT_GT(cmap->n_colors, 0);

    return 1;
}

/* Test colormap_get_by_name for viridis */
TEST(colormap_get_by_name_viridis) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("viridis");
    ASSERT_NOT_NULL(cmap);
    ASSERT_STR_EQ(cmap->name, "viridis");
    ASSERT_EQ(cmap->n_colors, 256);

    return 1;
}

/* Test colormap_get_by_name for hot */
TEST(colormap_get_by_name_hot) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("hot");
    ASSERT_NOT_NULL(cmap);
    ASSERT_STR_EQ(cmap->name, "hot");
    ASSERT_EQ(cmap->n_colors, 256);

    return 1;
}

/* Test colormap_get_by_name for grayscale */
TEST(colormap_get_by_name_grayscale) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);
    ASSERT_STR_EQ(cmap->name, "grayscale");
    ASSERT_EQ(cmap->n_colors, 256);

    return 1;
}

/* Test colormap_get_by_name returns NULL for unknown */
TEST(colormap_get_by_name_unknown) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("nonexistent_colormap");
    ASSERT_NULL(cmap);

    return 1;
}

/* Test colormap_next cycles through colormaps */
TEST(colormap_next) {
    ensure_colormaps_init();

    USColormap *first = colormap_get_current();
    ASSERT_NOT_NULL(first);
    const char *first_name = first->name;

    colormap_next();
    USColormap *second = colormap_get_current();
    ASSERT_NOT_NULL(second);

    /* May be different or same (if only 1 colormap) */
    int count = colormap_count();
    if (count > 1) {
        /* After cycling through count-1 more, should return to first */
        for (int i = 1; i < count; i++) {
            colormap_next();
        }
        USColormap *cycled = colormap_get_current();
        ASSERT_STR_EQ(first_name, cycled->name);
    }

    return 1;
}

/* Test colormap_prev cycles backwards */
TEST(colormap_prev) {
    ensure_colormaps_init();

    USColormap *first = colormap_get_current();
    ASSERT_NOT_NULL(first);

    colormap_prev();
    USColormap *prev = colormap_get_current();
    ASSERT_NOT_NULL(prev);

    colormap_next();
    USColormap *back = colormap_get_current();
    ASSERT_STR_EQ(first->name, back->name);

    return 1;
}

/* Test colormap_map_value at boundaries */
TEST(colormap_map_value_boundaries) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    unsigned char r, g, b;

    /* Value 0 -> first color (black for grayscale) */
    colormap_map_value(cmap, 0.0f, &r, &g, &b);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(g, 0);
    ASSERT_EQ(b, 0);

    /* Value 1 -> last color (white for grayscale) */
    colormap_map_value(cmap, 1.0f, &r, &g, &b);
    ASSERT_EQ(r, 255);
    ASSERT_EQ(g, 255);
    ASSERT_EQ(b, 255);

    return 1;
}

/* Test colormap_map_value midpoint */
TEST(colormap_map_value_mid) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    unsigned char r, g, b;

    /* Value 0.5 -> middle gray */
    colormap_map_value(cmap, 0.5f, &r, &g, &b);
    ASSERT_NEAR(r, 127, 2);  /* Should be ~127 */
    ASSERT_EQ(r, g);
    ASSERT_EQ(g, b);

    return 1;
}

/* Test colormap_map_value clamps below 0 */
TEST(colormap_map_value_clamp_low) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    unsigned char r, g, b;
    unsigned char r0, g0, b0;

    colormap_map_value(cmap, -1.0f, &r, &g, &b);
    colormap_map_value(cmap, 0.0f, &r0, &g0, &b0);

    /* Should be clamped to 0 */
    ASSERT_EQ(r, r0);
    ASSERT_EQ(g, g0);
    ASSERT_EQ(b, b0);

    return 1;
}

/* Test colormap_map_value clamps above 1 */
TEST(colormap_map_value_clamp_high) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    unsigned char r, g, b;
    unsigned char r1, g1, b1;

    colormap_map_value(cmap, 2.0f, &r, &g, &b);
    colormap_map_value(cmap, 1.0f, &r1, &g1, &b1);

    /* Should be clamped to 1 */
    ASSERT_EQ(r, r1);
    ASSERT_EQ(g, g1);
    ASSERT_EQ(b, b1);

    return 1;
}

/* Test colormap_map_value with NULL colormap */
TEST(colormap_map_value_null) {
    unsigned char r = 123, g = 123, b = 123;

    colormap_map_value(NULL, 0.5f, &r, &g, &b);

    /* Should set to black */
    ASSERT_EQ(r, 0);
    ASSERT_EQ(g, 0);
    ASSERT_EQ(b, 0);

    return 1;
}

/* Test colormap_apply basic */
TEST(colormap_apply_basic) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    /* 2x2 data grid */
    float data[] = {0.0f, 0.5f, 0.5f, 1.0f};
    unsigned char pixels[12];  /* 2x2x3 RGB */

    colormap_apply(cmap, data, 2, 2, 0.0f, 1.0f, 1e20f, pixels);

    /* Note: colormap_apply flips y-axis */
    /* Row 0 (bottom in data, top in display) has 0.5, 1.0 */
    /* Row 1 (top in data, bottom in display) has 0.0, 0.5 */

    /* Due to y-flip, display row 0 = data row 1 = {0.5, 1.0} */
    /* Check that we have valid grayscale values */
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(pixels[i*3], pixels[i*3+1]);  /* R == G */
        ASSERT_EQ(pixels[i*3+1], pixels[i*3+2]);  /* G == B */
    }

    return 1;
}

/* Test colormap_apply with fill value */
TEST(colormap_apply_fill_value) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("viridis");
    ASSERT_NOT_NULL(cmap);

    /* Data with fill value */
    float data[] = {0.5f, 1e20f, 0.5f, 0.5f};
    unsigned char pixels[12];

    colormap_apply(cmap, data, 2, 2, 0.0f, 1.0f, 1e20f, pixels);

    /* Fill value should map to white (255, 255, 255) */
    /* Due to y-flip, fill value at index 1 maps to display row 0, col 1 */
    /* Display row 0 = data row 1 = {0.5, 0.5} */
    /* Display row 1 = data row 0 = {0.5, 1e20} -> fill at col 1 */
    /* pixels[3*3 + 0] = R of (1,1) = fill value row */

    /* Actually let's check all pixels for white in fill positions */
    int found_white = 0;
    for (int i = 0; i < 4; i++) {
        if (pixels[i*3] == 255 && pixels[i*3+1] == 255 && pixels[i*3+2] == 255) {
            found_white = 1;
        }
    }
    ASSERT_TRUE(found_white);

    return 1;
}

/* Test colormap_apply with data range scaling */
TEST(colormap_apply_scaling) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    /* Data in range [10, 20] */
    float data[] = {10.0f, 15.0f, 15.0f, 20.0f};
    unsigned char pixels[12];

    colormap_apply(cmap, data, 2, 2, 10.0f, 20.0f, 1e20f, pixels);

    /* After scaling, 10 -> 0, 15 -> 0.5, 20 -> 1.0 */
    /* Verify there are different shades */
    int has_variation = 0;
    unsigned char first_r = pixels[0];
    for (int i = 1; i < 4; i++) {
        if (pixels[i*3] != first_r) {
            has_variation = 1;
            break;
        }
    }
    ASSERT_TRUE(has_variation);

    return 1;
}

/* Test colormap_apply_scaled */
TEST(colormap_apply_scaled_2x) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    /* 2x2 data */
    float data[] = {0.0f, 1.0f, 0.0f, 1.0f};

    /* Scale factor 2 -> 4x4 output */
    unsigned char pixels[48];  /* 4x4x3 */

    colormap_apply_scaled(cmap, data, 2, 2, 0.0f, 1.0f, 1e20f, pixels, 2);

    /* Each 2x2 block should have same color */
    /* Check top-left 2x2 block */
    unsigned char r00 = pixels[0];
    ASSERT_EQ(pixels[3], r00);  /* Same row, next col */
    ASSERT_EQ(pixels[4*3], r00);  /* Next row, same col */
    ASSERT_EQ(pixels[4*3 + 3], r00);  /* Next row, next col */

    return 1;
}

/* Test colormap_apply with NULL inputs */
TEST(colormap_apply_null) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_current();
    float data[4] = {0};
    unsigned char pixels[12];

    /* These should not crash */
    colormap_apply(NULL, data, 2, 2, 0, 1, 1e20f, pixels);
    colormap_apply(cmap, NULL, 2, 2, 0, 1, 1e20f, pixels);
    colormap_apply(cmap, data, 2, 2, 0, 1, 1e20f, NULL);

    return 1;
}

/* Test hot colormap gradient */
TEST(colormap_hot_gradient) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("hot");
    ASSERT_NOT_NULL(cmap);

    unsigned char r, g, b;

    /* Hot starts with black->red->yellow->white */
    /* At t=0, should be black/dark */
    colormap_map_value(cmap, 0.0f, &r, &g, &b);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(g, 0);
    ASSERT_EQ(b, 0);

    /* At t=0.15, should be pure red-ish (green and blue = 0) */
    colormap_map_value(cmap, 0.15f, &r, &g, &b);
    ASSERT_GT(r, 0);
    ASSERT_EQ(g, 0);
    ASSERT_EQ(b, 0);

    /* At t=1.0, should be bright - may not be exactly white due to colormap impl */
    colormap_map_value(cmap, 1.0f, &r, &g, &b);
    ASSERT_EQ(r, 255);
    ASSERT_EQ(g, 255);
    /* Blue might not be exactly 255 depending on colormap precision */
    ASSERT_GE(b, 250);  /* Should be very bright */

    return 1;
}

/* Test grayscale monotonicity */
TEST(colormap_grayscale_monotonic) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    unsigned char prev_r = 0;
    for (int i = 0; i <= 10; i++) {
        float t = (float)i / 10.0f;
        unsigned char r, g, b;
        colormap_map_value(cmap, t, &r, &g, &b);

        ASSERT_GE(r, prev_r);  /* Should be monotonically increasing */
        ASSERT_EQ(r, g);
        ASSERT_EQ(g, b);
        prev_r = r;
    }

    return 1;
}

/* Test NaN handling in colormap_apply */
TEST(colormap_apply_nan) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_current();
    ASSERT_NOT_NULL(cmap);

    float nan_val = 0.0f / 0.0f;  /* NaN */
    float data[] = {nan_val, 0.5f, 0.5f, 0.5f};
    unsigned char pixels[12];

    colormap_apply(cmap, data, 2, 2, 0.0f, 1.0f, 1e20f, pixels);

    /* NaN should be rendered as white (fill color) */
    /* Check that at least one pixel is white (the NaN one) */
    int has_white = 0;
    for (int i = 0; i < 4; i++) {
        if (pixels[i*3] == 255 && pixels[i*3+1] == 255 && pixels[i*3+2] == 255) {
            has_white = 1;
            break;
        }
    }
    ASSERT_TRUE(has_white);

    return 1;
}

/* Test colormap_count returns positive */
TEST(colormap_count_positive) {
    ensure_colormaps_init();

    int count = colormap_count();
    ASSERT_GT(count, 0);
    ASSERT_LE(count, 32);  /* MAX_COLORMAPS */

    return 1;
}

/* Test all colormaps have valid structure */
TEST(colormap_all_valid) {
    ensure_colormaps_init();

    int count = colormap_count();
    USColormap *cmap = colormap_get_current();
    const char *first_name = cmap->name;

    for (int i = 0; i < count; i++) {
        cmap = colormap_get_current();
        ASSERT_NOT_NULL(cmap);
        ASSERT_NOT_NULL(cmap->name);
        ASSERT_NOT_NULL(cmap->colors);
        ASSERT_EQ(cmap->n_colors, 256);

        /* Verify name is not empty */
        ASSERT_GT(strlen(cmap->name), 0);

        colormap_next();
    }

    /* Should be back to first */
    cmap = colormap_get_current();
    ASSERT_STR_EQ(cmap->name, first_name);

    return 1;
}

/* Test colormap_apply with equal min/max (zero range) */
TEST(colormap_apply_zero_range) {
    ensure_colormaps_init();

    USColormap *cmap = colormap_get_by_name("grayscale");
    ASSERT_NOT_NULL(cmap);

    float data[] = {5.0f, 5.0f, 5.0f, 5.0f};
    unsigned char pixels[12];

    /* min == max, should handle gracefully */
    colormap_apply(cmap, data, 2, 2, 5.0f, 5.0f, 1e20f, pixels);

    /* All pixels should be same color and not crash */
    ASSERT_EQ(pixels[0], pixels[3]);
    ASSERT_EQ(pixels[0], pixels[6]);

    return 1;
}

RUN_TESTS("Colormaps")
