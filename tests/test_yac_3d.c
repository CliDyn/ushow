/*
 * test_yac_3d.c - Unit tests for per-depth masked YAC interpolation (--yac-3d)
 *
 * Requires: make WITH_YAC=1 test-yac-3d
 *
 * Tests the depth-cache API: enable_3d, is_3d, clear_depth_cache, apply_3d.
 * Uses a small point cloud so YAC setup is fast (~0.1s per interpolation).
 */

#include "test_framework.h"
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include "../src/regrid_yac.h"
#include <stdlib.h>
#include <math.h>

/* Small mesh: 100 points on a regular 10x10 lat-lon grid */
static USMesh *create_small_mesh(void) {
    int nx = 10, ny = 10;
    size_t n = (size_t)(nx * ny);
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));
    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    size_t idx = 0;
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            lon[idx] = -180.0 + 360.0 * (i + 0.5) / nx;
            lat[idx] = -90.0 + 180.0 * (j + 0.5) / ny;
            idx++;
        }
    }
    return mesh_create(lon, lat, n, COORD_TYPE_1D_UNSTRUCTURED);
}

/* Fill src buffer: all valid data */
static void fill_all_valid(float *src, size_t n, float fill) {
    (void)fill;
    for (size_t i = 0; i < n; i++)
        src[i] = 10.0f + (float)i * 0.01f;
}

/* Fill src buffer: some points are fill (simulating land mask) */
static void fill_with_mask(float *src, size_t n, float fill, int every_nth) {
    for (size_t i = 0; i < n; i++) {
        if ((int)i % every_nth == 0)
            src[i] = fill;  /* "land" */
        else
            src[i] = 10.0f + (float)i * 0.01f;
    }
}

/* ---- Tests ---- */

static USMesh *g_mesh = NULL;
static USYacRegrid *g_regrid = NULL;

static int setup(void) {
    g_mesh = create_small_mesh();
    if (!g_mesh) return 0;
    g_regrid = yac_regrid_create(g_mesh, 10.0, YAC_METHOD_NNN_1);
    return g_regrid != NULL;
}

static void teardown(void) {
    if (g_regrid) { yac_regrid_free(g_regrid); g_regrid = NULL; }
    if (g_mesh) { mesh_free(g_mesh); g_mesh = NULL; }
}

TEST(yac3d_not_enabled_by_default) {
    ASSERT_TRUE(setup());
    ASSERT_FALSE(yac_regrid_is_3d(g_regrid));
    teardown();
    return 1;
}

TEST(yac3d_enable) {
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);
    ASSERT_TRUE(yac_regrid_is_3d(g_regrid));
    teardown();
    return 1;
}

TEST(yac3d_is_3d_null_safe) {
    ASSERT_FALSE(yac_regrid_is_3d(NULL));
    return 1;
}

TEST(yac3d_clear_empty_cache) {
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);
    /* Clearing before any apply should be safe (no-op) */
    yac_regrid_clear_depth_cache(g_regrid);
    ASSERT_TRUE(yac_regrid_is_3d(g_regrid));
    teardown();
    return 1;
}

TEST(yac3d_clear_null_safe) {
    yac_regrid_clear_depth_cache(NULL);
    return 1;
}

TEST(yac3d_apply_all_valid_reuses_base) {
    /* When all source data is valid, depth cache should use the base
     * interpolation (sentinel). Verify by checking output is reasonable. */
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);

    size_t n_src = g_mesh->n_points;
    float *src = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src);

    size_t nx, ny;
    yac_regrid_get_target_dims(g_regrid, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    fill_all_valid(src, n_src, fill);

    /* Apply for depth 0 of 5 depths */
    yac_regrid_apply_3d(g_regrid, 0, 5, src, fill, dst);

    /* Output should have some non-fill values */
    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    teardown();
    return 1;
}

TEST(yac3d_apply_with_mask_builds_new_interp) {
    /* When some source points are fill, a new masked interpolation is built. */
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);

    size_t n_src = g_mesh->n_points;
    float *src = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src);

    size_t nx, ny;
    yac_regrid_get_target_dims(g_regrid, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    /* Every 3rd point is fill (masked) */
    fill_with_mask(src, n_src, fill, 3);

    /* Apply for depth 2 of 5 depths */
    yac_regrid_apply_3d(g_regrid, 2, 5, src, fill, dst);

    /* Output should have some non-fill values */
    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    teardown();
    return 1;
}

TEST(yac3d_cached_second_call_fast) {
    /* Second call to same depth should use cached interpolation.
     * We can't measure time precisely but verify it doesn't crash
     * and produces same results. */
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);

    size_t n_src = g_mesh->n_points;
    float *src = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src);

    size_t nx, ny;
    yac_regrid_get_target_dims(g_regrid, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst1 = malloc(n_tgt * sizeof(float));
    float *dst2 = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst1);
    ASSERT_NOT_NULL(dst2);

    float fill = 1.0e20f;
    fill_with_mask(src, n_src, fill, 4);

    /* First call builds the cached interpolation */
    yac_regrid_apply_3d(g_regrid, 1, 3, src, fill, dst1);
    /* Second call should reuse it */
    yac_regrid_apply_3d(g_regrid, 1, 3, src, fill, dst2);

    /* Results should be identical */
    for (size_t i = 0; i < n_tgt; i++) {
        ASSERT_NEAR(dst1[i], dst2[i], 1e-6);
    }

    free(src);
    free(dst1);
    free(dst2);
    teardown();
    return 1;
}

TEST(yac3d_different_depths_different_masks) {
    /* Two depth levels with different masks should produce different results */
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);

    size_t n_src = g_mesh->n_points;
    float *src_d0 = malloc(n_src * sizeof(float));
    float *src_d1 = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src_d0);
    ASSERT_NOT_NULL(src_d1);

    size_t nx, ny;
    yac_regrid_get_target_dims(g_regrid, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst0 = malloc(n_tgt * sizeof(float));
    float *dst1 = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst0);
    ASSERT_NOT_NULL(dst1);

    float fill = 1.0e20f;

    /* Depth 0: all valid */
    fill_all_valid(src_d0, n_src, fill);
    /* Depth 1: heavy masking (every 2nd point) */
    fill_with_mask(src_d1, n_src, fill, 2);

    yac_regrid_apply_3d(g_regrid, 0, 2, src_d0, fill, dst0);
    yac_regrid_apply_3d(g_regrid, 1, 2, src_d1, fill, dst1);

    /* The results should differ (different masks, different source data) */
    int n_differ = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst0[i] - dst1[i]) > 1e-6f) n_differ++;
    }
    ASSERT_GT(n_differ, 0);

    free(src_d0);
    free(src_d1);
    free(dst0);
    free(dst1);
    teardown();
    return 1;
}

TEST(yac3d_clear_then_rebuild) {
    /* After clearing cache, next apply should rebuild */
    ASSERT_TRUE(setup());
    yac_regrid_enable_3d(g_regrid, g_mesh);

    size_t n_src = g_mesh->n_points;
    float *src = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src);

    size_t nx, ny;
    yac_regrid_get_target_dims(g_regrid, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst1 = malloc(n_tgt * sizeof(float));
    float *dst2 = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst1);
    ASSERT_NOT_NULL(dst2);

    float fill = 1.0e20f;
    fill_with_mask(src, n_src, fill, 5);

    /* Build cache */
    yac_regrid_apply_3d(g_regrid, 0, 3, src, fill, dst1);

    /* Clear and rebuild */
    yac_regrid_clear_depth_cache(g_regrid);
    yac_regrid_apply_3d(g_regrid, 0, 3, src, fill, dst2);

    /* Results should be the same */
    for (size_t i = 0; i < n_tgt; i++) {
        ASSERT_NEAR(dst1[i], dst2[i], 1e-6);
    }

    free(src);
    free(dst1);
    free(dst2);
    teardown();
    return 1;
}

TEST(yac3d_non_3d_apply_still_works) {
    /* When 3d is not enabled, regular apply should work fine */
    ASSERT_TRUE(setup());
    ASSERT_FALSE(yac_regrid_is_3d(g_regrid));

    size_t n_src = g_mesh->n_points;
    float *src = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src);

    size_t nx, ny;
    yac_regrid_get_target_dims(g_regrid, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    fill_all_valid(src, n_src, fill);

    yac_regrid_apply(g_regrid, src, fill, dst);

    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    teardown();
    return 1;
}

/* Global init/finalize for YAC + MPI */
int main(void) {
    if (yac_regrid_init() != 0) {
        fprintf(stderr, "Failed to initialize YAC\n");
        return 1;
    }

    int result = run_all_tests("YAC 3D Per-Depth Masking");

    yac_regrid_finalize();
    return result;
}
