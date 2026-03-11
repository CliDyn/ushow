/*
 * test_regional_range.c - Tests for regional colorbar range computation
 *
 * Verifies that when --box or --polar region selection is used,
 * the data range is computed from the regional (regridded) data
 * rather than the full global dataset.
 */

#include "test_framework.h"
#include "test_utils.h"
#include "../src/us_types.h"
#include "../src/mesh.h"
#include "../src/regrid.h"
#include "../src/file_netcdf.h"
#include "../src/view.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helper: compute min/max from regridded data (mirrors view_update logic) */
static int compute_regional_range(const float *data, size_t n,
                                   float fill_value,
                                   float *out_min, float *out_max) {
    float rmin = 1e30f, rmax = -1e30f;
    for (size_t i = 0; i < n; i++) {
        float v = data[i];
        if (v != v) continue;  /* NaN */
        if (fabsf(v) > INVALID_DATA_THRESHOLD) continue;
        if (fabsf(v - fill_value) < 1e-6f * fabsf(fill_value)) continue;
        if (v < rmin) rmin = v;
        if (v > rmax) rmax = v;
    }
    if (rmin > rmax) return -1;  /* No valid data */
    *out_min = rmin;
    *out_max = rmax;
    return 0;
}

/* Helper: Create global mesh with latitude-dependent data.
 * Northern hemisphere: values in [100, 200]
 * Southern hemisphere: values in [0, 100]
 * This creates a clear distinction between regional and global ranges. */
static USMesh *create_gradient_mesh(size_t nx, size_t ny,
                                     float *source_data) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));

    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    double dlon = 360.0 / nx;
    double dlat = 180.0 / ny;

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + (i + 0.5) * dlon;
            lat[idx] = -90.0 + (j + 0.5) * dlat;

            /* Data: linearly maps latitude [-90,90] to [0, 200] */
            source_data[idx] = (float)((lat[idx] + 90.0) / 180.0 * 200.0);
        }
    }

    return mesh_create(lon, lat, n, COORD_TYPE_1D_STRUCTURED);
}

/* Test: Box selection produces a narrower range than global */
TEST(box_range_narrower_than_global) {
    size_t nx = 36, ny = 18;
    float *source_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(source_data);

    USMesh *mesh = create_gradient_mesh(nx, ny, source_data);
    ASSERT_NOT_NULL(mesh);

    /* Global regrid */
    USRegrid *regrid_global = regrid_create(mesh, 10.0, 1600000.0, NULL);
    ASSERT_NOT_NULL(regrid_global);

    /* Box regrid: select only northern hemisphere (lat 0 to 90) */
    USTargetConfig box_config = {
        .projection = PROJ_EQUIRECTANGULAR,
        .lon_min = -180.0, .lon_max = 180.0,
        .lat_min = 0.0, .lat_max = 90.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid_box = regrid_create(mesh, 10.0, 1600000.0, &box_config);
    ASSERT_NOT_NULL(regrid_box);

    /* Get dimensions */
    size_t gnx, gny, bnx, bny;
    regrid_get_target_dims(regrid_global, &gnx, &gny);
    regrid_get_target_dims(regrid_box, &bnx, &bny);

    /* Box grid should be smaller in latitude */
    ASSERT_EQ_SIZET(bnx, 36);  /* Same longitude extent */
    ASSERT_EQ_SIZET(bny, 9);   /* Half the latitude extent */

    /* Apply regrids */
    float *global_data = malloc(gnx * gny * sizeof(float));
    float *box_data = malloc(bnx * bny * sizeof(float));
    ASSERT_NOT_NULL(global_data);
    ASSERT_NOT_NULL(box_data);

    float fill = 1e20f;
    regrid_apply(regrid_global, source_data, fill, global_data);
    regrid_apply(regrid_box, source_data, fill, box_data);

    /* Compute ranges */
    float global_min, global_max, box_min, box_max;
    int g_ok = compute_regional_range(global_data, gnx * gny, fill,
                                       &global_min, &global_max);
    int b_ok = compute_regional_range(box_data, bnx * bny, fill,
                                       &box_min, &box_max);
    ASSERT_EQ(g_ok, 0);
    ASSERT_EQ(b_ok, 0);

    /* Global range should span ~[0, 200] */
    ASSERT_LT(global_min, 20.0f);
    ASSERT_GT(global_max, 180.0f);

    /* Box range (northern hemisphere only) should be ~[100, 200] */
    ASSERT_GT(box_min, 80.0f);   /* Should be around 100, definitely > 80 */
    ASSERT_GT(box_max, 180.0f);  /* Should reach close to 200 */

    /* Box min should be significantly higher than global min */
    ASSERT_GT(box_min, global_min + 50.0f);

    free(source_data);
    free(global_data);
    free(box_data);
    regrid_free(regrid_global);
    regrid_free(regrid_box);
    mesh_free(mesh);
    return 1;
}

/* Test: Small box produces range from only that region */
TEST(small_box_regional_range) {
    size_t nx = 36, ny = 18;
    float *source_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(source_data);

    USMesh *mesh = create_gradient_mesh(nx, ny, source_data);
    ASSERT_NOT_NULL(mesh);

    /* Small box: Europe region (lon -10 to 30, lat 35 to 70) */
    USTargetConfig box_config = {
        .projection = PROJ_EQUIRECTANGULAR,
        .lon_min = -10.0, .lon_max = 30.0,
        .lat_min = 35.0, .lat_max = 70.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid_box = regrid_create(mesh, 10.0, 1600000.0, &box_config);
    ASSERT_NOT_NULL(regrid_box);

    size_t bnx, bny;
    regrid_get_target_dims(regrid_box, &bnx, &bny);

    /* Should be a small grid */
    ASSERT_EQ_SIZET(bnx, 4);   /* (30 - (-10)) / 10 = 4 */

    float *box_data = malloc(bnx * bny * sizeof(float));
    ASSERT_NOT_NULL(box_data);

    float fill = 1e20f;
    regrid_apply(regrid_box, source_data, fill, box_data);

    float box_min, box_max;
    int ok = compute_regional_range(box_data, bnx * bny, fill,
                                     &box_min, &box_max);
    ASSERT_EQ(ok, 0);

    /* Europe range: lat 35-70 maps to values ~138-178 */
    /* (35+90)/180*200 = 138.9, (70+90)/180*200 = 177.8 */
    ASSERT_GT(box_min, 120.0f);
    ASSERT_LT(box_min, 160.0f);
    ASSERT_GT(box_max, 160.0f);
    ASSERT_LT(box_max, 200.0f);

    /* Range should NOT include values from equator or southern hemisphere */
    ASSERT_GT(box_min, 100.0f);

    free(source_data);
    free(box_data);
    regrid_free(regrid_box);
    mesh_free(mesh);
    return 1;
}

/* Test: Polar LAEA projection produces regional range */
TEST(polar_range_north) {
    size_t nx = 36, ny = 18;
    float *source_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(source_data);

    USMesh *mesh = create_gradient_mesh(nx, ny, source_data);
    ASSERT_NOT_NULL(mesh);

    /* North polar projection */
    USTargetConfig polar_config = {
        .projection = PROJ_LAEA_NORTH,
        .lon_min = -180.0, .lon_max = 180.0,
        .lat_min = -90.0, .lat_max = 90.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid_polar = regrid_create(mesh, 10.0, 1600000.0, &polar_config);
    ASSERT_NOT_NULL(regrid_polar);

    size_t pnx, pny;
    regrid_get_target_dims(regrid_polar, &pnx, &pny);
    ASSERT_GT(pnx, 0);
    ASSERT_GT(pny, 0);

    float *polar_data = malloc(pnx * pny * sizeof(float));
    ASSERT_NOT_NULL(polar_data);

    float fill = 1e20f;
    regrid_apply(regrid_polar, source_data, fill, polar_data);

    float polar_min, polar_max;
    int ok = compute_regional_range(polar_data, pnx * pny, fill,
                                     &polar_min, &polar_max);
    ASSERT_EQ(ok, 0);

    /* North polar (cutoff 60): only lat >= 60 shown
     * lat 60 maps to value (60+90)/180*200 = 166.7
     * lat 90 maps to value (90+90)/180*200 = 200.0
     * With nearest-neighbor and 10-degree mesh, allow some tolerance */
    ASSERT_GT(polar_min, 130.0f);  /* Should be well above equatorial values */
    ASSERT_GT(polar_max, 170.0f);  /* Should reach near 200 */

    /* Should NOT include southern hemisphere values (< 100) */
    ASSERT_GT(polar_min, 100.0f);

    free(source_data);
    free(polar_data);
    regrid_free(regrid_polar);
    mesh_free(mesh);
    return 1;
}

/* Test: South polar projection produces regional range */
TEST(polar_range_south) {
    size_t nx = 36, ny = 18;
    float *source_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(source_data);

    USMesh *mesh = create_gradient_mesh(nx, ny, source_data);
    ASSERT_NOT_NULL(mesh);

    /* South polar projection */
    USTargetConfig polar_config = {
        .projection = PROJ_LAEA_SOUTH,
        .lon_min = -180.0, .lon_max = 180.0,
        .lat_min = -90.0, .lat_max = 90.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid_polar = regrid_create(mesh, 10.0, 1600000.0, &polar_config);
    ASSERT_NOT_NULL(regrid_polar);

    size_t pnx, pny;
    regrid_get_target_dims(regrid_polar, &pnx, &pny);

    float *polar_data = malloc(pnx * pny * sizeof(float));
    ASSERT_NOT_NULL(polar_data);

    float fill = 1e20f;
    regrid_apply(regrid_polar, source_data, fill, polar_data);

    float polar_min, polar_max;
    int ok = compute_regional_range(polar_data, pnx * pny, fill,
                                     &polar_min, &polar_max);
    ASSERT_EQ(ok, 0);

    /* South polar (cutoff 60): only lat <= -60 shown
     * lat -90 maps to value (−90+90)/180*200 = 0
     * lat -60 maps to value (−60+90)/180*200 = 33.3 */
    ASSERT_LT(polar_max, 70.0f);  /* Should be well below northern values */
    ASSERT_LT(polar_min, 30.0f);  /* Should reach near 0 */

    /* Should NOT include northern hemisphere values (> 100) */
    ASSERT_LT(polar_max, 100.0f);

    free(source_data);
    free(polar_data);
    regrid_free(regrid_polar);
    mesh_free(mesh);
    return 1;
}

/* Test: Regional range skips fill values correctly */
TEST(regional_range_skips_fill) {
    size_t n = 100;
    float data[100];
    float fill = 1e20f;

    /* Mix of valid data and fill values */
    for (size_t i = 0; i < n; i++) {
        if (i % 3 == 0) {
            data[i] = fill;  /* Fill value */
        } else {
            data[i] = 10.0f + (float)i;
        }
    }

    float rmin, rmax;
    int ok = compute_regional_range(data, n, fill, &rmin, &rmax);
    ASSERT_EQ(ok, 0);

    /* Should skip fill values; min valid at i=1 (11), max valid at i=98 (108) */
    ASSERT_NEAR(rmin, 11.0f, 0.01f);
    ASSERT_NEAR(rmax, 108.0f, 0.01f);
    return 1;
}

/* Test: Regional range handles all-fill data */
TEST(regional_range_all_fill) {
    size_t n = 10;
    float data[10];
    float fill = 1e20f;

    for (size_t i = 0; i < n; i++) {
        data[i] = fill;
    }

    float rmin, rmax;
    int ok = compute_regional_range(data, n, fill, &rmin, &rmax);
    ASSERT_EQ(ok, -1);  /* No valid data */
    return 1;
}

/* Test: Regional range handles NaN values */
TEST(regional_range_skips_nan) {
    float data[5] = { 10.0f, NAN, 20.0f, NAN, 15.0f };

    float rmin, rmax;
    int ok = compute_regional_range(data, 5, 1e20f, &rmin, &rmax);
    ASSERT_EQ(ok, 0);
    ASSERT_NEAR(rmin, 10.0f, 0.01f);
    ASSERT_NEAR(rmax, 20.0f, 0.01f);
    return 1;
}

/* Test: Regional range handles values above INVALID_DATA_THRESHOLD */
TEST(regional_range_skips_invalid) {
    float data[5] = { 5.0f, 1e38f, 25.0f, -1e38f, 15.0f };

    float rmin, rmax;
    int ok = compute_regional_range(data, 5, 1e20f, &rmin, &rmax);
    ASSERT_EQ(ok, 0);
    ASSERT_NEAR(rmin, 5.0f, 0.01f);
    ASSERT_NEAR(rmax, 25.0f, 0.01f);
    return 1;
}

/* Test: needs_regional_range flag detection for box config */
TEST(flag_detection_box) {
    size_t nx = 36, ny = 18;
    double *lon = malloc(nx * ny * sizeof(double));
    double *lat = malloc(nx * ny * sizeof(double));

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + (i + 0.5) * 10.0;
            lat[idx] = -90.0 + (j + 0.5) * 10.0;
        }
    }

    USMesh *mesh = mesh_create(lon, lat, nx * ny, COORD_TYPE_1D_STRUCTURED);
    ASSERT_NOT_NULL(mesh);

    /* Box config: non-default bounds */
    USTargetConfig box_config = {
        .projection = PROJ_EQUIRECTANGULAR,
        .lon_min = -10.0, .lon_max = 30.0,
        .lat_min = 35.0, .lat_max = 70.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0, &box_config);
    ASSERT_NOT_NULL(regrid);

    /* Check that regrid has non-global bounds (this is what view_set_variable checks) */
    int is_regional = 0;
    if (regrid->projection != PROJ_EQUIRECTANGULAR) {
        is_regional = 1;
    } else if (regrid->target_lon_min > -180.0 || regrid->target_lon_max < 180.0 ||
               regrid->target_lat_min > -90.0  || regrid->target_lat_max < 90.0) {
        is_regional = 1;
    }
    ASSERT_TRUE(is_regional);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test: needs_regional_range flag detection for polar config */
TEST(flag_detection_polar) {
    size_t nx = 36, ny = 18;
    double *lon = malloc(nx * ny * sizeof(double));
    double *lat = malloc(nx * ny * sizeof(double));

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + (i + 0.5) * 10.0;
            lat[idx] = -90.0 + (j + 0.5) * 10.0;
        }
    }

    USMesh *mesh = mesh_create(lon, lat, nx * ny, COORD_TYPE_1D_STRUCTURED);
    ASSERT_NOT_NULL(mesh);

    /* Polar config */
    USTargetConfig polar_config = {
        .projection = PROJ_LAEA_NORTH,
        .lon_min = -180.0, .lon_max = 180.0,
        .lat_min = -90.0, .lat_max = 90.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0, &polar_config);
    ASSERT_NOT_NULL(regrid);

    /* Polar projection should be detected as regional */
    int is_regional = (regrid->projection != PROJ_EQUIRECTANGULAR);
    ASSERT_TRUE(is_regional);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test: Global config should NOT be detected as regional */
TEST(flag_detection_global) {
    size_t nx = 36, ny = 18;
    double *lon = malloc(nx * ny * sizeof(double));
    double *lat = malloc(nx * ny * sizeof(double));

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + (i + 0.5) * 10.0;
            lat[idx] = -90.0 + (j + 0.5) * 10.0;
        }
    }

    USMesh *mesh = mesh_create(lon, lat, nx * ny, COORD_TYPE_1D_STRUCTURED);
    ASSERT_NOT_NULL(mesh);

    /* Default global config (NULL = global defaults) */
    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0, NULL);
    ASSERT_NOT_NULL(regrid);

    /* Global should NOT be regional */
    int is_regional = 0;
    if (regrid->projection != PROJ_EQUIRECTANGULAR) {
        is_regional = 1;
    } else if (regrid->target_lon_min > -180.0 || regrid->target_lon_max < 180.0 ||
               regrid->target_lat_min > -90.0  || regrid->target_lat_max < 90.0) {
        is_regional = 1;
    }
    ASSERT_FALSE(is_regional);

    regrid_free(regrid);
    mesh_free(mesh);
    return 1;
}

/* Test: Full pipeline with NetCDF - box region range vs global range */
TEST(netcdf_box_vs_global_range) {
    /* Create test file: lat-dependent data, 3 time steps */
    const char *filename = create_test_netcdf_1d_structured(36, 18, 1);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    /* Global regrid */
    USRegrid *regrid_global = regrid_create(mesh, 10.0, 1600000.0, NULL);
    ASSERT_NOT_NULL(regrid_global);

    /* Box regrid: northern half only */
    USTargetConfig box_config = {
        .projection = PROJ_EQUIRECTANGULAR,
        .lon_min = -180.0, .lon_max = 180.0,
        .lat_min = 0.0, .lat_max = 90.0,
        .cutoff_lat = 60.0
    };
    USRegrid *regrid_box = regrid_create(mesh, 10.0, 1600000.0, &box_config);
    ASSERT_NOT_NULL(regrid_box);

    /* Scan variables */
    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Read data */
    float *raw_data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(raw_data);
    int status = netcdf_read_slice(vars, 0, 0, raw_data);
    ASSERT_EQ(status, 0);

    /* Apply global regrid */
    size_t gnx, gny;
    regrid_get_target_dims(regrid_global, &gnx, &gny);
    float *global_regridded = malloc(gnx * gny * sizeof(float));
    regrid_apply(regrid_global, raw_data, 1e20f, global_regridded);

    /* Apply box regrid */
    size_t bnx, bny;
    regrid_get_target_dims(regrid_box, &bnx, &bny);
    float *box_regridded = malloc(bnx * bny * sizeof(float));
    regrid_apply(regrid_box, raw_data, 1e20f, box_regridded);

    /* Compute ranges */
    float global_min, global_max, box_min, box_max;
    compute_regional_range(global_regridded, gnx * gny, 1e20f,
                            &global_min, &global_max);
    compute_regional_range(box_regridded, bnx * bny, 1e20f,
                            &box_min, &box_max);

    /* Box min should be higher (warmer, since northern latitudes have higher values
     * in test data: 273 + lat*0.5, so north = higher values) */
    ASSERT_GT(box_min, global_min);

    /* The two ranges should be detectably different */
    ASSERT_GT(box_min - global_min, 5.0f);

    free(raw_data);
    free(global_regridded);
    free(box_regridded);
    regrid_free(regrid_global);
    regrid_free(regrid_box);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

RUN_TESTS("RegionalRange")
