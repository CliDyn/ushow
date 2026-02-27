/*
 * test_yac_regional.c - Tests for YAC --box and --polar regional support
 *
 * Requires: make WITH_YAC=1 test-yac-regional
 *
 * Tests that yac_regrid_create() respects USTargetConfig for:
 * - Regional box selection (equirectangular with non-global bounds)
 * - LAEA polar projections (north and south)
 * - yac_regrid_is_regional() detection
 * - yac_regrid_get_lonlat() for both equirectangular and LAEA
 */

#include "test_framework.h"
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include "../src/projection.h"
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

/* ---- is_regional tests ---- */

TEST(regional_global_not_regional) {
    /* NULL config → global defaults → not regional */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_FALSE(yac_regrid_is_regional(r));

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(regional_explicit_global_not_regional) {
    /* Explicit global config → not regional */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);
    ASSERT_FALSE(yac_regrid_is_regional(r));

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(regional_box_is_regional) {
    /* --box bounds → regional */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 50.0;
    cfg.lat_max = 80.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(yac_regrid_is_regional(r));

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(regional_polar_north_is_regional) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(yac_regrid_is_regional(r));

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(regional_polar_south_is_regional) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_SOUTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(yac_regrid_is_regional(r));

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(regional_is_regional_null_safe) {
    ASSERT_FALSE(yac_regrid_is_regional(NULL));
    return 1;
}

/* ---- Box target grid tests ---- */

TEST(box_target_dims_smaller_than_global) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    /* Global: 360/10 = 36 x 180/10 = 18 */
    USYacRegrid *r_global = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, NULL);
    ASSERT_NOT_NULL(r_global);
    size_t gx, gy;
    yac_regrid_get_target_dims(r_global, &gx, &gy);
    ASSERT_EQ_SIZET(gx, 36);
    ASSERT_EQ_SIZET(gy, 18);

    /* Box: 40/10 = 4 x 30/10 = 3 */
    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 50.0;
    cfg.lat_max = 80.0;

    USYacRegrid *r_box = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r_box);
    size_t bx, by;
    yac_regrid_get_target_dims(r_box, &bx, &by);
    ASSERT_EQ_SIZET(bx, 4);
    ASSERT_EQ_SIZET(by, 3);

    /* Box grid should be smaller */
    ASSERT_LT((double)bx, (double)gx);
    ASSERT_LT((double)by, (double)gy);

    yac_regrid_free(r_global);
    yac_regrid_free(r_box);
    mesh_free(mesh);
    return 1;
}

TEST(box_lonlat_within_bounds) {
    /* All target cell centers should be within the box bounds */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 50.0;
    cfg.lat_max = 80.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            double lon, lat;
            yac_regrid_get_lonlat(r, i, j, &lon, &lat);
            ASSERT_GE(lon, cfg.lon_min);
            ASSERT_LE(lon, cfg.lon_max);
            ASSERT_GE(lat, cfg.lat_min);
            ASSERT_LE(lat, cfg.lat_max);
        }
    }

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(box_apply_produces_valid_output) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 50.0;
    cfg.lat_max = 80.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    for (size_t i = 0; i < mesh->n_points; i++)
        src[i] = 273.0f + (float)(mesh->lat[i] * 0.5);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

/* ---- LAEA polar target grid tests ---- */

TEST(polar_north_target_grid_square) {
    /* LAEA grids should be square (nx == ny) */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    ASSERT_EQ_SIZET(nx, ny);
    ASSERT_GT((double)nx, 0.0);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(polar_north_lonlat_near_pole) {
    /* Center of LAEA north should be near the north pole */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);

    /* Center cell should be near north pole (lat close to 90) */
    double lon, lat;
    yac_regrid_get_lonlat(r, nx / 2, ny / 2, &lon, &lat);
    ASSERT_GT(lat, 80.0);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(polar_south_lonlat_near_pole) {
    /* Center of LAEA south should be near the south pole */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_SOUTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);

    /* Center cell should be near south pole (lat close to -90) */
    double lon, lat;
    yac_regrid_get_lonlat(r, nx / 2, ny / 2, &lon, &lat);
    ASSERT_LT(lat, -80.0);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(polar_north_all_lats_above_cutoff) {
    /* All inner cells of LAEA north grid should have lat >= cutoff */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);

    /* Check a subset of cells that are inside the projection disk.
     * The center quarter should all be above cutoff. */
    size_t margin = nx / 4;
    for (size_t j = margin; j < ny - margin; j++) {
        for (size_t i = margin; i < nx - margin; i++) {
            double lon, lat;
            yac_regrid_get_lonlat(r, i, j, &lon, &lat);
            /* Inner cells should have lat >= cutoff (with tolerance for
             * projection edge effects) */
            ASSERT_GE(lat, cfg.cutoff_lat - 5.0);
        }
    }

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(polar_north_apply_produces_valid_output) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    for (size_t i = 0; i < mesh->n_points; i++)
        src[i] = 273.0f + (float)(mesh->lat[i] * 0.5);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(polar_south_apply_produces_valid_output) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_SOUTH;
    cfg.cutoff_lat = 60.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    for (size_t i = 0; i < mesh->n_points; i++)
        src[i] = 273.0f + (float)(mesh->lat[i] * 0.5);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

/* ---- 3D rebuild path with config ---- */

TEST(box_3d_rebuild_respects_config) {
    /* When --yac-3d rebuilds masked interpolation, it should use
     * the stored config (box bounds), not global defaults */
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 50.0;
    cfg.lat_max = 80.0;

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_NNN_1, &cfg);
    ASSERT_NOT_NULL(r);

    yac_regrid_enable_3d(r, mesh);
    ASSERT_TRUE(yac_regrid_is_3d(r));

    size_t n_src = mesh->n_points;
    float *src = malloc(n_src * sizeof(float));
    ASSERT_NOT_NULL(src);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;

    /* Create data with some fill values to force a 3D rebuild */
    for (size_t i = 0; i < n_src; i++) {
        if (i % 3 == 0)
            src[i] = fill;
        else
            src[i] = 273.0f + (float)i * 0.01f;
    }

    /* This triggers build_interpolation with stored config */
    yac_regrid_apply_3d(r, 0, 3, src, fill, dst);

    /* Grid dims should still match box (not global) */
    size_t nx2, ny2;
    yac_regrid_get_target_dims(r, &nx2, &ny2);
    ASSERT_EQ_SIZET(nx, nx2);
    ASSERT_EQ_SIZET(ny, ny2);

    /* Output should have some valid values */
    int n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

/* Global init/finalize for YAC + MPI */
int main(void) {
    if (yac_regrid_init() != 0) {
        fprintf(stderr, "Failed to initialize YAC\n");
        return 1;
    }

    int result = run_all_tests("YAC Regional (--box and --polar)");

    yac_regrid_finalize();
    return result;
}
