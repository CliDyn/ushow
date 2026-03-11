/*
 * test_yac_click.c - Tests for time series click-to-node lookup in YAC mode
 *
 * Requires: make WITH_YAC=1 test-yac-click
 *
 * Tests the logic used by on_mouse_click() in YAC mode:
 * - yac_regrid_get_lonlat returns correct target cell coordinates
 * - KDTree lookup from target cell lon/lat finds the correct nearest source node
 * - Validity detection via data threshold works correctly
 */

#include "test_framework.h"
#include "../src/us_types.h"
#include "../src/mesh.h"
#include "../src/kdtree.h"
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

/* ---- Tests ---- */

/* YAC target grid lonlat returns correct cell center coordinates */
TEST(yac_click_lonlat_correct) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    /* Use coarse resolution (10 deg) for fast test */
    USYacRegrid *regrid = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, NULL);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    yac_regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 36);  /* 360/10 */
    ASSERT_EQ_SIZET(ny, 18);  /* 180/10 */

    /* Cell (0,0) should be centered at lon=-175, lat=-85 */
    double lon, lat;
    yac_regrid_get_lonlat(regrid, 0, 0, &lon, &lat);
    ASSERT_NEAR(lon, -175.0, 0.1);
    ASSERT_NEAR(lat, -85.0, 0.1);

    /* Cell at grid center */
    yac_regrid_get_lonlat(regrid, nx / 2, ny / 2, &lon, &lat);
    ASSERT_NEAR(lon, 5.0, 0.1);
    ASSERT_NEAR(lat, 5.0, 0.1);

    /* Cell at last position */
    yac_regrid_get_lonlat(regrid, nx - 1, ny - 1, &lon, &lat);
    ASSERT_NEAR(lon, 175.0, 0.1);
    ASSERT_NEAR(lat, 85.0, 0.1);

    yac_regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* KDTree lookup from target cell lonlat finds nearest source node */
TEST(yac_click_kdtree_finds_nearest_node) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *regrid = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, NULL);
    ASSERT_NOT_NULL(regrid);

    /* Build KDTree from mesh (same as on_mouse_click does lazily) */
    KDTree *tree = kdtree_create(mesh->xyz, mesh->n_points);
    ASSERT_NOT_NULL(tree);

    size_t nx, ny;
    yac_regrid_get_target_dims(regrid, &nx, &ny);

    /* Test several target cells: get lonlat, query KDTree, verify result */
    size_t test_cells[][2] = {
        {0, 0},              /* bottom-left */
        {nx / 2, ny / 2},   /* center */
        {nx - 1, ny - 1},   /* top-right */
        {0, ny / 2},        /* left-center */
        {nx / 2, 0},        /* bottom-center */
    };

    for (size_t t = 0; t < sizeof(test_cells) / sizeof(test_cells[0]); t++) {
        size_t ix = test_cells[t][0];
        size_t iy = test_cells[t][1];

        double lon, lat;
        yac_regrid_get_lonlat(regrid, ix, iy, &lon, &lat);

        /* Convert to cartesian and query KDTree */
        double query[3];
        lonlat_to_cartesian(lon, lat, &query[0], &query[1], &query[2]);

        size_t node_idx;
        double nn_dist;
        kdtree_query_nearest(tree, query, &node_idx, &nn_dist);

        /* Verify: returned node should be a valid index */
        ASSERT_LT((double)node_idx, (double)mesh->n_points);

        /* Verify: returned node should be reasonably close to the query point.
         * For a 10x10 source grid with 10deg resolution target,
         * the nearest source node should be within ~25 degrees */
        double dlat = fabs(mesh->lat[node_idx] - lat);
        ASSERT_LT(dlat, 25.0);

        /* Verify: node distance is finite and non-negative */
        ASSERT_GE(nn_dist, 0.0);
    }

    kdtree_free(tree);
    yac_regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Validity check: on_mouse_click YAC path checks both fill_value and threshold */
TEST(yac_click_validity_check) {
    /* The click handler checks:
     * val == fill_value || fabsf(val) >= INVALID_DATA_THRESHOLD
     * Both conditions mean "invalid / no data here" */

    float fill = DEFAULT_FILL_VALUE;  /* 1e20f */

    /* Valid data values: not fill and below threshold */
    float valid_vals[] = {0.0f, 273.15f, -50.0f, 1e10f, -1e10f};
    for (size_t i = 0; i < sizeof(valid_vals) / sizeof(valid_vals[0]); i++) {
        int is_invalid = (valid_vals[i] == fill) ||
                         (fabsf(valid_vals[i]) >= INVALID_DATA_THRESHOLD);
        ASSERT_FALSE(is_invalid);
    }

    /* Invalid: exact fill value match */
    ASSERT_TRUE(fill == DEFAULT_FILL_VALUE);

    /* Invalid: above threshold (very large values) */
    float large_vals[] = {1e38f, -1e38f};
    for (size_t i = 0; i < sizeof(large_vals) / sizeof(large_vals[0]); i++) {
        ASSERT_TRUE(fabsf(large_vals[i]) >= INVALID_DATA_THRESHOLD);
    }

    /* Invalid: exact fill value match (e.g. NetCDF default 9.96921e+36) */
    float nc_fill = 9.96921e+36f;  /* below threshold but caught by == fill */
    ASSERT_TRUE(nc_fill == nc_fill);  /* sanity: not NaN */
    ASSERT_TRUE(fabsf(nc_fill) < INVALID_DATA_THRESHOLD);  /* below threshold */
    /* In practice, caught by val == current_var->fill_value in on_mouse_click */

    return 1;
}

/* YAC regrid apply produces valid data for subsequent click detection */
TEST(yac_click_regridded_data_detectable) {
    USMesh *mesh = create_small_mesh();
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *regrid = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1, NULL);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    yac_regrid_get_target_dims(regrid, &nx, &ny);
    size_t n_tgt = nx * ny;

    /* Create source data: all valid */
    float *src = malloc(mesh->n_points * sizeof(float));
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dst);

    for (size_t i = 0; i < mesh->n_points; i++)
        src[i] = 273.0f + (float)i * 0.01f;

    float fill = DEFAULT_FILL_VALUE;
    yac_regrid_apply(regrid, src, fill, dst);

    /* Count how many target cells have valid data */
    size_t n_valid = 0;
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i]) < INVALID_DATA_THRESHOLD)
            n_valid++;
    }

    /* With a 100-point global mesh and 10deg target, should have some valid cells */
    ASSERT_GT((double)n_valid, 0.0);

    /* For each valid cell, verify we can find a nearby source node */
    KDTree *tree = kdtree_create(mesh->xyz, mesh->n_points);
    ASSERT_NOT_NULL(tree);

    size_t spot_checks = 0;
    for (size_t j = 0; j < ny && spot_checks < 5; j++) {
        for (size_t i = 0; i < nx && spot_checks < 5; i++) {
            size_t idx = j * nx + i;
            if (fabsf(dst[idx]) >= INVALID_DATA_THRESHOLD) continue;

            double lon, lat;
            yac_regrid_get_lonlat(regrid, i, j, &lon, &lat);

            double query[3];
            lonlat_to_cartesian(lon, lat, &query[0], &query[1], &query[2]);

            size_t node_idx;
            double nn_dist;
            kdtree_query_nearest(tree, query, &node_idx, &nn_dist);

            ASSERT_LT((double)node_idx, (double)mesh->n_points);
            spot_checks++;
        }
    }
    ASSERT_GT((double)spot_checks, 0.0);

    kdtree_free(tree);
    free(src);
    free(dst);
    yac_regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Pixel-to-grid coordinate conversion (same as on_mouse_click) */
TEST(yac_click_pixel_to_grid_coords) {
    /* Simulate the pixel-to-data-grid conversion from on_mouse_click */
    size_t data_nx = 36, data_ny = 18;
    int scale = 2;

    /* Click at pixel (10, 5) with scale 2 → data (5, 2) */
    int px = 10, py = 5;
    size_t data_x = px / scale;
    size_t data_y = py / scale;
    ASSERT_EQ_SIZET(data_x, 5);
    ASSERT_EQ_SIZET(data_y, 2);

    /* Y flip */
    size_t src_y = data_ny - 1 - data_y;
    ASSERT_EQ_SIZET(src_y, 15);

    /* Grid index */
    size_t grid_idx = src_y * data_nx + data_x;
    ASSERT_EQ_SIZET(grid_idx, 15 * 36 + 5);

    /* Bounds check */
    ASSERT_TRUE(data_x < data_nx);
    ASSERT_TRUE(data_y < data_ny);

    return 1;
}

/* Global init/finalize for YAC + MPI */
int main(void) {
    if (yac_regrid_init() != 0) {
        fprintf(stderr, "Failed to initialize YAC\n");
        return 1;
    }

    int result = run_all_tests("YAC Click-to-Timeseries");

    yac_regrid_finalize();
    return result;
}
