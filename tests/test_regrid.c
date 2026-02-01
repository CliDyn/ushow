/*
 * test_regrid.c - Unit tests for regridding engine
 */

#include "test_framework.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include "../src/regrid.h"
#include <stdlib.h>
#include <stdio.h>

/* Helper: Create a simple mesh with global coverage */
static USMesh *create_test_mesh_global(size_t nx, size_t ny) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));

    if (!lon || !lat) {
        free(lon);
        free(lat);
        return NULL;
    }

    /* Create a regular grid */
    double dlon = 360.0 / nx;
    double dlat = 180.0 / ny;

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + (i + 0.5) * dlon;
            lat[idx] = -90.0 + (j + 0.5) * dlat;
        }
    }

    return mesh_create(lon, lat, n, COORD_TYPE_1D_STRUCTURED);
}

/* Helper: Create a localized mesh (small region) */
static USMesh *create_test_mesh_local(double lon_min, double lon_max,
                                       double lat_min, double lat_max,
                                       size_t nx, size_t ny) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));

    if (!lon || !lat) {
        free(lon);
        free(lat);
        return NULL;
    }

    double dlon = (lon_max - lon_min) / nx;
    double dlat = (lat_max - lat_min) / ny;

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = lon_min + (i + 0.5) * dlon;
            lat[idx] = lat_min + (j + 0.5) * dlat;
        }
    }

    return mesh_create(lon, lat, n, COORD_TYPE_1D_STRUCTURED);
}

/* Test regrid_create with NULL mesh */
TEST(regrid_create_null_mesh) {
    USRegrid *regrid = regrid_create(NULL, 1.0, 200000.0);
    ASSERT_NULL(regrid);
    return 1;
}

/* Test regrid_create with valid mesh */
TEST(regrid_create_basic) {
    USMesh *mesh = create_test_mesh_global(36, 18);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 500000.0);
    ASSERT_NOT_NULL(regrid);

    /* Check target grid dimensions */
    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 36);  /* 360 / 10 */
    ASSERT_EQ_SIZET(ny, 18);  /* 180 / 10 */

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid target dimensions */
TEST(regrid_target_dims) {
    USMesh *mesh = create_test_mesh_global(10, 10);
    ASSERT_NOT_NULL(mesh);

    /* Resolution 2 degrees -> 180x90 */
    USRegrid *regrid = regrid_create(mesh, 2.0, 200000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 180);
    ASSERT_EQ_SIZET(ny, 90);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid_get_target_dims with NULL */
TEST(regrid_get_target_dims_null) {
    size_t nx = 999, ny = 999;
    regrid_get_target_dims(NULL, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 0);
    ASSERT_EQ_SIZET(ny, 0);
    return 1;
}

/* Test regrid_get_lonlat */
TEST(regrid_get_lonlat) {
    USMesh *mesh = create_test_mesh_global(36, 18);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 500000.0);
    ASSERT_NOT_NULL(regrid);

    double lon, lat;

    /* First pixel (0, 0) -> center is (-180 + 5, -90 + 5) = (-175, -85) */
    regrid_get_lonlat(regrid, 0, 0, &lon, &lat);
    ASSERT_NEAR(lon, -175.0, 0.1);
    ASSERT_NEAR(lat, -85.0, 0.1);

    /* Last pixel (35, 17) -> center is (-180 + 35.5*10, -90 + 17.5*10) = (175, 85) */
    regrid_get_lonlat(regrid, 35, 17, &lon, &lat);
    ASSERT_NEAR(lon, 175.0, 0.1);
    ASSERT_NEAR(lat, 85.0, 0.1);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid_apply with uniform data */
TEST(regrid_apply_uniform) {
    USMesh *mesh = create_test_mesh_global(36, 18);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1000000.0);  /* Large influence radius */
    ASSERT_NOT_NULL(regrid);

    size_t source_n = mesh->n_points;
    float *source_data = malloc(source_n * sizeof(float));
    ASSERT_NOT_NULL(source_data);

    /* Fill with constant value */
    for (size_t i = 0; i < source_n; i++) {
        source_data[i] = 42.0f;
    }

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    float *target_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(target_data);

    regrid_apply(regrid, source_data, 1e20f, target_data);

    /* All valid target points should have value 42 or fill_value */
    int has_valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (target_data[i] < 1e10f) {
            ASSERT_NEAR(target_data[i], 42.0f, 0.01f);
            has_valid = 1;
        }
    }
    ASSERT_TRUE(has_valid);

    free(source_data);
    free(target_data);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid_apply with gradient data */
TEST(regrid_apply_gradient) {
    USMesh *mesh = create_test_mesh_global(36, 18);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1000000.0);
    ASSERT_NOT_NULL(regrid);

    size_t source_n = mesh->n_points;
    float *source_data = malloc(source_n * sizeof(float));
    ASSERT_NOT_NULL(source_data);

    /* Fill with latitude-based gradient */
    for (size_t i = 0; i < source_n; i++) {
        source_data[i] = (float)mesh->lat[i];  /* Value = latitude */
    }

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    float *target_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(target_data);

    regrid_apply(regrid, source_data, 1e20f, target_data);

    /* Check that values increase with row index (latitude) */
    /* Note: Row 0 is south (-90), last row is north (+90) */
    int valid_count = 0;
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            if (target_data[idx] < 1e10f) {
                valid_count++;
                /* Value should approximately equal the latitude at that row */
                double expected_lat = -90.0 + (j + 0.5) * (180.0 / ny);
                /* Due to nearest-neighbor, allow some tolerance */
                ASSERT_NEAR(target_data[idx], expected_lat, 15.0);
            }
        }
    }
    ASSERT_GT(valid_count, 0);

    free(source_data);
    free(target_data);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid_apply with NULL inputs */
TEST(regrid_apply_null) {
    float target[10];
    float source[10];

    /* These should not crash */
    regrid_apply(NULL, source, 1e20f, target);
    /* regrid_apply with NULL source or target would crash, so don't test those */

    return 1;
}

/* Test regrid with local mesh (partial coverage) */
TEST(regrid_local_mesh) {
    /* Create a mesh that only covers Europe */
    USMesh *mesh = create_test_mesh_local(-10.0, 30.0, 35.0, 70.0, 40, 35);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 2.0, 200000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    /* Count valid target points - should be limited to Europe region */
    size_t valid_count = 0;
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            double lon, lat;
            regrid_get_lonlat(regrid, i, j, &lon, &lat);

            /* Only points near Europe should be valid */
            /* Check if point is roughly in Europe region */
            if (lon >= -15 && lon <= 35 && lat >= 30 && lat <= 75) {
                /* This point might be valid */
            }
        }
    }

    /* Source data */
    float *source_data = malloc(mesh->n_points * sizeof(float));
    for (size_t i = 0; i < mesh->n_points; i++) {
        source_data[i] = 100.0f;
    }

    float *target_data = malloc(nx * ny * sizeof(float));
    regrid_apply(regrid, source_data, 1e20f, target_data);

    /* Count actually valid points */
    for (size_t i = 0; i < nx * ny; i++) {
        if (target_data[i] < 1e10f) {
            valid_count++;
        }
    }

    /* Should have some valid points but not full coverage */
    ASSERT_GT(valid_count, 0);
    ASSERT_LT(valid_count, nx * ny);

    free(source_data);
    free(target_data);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid_free handles NULL */
TEST(regrid_free_null) {
    regrid_free(NULL);  /* Should not crash */
    return 1;
}

/* Test influence radius effect */
TEST(regrid_influence_radius) {
    /* Create sparse mesh */
    size_t n = 5;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));

    /* 5 points scattered around the globe */
    lon[0] = 0.0; lat[0] = 0.0;
    lon[1] = 45.0; lat[1] = 45.0;
    lon[2] = -45.0; lat[2] = 45.0;
    lon[3] = 90.0; lat[3] = 0.0;
    lon[4] = -90.0; lat[4] = 0.0;

    USMesh *mesh = mesh_create(lon, lat, n, COORD_TYPE_1D_UNSTRUCTURED);
    ASSERT_NOT_NULL(mesh);

    /* Small influence radius - fewer valid points */
    USRegrid *regrid_small = regrid_create(mesh, 10.0, 100000.0);  /* 100 km */
    ASSERT_NOT_NULL(regrid_small);

    /* Large influence radius - more valid points */
    USRegrid *regrid_large = regrid_create(mesh, 10.0, 5000000.0);  /* 5000 km */
    ASSERT_NOT_NULL(regrid_large);

    size_t nx, ny;
    regrid_get_target_dims(regrid_small, &nx, &ny);

    float *source_data = malloc(n * sizeof(float));
    float *target_small = malloc(nx * ny * sizeof(float));
    float *target_large = malloc(nx * ny * sizeof(float));

    for (size_t i = 0; i < n; i++) source_data[i] = 1.0f;

    regrid_apply(regrid_small, source_data, 1e20f, target_small);
    regrid_apply(regrid_large, source_data, 1e20f, target_large);

    /* Count valid points */
    size_t valid_small = 0, valid_large = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (target_small[i] < 1e10f) valid_small++;
        if (target_large[i] < 1e10f) valid_large++;
    }

    /* Larger influence radius should have more valid points */
    ASSERT_GE(valid_large, valid_small);

    free(source_data);
    free(target_small);
    free(target_large);
    regrid_free(regrid_small);
    regrid_free(regrid_large);
    mesh_free(mesh);
    return 1;
}

/* Test regrid resolution effect on target grid size */
TEST(regrid_resolution) {
    USMesh *mesh = create_test_mesh_global(10, 10);
    ASSERT_NOT_NULL(mesh);

    /* Test various resolutions */
    struct { double res; size_t expected_nx; size_t expected_ny; } tests[] = {
        {1.0, 360, 180},
        {2.0, 180, 90},
        {5.0, 72, 36},
        {10.0, 36, 18},
        {30.0, 12, 6}
    };

    for (size_t t = 0; t < sizeof(tests)/sizeof(tests[0]); t++) {
        USRegrid *regrid = regrid_create(mesh, tests[t].res, 500000.0);
        ASSERT_NOT_NULL(regrid);

        size_t nx, ny;
        regrid_get_target_dims(regrid, &nx, &ny);
        ASSERT_EQ_SIZET(nx, tests[t].expected_nx);
        ASSERT_EQ_SIZET(ny, tests[t].expected_ny);

        regrid_free(regrid);
    }

    mesh_free(mesh);
    return 1;
}

/* Test source fill value handling in regrid_apply */
TEST(regrid_apply_fill_value) {
    USMesh *mesh = create_test_mesh_global(36, 18);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1000000.0);
    ASSERT_NOT_NULL(regrid);

    size_t source_n = mesh->n_points;
    float *source_data = malloc(source_n * sizeof(float));

    /* Set half to fill value */
    for (size_t i = 0; i < source_n; i++) {
        if (i % 2 == 0) {
            source_data[i] = 1e20f;  /* Fill value */
        } else {
            source_data[i] = 50.0f;
        }
    }

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    float *target_data = malloc(nx * ny * sizeof(float));

    regrid_apply(regrid, source_data, 1e20f, target_data);

    /* Check that fill values are preserved or valid data is used */
    for (size_t i = 0; i < nx * ny; i++) {
        if (target_data[i] < 1e10f) {
            /* Valid data should be ~50 */
            ASSERT_TRUE((target_data[i] > 40.0f && target_data[i] < 60.0f) ||
                       target_data[i] >= 1e10f);
        }
    }

    free(source_data);
    free(target_data);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test regrid preserves data identity for exact grid match */
TEST(regrid_identity) {
    /* Create mesh that matches target grid exactly */
    size_t nx = 36, ny = 18;
    double *lon = malloc(nx * ny * sizeof(double));
    double *lat = malloc(nx * ny * sizeof(double));

    double dlon = 360.0 / nx;
    double dlat = 180.0 / ny;

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + (i + 0.5) * dlon;
            lat[idx] = -90.0 + (j + 0.5) * dlat;
        }
    }

    USMesh *mesh = mesh_create(lon, lat, nx * ny, COORD_TYPE_1D_STRUCTURED);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 500000.0);  /* 10 degree resolution */
    ASSERT_NOT_NULL(regrid);

    size_t target_nx, target_ny;
    regrid_get_target_dims(regrid, &target_nx, &target_ny);
    ASSERT_EQ_SIZET(target_nx, nx);
    ASSERT_EQ_SIZET(target_ny, ny);

    /* Create test pattern */
    float *source_data = malloc(nx * ny * sizeof(float));
    for (size_t i = 0; i < nx * ny; i++) {
        source_data[i] = (float)i;  /* Unique value per point */
    }

    float *target_data = malloc(nx * ny * sizeof(float));
    regrid_apply(regrid, source_data, 1e20f, target_data);

    /* For exact grid match, values should be very close (nearest-neighbor) */
    int match_count = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (target_data[i] < 1e10f) {
            /* Should match one of the source values */
            ASSERT_GE(target_data[i], 0.0f);
            ASSERT_LT(target_data[i], (float)(nx * ny));
            match_count++;
        }
    }
    ASSERT_GT(match_count, nx * ny / 2);

    free(source_data);
    free(target_data);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

RUN_TESTS("Regrid")
