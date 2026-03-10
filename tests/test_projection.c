/*
 * test_projection.c - Unit tests for projection, regional box, and polar grid
 */

#include "test_framework.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include "../src/us_types.h"
#include "../src/mesh.h"
#include "../src/regrid.h"
#include "../src/projection.h"
#include <stdlib.h>
#include <stdio.h>

/* ========== target_config_init_default ========== */

TEST(target_config_defaults) {
    USTargetConfig cfg;
    target_config_init_default(&cfg);

    ASSERT_EQ_INT(cfg.projection, PROJ_EQUIRECTANGULAR);
    ASSERT_NEAR(cfg.lon_min, -180.0, 0.01);
    ASSERT_NEAR(cfg.lon_max, 180.0, 0.01);
    ASSERT_NEAR(cfg.lat_min, -90.0, 0.01);
    ASSERT_NEAR(cfg.lat_max, 90.0, 0.01);
    ASSERT_NEAR(cfg.cutoff_lat, 60.0, 0.01);
    return 1;
}

/* ========== LAEA forward projection ========== */

/* North pole projects to origin */
TEST(laea_forward_north_pole) {
    double x, y;
    laea_forward(0.0, 90.0, 1, EARTH_RADIUS_M, &x, &y);
    ASSERT_NEAR(x, 0.0, 1.0);
    ASSERT_NEAR(y, 0.0, 1.0);
    return 1;
}

/* South pole projects to origin */
TEST(laea_forward_south_pole) {
    double x, y;
    laea_forward(0.0, -90.0, -1, EARTH_RADIUS_M, &x, &y);
    ASSERT_NEAR(x, 0.0, 1.0);
    ASSERT_NEAR(y, 0.0, 1.0);
    return 1;
}

/* Equator at lon=0 for north pole projection: should project to negative y */
TEST(laea_forward_equator_north) {
    double x, y;
    laea_forward(0.0, 0.0, 1, EARTH_RADIUS_M, &x, &y);
    ASSERT_NEAR(x, 0.0, 1.0);
    ASSERT_LT(y, 0.0);  /* Below the pole */
    /* Distance from origin should be R*sqrt(2) for 90 degrees from pole */
    double rho = sqrt(x * x + y * y);
    ASSERT_NEAR(rho, EARTH_RADIUS_M * sqrt(2.0), 1000.0);
    return 1;
}

/* Points at same latitude should be equidistant from origin */
TEST(laea_forward_symmetric) {
    double x1, y1, x2, y2;
    laea_forward(30.0, 60.0, 1, EARTH_RADIUS_M, &x1, &y1);
    laea_forward(-30.0, 60.0, 1, EARTH_RADIUS_M, &x2, &y2);

    double r1 = sqrt(x1 * x1 + y1 * y1);
    double r2 = sqrt(x2 * x2 + y2 * y2);
    ASSERT_NEAR(r1, r2, 1.0);
    /* x should be opposite */
    ASSERT_NEAR(x1, -x2, 1.0);
    return 1;
}

/* ========== LAEA inverse projection ========== */

/* Origin inverts to pole */
TEST(laea_inverse_origin_north) {
    double lon, lat;
    int rc = laea_inverse(0.0, 0.0, 1, EARTH_RADIUS_M, &lon, &lat);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_NEAR(lat, 90.0, 0.01);
    return 1;
}

TEST(laea_inverse_origin_south) {
    double lon, lat;
    int rc = laea_inverse(0.0, 0.0, -1, EARTH_RADIUS_M, &lon, &lat);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_NEAR(lat, -90.0, 0.01);
    return 1;
}

/* Forward then inverse should roundtrip */
TEST(laea_roundtrip_north) {
    double test_points[][2] = {
        {0.0, 80.0}, {45.0, 70.0}, {-90.0, 60.0}, {180.0, 50.0},
        {120.0, 85.0}, {-60.0, 45.0}
    };
    int n = sizeof(test_points) / sizeof(test_points[0]);

    for (int i = 0; i < n; i++) {
        double lon_in = test_points[i][0];
        double lat_in = test_points[i][1];
        double x, y, lon_out, lat_out;

        laea_forward(lon_in, lat_in, 1, EARTH_RADIUS_M, &x, &y);
        int rc = laea_inverse(x, y, 1, EARTH_RADIUS_M, &lon_out, &lat_out);
        ASSERT_EQ_INT(rc, 0);
        ASSERT_NEAR(lat_out, lat_in, 0.001);

        /* Normalize longitudes to [-180, 180] for comparison */
        double diff = fmod(lon_out - lon_in + 540.0, 360.0) - 180.0;
        ASSERT_NEAR(diff, 0.0, 0.001);
    }
    return 1;
}

TEST(laea_roundtrip_south) {
    double test_points[][2] = {
        {0.0, -80.0}, {45.0, -70.0}, {-90.0, -60.0}, {180.0, -50.0}
    };
    int n = sizeof(test_points) / sizeof(test_points[0]);

    for (int i = 0; i < n; i++) {
        double lon_in = test_points[i][0];
        double lat_in = test_points[i][1];
        double x, y, lon_out, lat_out;

        laea_forward(lon_in, lat_in, -1, EARTH_RADIUS_M, &x, &y);
        int rc = laea_inverse(x, y, -1, EARTH_RADIUS_M, &lon_out, &lat_out);
        ASSERT_EQ_INT(rc, 0);
        ASSERT_NEAR(lat_out, lat_in, 0.001);

        double diff = fmod(lon_out - lon_in + 540.0, 360.0) - 180.0;
        ASSERT_NEAR(diff, 0.0, 0.001);
    }
    return 1;
}

/* Point outside projection disk should fail */
TEST(laea_inverse_outside_disk) {
    double lon, lat;
    /* rho = 3R > 2R, should return -1 */
    int rc = laea_inverse(3.0 * EARTH_RADIUS_M, 0.0, 1, EARTH_RADIUS_M, &lon, &lat);
    ASSERT_EQ_INT(rc, -1);
    return 1;
}

/* ========== laea_extent_from_cutoff ========== */

TEST(laea_extent_cutoff_60) {
    double extent = laea_extent_from_cutoff(60.0, 1, EARTH_RADIUS_M);
    ASSERT_GT(extent, 0.0);
    /* 60° cutoff means 30° from pole. Should be ~R*sqrt(2*(1-sin(60))) */
    /* = R*sqrt(2*(1-0.866)) = R*sqrt(0.268) ≈ R*0.518 */
    double expected = EARTH_RADIUS_M * sqrt(2.0 * (1.0 - sin(60.0 * DEG2RAD)));
    ASSERT_NEAR(extent, expected, 100.0);
    return 1;
}

TEST(laea_extent_cutoff_0) {
    double extent = laea_extent_from_cutoff(0.0, 1, EARTH_RADIUS_M);
    /* 0° cutoff = equator, 90° from pole -> R*sqrt(2) */
    ASSERT_NEAR(extent, EARTH_RADIUS_M * sqrt(2.0), 100.0);
    return 1;
}

/* Lower cutoff = wider view = larger extent */
TEST(laea_extent_monotonic) {
    double ext_70 = laea_extent_from_cutoff(70.0, 1, EARTH_RADIUS_M);
    double ext_60 = laea_extent_from_cutoff(60.0, 1, EARTH_RADIUS_M);
    double ext_50 = laea_extent_from_cutoff(50.0, 1, EARTH_RADIUS_M);
    ASSERT_LT(ext_70, ext_60);
    ASSERT_LT(ext_60, ext_50);
    return 1;
}

/* ========== Regional box via regrid_create ========== */

/* Helper: create global mesh */
static USMesh *create_global_mesh(size_t nx, size_t ny) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));
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

/* Box config restricts grid dimensions */
TEST(regrid_box_dims) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 35.0;
    cfg.lat_max = 70.0;

    /* 40 degrees lon / 10 deg resolution = 4, 35 degrees lat / 10 = 3 */
    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 4);
    ASSERT_EQ_SIZET(ny, 3);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Box lonlat returns values within box */
TEST(regrid_box_lonlat) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 35.0;
    cfg.lat_max = 70.0;

    USRegrid *regrid = regrid_create(mesh, 5.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            double lon, lat;
            regrid_get_lonlat(regrid, i, j, &lon, &lat);
            ASSERT_GE(lon, -10.0);
            ASSERT_LE(lon, 30.0);
            ASSERT_GE(lat, 35.0);
            ASSERT_LE(lat, 70.0);
        }
    }

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Box with data: only source points in box region should contribute */
TEST(regrid_box_apply) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.lon_min = -10.0;
    cfg.lon_max = 30.0;
    cfg.lat_min = 35.0;
    cfg.lat_max = 70.0;

    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    /* Uniform source data */
    float *source = malloc(mesh->n_points * sizeof(float));
    for (size_t i = 0; i < mesh->n_points; i++)
        source[i] = 7.0f;

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    float *target = malloc(nx * ny * sizeof(float));

    regrid_apply(regrid, source, 1e20f, target);

    /* All valid target points should have value 7 */
    int has_valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (target[i] < 1e10f) {
            ASSERT_NEAR(target[i], 7.0f, 0.01f);
            has_valid = 1;
        }
    }
    ASSERT_TRUE(has_valid);

    free(source);
    free(target);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* NULL config produces global grid (backward compat) */
TEST(regrid_null_config_global) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 500000.0, NULL);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 36);
    ASSERT_EQ_SIZET(ny, 18);

    /* First and last pixel lonlat should span global */
    double lon, lat;
    regrid_get_lonlat(regrid, 0, 0, &lon, &lat);
    ASSERT_NEAR(lon, -175.0, 0.1);
    ASSERT_NEAR(lat, -85.0, 0.1);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* ========== Polar LAEA via regrid_create ========== */

/* Polar grid should be square */
TEST(regrid_polar_square_grid) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, ny);  /* Square grid */
    ASSERT_GT(nx, 0);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Polar center pixel should be near the pole */
TEST(regrid_polar_center_is_pole) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USRegrid *regrid = regrid_create(mesh, 5.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    /* Center pixel should be close to north pole */
    double lon, lat;
    regrid_get_lonlat(regrid, nx / 2, ny / 2, &lon, &lat);
    ASSERT_GT(lat, 85.0);  /* Close to 90° */

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* South polar center should be near south pole */
TEST(regrid_polar_south_center) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_SOUTH;
    cfg.cutoff_lat = 60.0;

    USRegrid *regrid = regrid_create(mesh, 5.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    double lon, lat;
    regrid_get_lonlat(regrid, nx / 2, ny / 2, &lon, &lat);
    ASSERT_LT(lat, -85.0);  /* Close to -90° */

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Corners of polar grid correspond to lower latitude than center */
TEST(regrid_polar_corners_lower_lat) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USRegrid *regrid = regrid_create(mesh, 5.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    /* Center should be near pole */
    double clon, clat;
    regrid_get_lonlat(regrid, nx / 2, ny / 2, &clon, &clat);

    /* Corners should be at lower latitude than center */
    double lon, lat;
    regrid_get_lonlat(regrid, 0, 0, &lon, &lat);
    ASSERT_LT(lat, clat);

    regrid_get_lonlat(regrid, nx - 1, 0, &lon, &lat);
    ASSERT_LT(lat, clat);

    regrid_get_lonlat(regrid, 0, ny - 1, &lon, &lat);
    ASSERT_LT(lat, clat);

    regrid_get_lonlat(regrid, nx - 1, ny - 1, &lon, &lat);
    ASSERT_LT(lat, clat);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Smaller cutoff = wider view = larger grid */
TEST(regrid_polar_cutoff_size) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg1, cfg2;
    target_config_init_default(&cfg1);
    target_config_init_default(&cfg2);
    cfg1.projection = PROJ_LAEA_NORTH;
    cfg2.projection = PROJ_LAEA_NORTH;
    cfg1.cutoff_lat = 70.0;
    cfg2.cutoff_lat = 50.0;

    USRegrid *r1 = regrid_create(mesh, 5.0, 1600000.0, &cfg1);
    USRegrid *r2 = regrid_create(mesh, 5.0, 1600000.0, &cfg2);
    ASSERT_NOT_NULL(r1);
    ASSERT_NOT_NULL(r2);

    size_t nx1, ny1, nx2, ny2;
    regrid_get_target_dims(r1, &nx1, &ny1);
    regrid_get_target_dims(r2, &nx2, &ny2);

    /* 50° cutoff covers more area -> larger grid */
    ASSERT_GT(nx2, nx1);

    regrid_free(r1);
    regrid_free(r2);
    mesh_free(mesh);
    return 1;
}

/* Polar regrid with data produces valid center and invalid corners with small influence */
TEST(regrid_polar_apply_data) {
    USMesh *mesh = create_global_mesh(36, 18);
    ASSERT_NOT_NULL(mesh);

    USTargetConfig cfg;
    target_config_init_default(&cfg);
    cfg.projection = PROJ_LAEA_NORTH;
    cfg.cutoff_lat = 60.0;

    USRegrid *regrid = regrid_create(mesh, 5.0, 1600000.0, &cfg);
    ASSERT_NOT_NULL(regrid);

    float *source = malloc(mesh->n_points * sizeof(float));
    for (size_t i = 0; i < mesh->n_points; i++)
        source[i] = 99.0f;

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    float *target = malloc(nx * ny * sizeof(float));

    regrid_apply(regrid, source, 1e20f, target);

    /* Center region should have valid data */
    size_t mid = (ny / 2) * nx + (nx / 2);
    ASSERT_NEAR(target[mid], 99.0f, 0.01f);

    /* Should have some valid points */
    size_t valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (target[i] < 1e10f) valid++;
    }
    ASSERT_GT(valid, 0);

    free(source);
    free(target);
    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

RUN_TESTS("Projection")
