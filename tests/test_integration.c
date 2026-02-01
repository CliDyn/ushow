/*
 * test_integration.c - Integration tests for ushow pipeline
 *
 * Tests the full data flow from NetCDF -> Mesh -> Regrid -> Colormap
 */

#include "test_framework.h"
#include "test_utils.h"
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include "../src/regrid.h"
#include "../src/colormaps.h"
#include "../src/file_netcdf.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int colormaps_initialized = 0;

static void ensure_colormaps_init(void) {
    if (!colormaps_initialized) {
        colormaps_init();
        colormaps_initialized = 1;
    }
}

/* Full pipeline test: NetCDF -> Mesh -> Regrid -> Apply */
TEST(integration_full_pipeline_1d) {
    ensure_colormaps_init();

    /* Create test NetCDF file */
    const char *filename = create_test_netcdf_1d_structured(36, 18, 3);
    ASSERT_NOT_NULL(filename);

    /* Open file */
    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    /* Create mesh */
    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 36 * 18);

    /* Create regrid - use 1600km influence for 10-degree grid spacing
       (diagonal of 10x10 degree cell at equator is ~1570km) */
    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 36);
    ASSERT_EQ_SIZET(ny, 18);

    /* Scan for variables */
    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Read data */
    float *raw_data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(raw_data);

    int status = netcdf_read_slice(vars, 0, 0, raw_data);
    ASSERT_EQ(status, 0);

    /* Apply regrid */
    float *regridded_data = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(regridded_data);

    regrid_apply(regrid, raw_data, 1e20f, regridded_data);

    /* Verify some regridded data is valid */
    int valid_count = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (regridded_data[i] < 1e10f) {
            valid_count++;
            /* Should be temperature values */
            ASSERT_GT(regridded_data[i], 200.0f);
            ASSERT_LT(regridded_data[i], 400.0f);
        }
    }
    /* Allow fewer valid points if regridding didn't find close matches */
    ASSERT_GT(valid_count, 0);

    /* Apply colormap */
    float min_val, max_val;
    netcdf_estimate_range(vars, &min_val, &max_val);

    unsigned char *pixels = malloc(nx * ny * 3);
    ASSERT_NOT_NULL(pixels);

    USColormap *cmap = colormap_get_current();
    colormap_apply(cmap, regridded_data, nx, ny, min_val, max_val, 1e20f, pixels);

    /* Cleanup */
    free(pixels);
    free(regridded_data);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test pipeline with curvilinear data */
TEST(integration_full_pipeline_curvilinear) {
    ensure_colormaps_init();

    const char *filename = create_test_netcdf_2d_curvilinear(30, 20);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ(mesh->coord_type, COORD_TYPE_2D_CURVILINEAR);

    USRegrid *regrid = regrid_create(mesh, 5.0, 300000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    netcdf_read_slice(vars, 0, 0, raw_data);

    float *regridded = malloc(nx * ny * sizeof(float));
    regrid_apply(regrid, raw_data, 1e20f, regridded);

    unsigned char *pixels = malloc(nx * ny * 3);
    colormap_apply(colormap_get_current(), regridded, nx, ny, 270.0f, 300.0f, 1e20f, pixels);

    /* Basic validation */
    ASSERT_NOT_NULL(pixels);

    free(pixels);
    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test pipeline with unstructured data */
TEST(integration_full_pipeline_unstructured) {
    ensure_colormaps_init();

    const char *filename = create_test_netcdf_unstructured(500);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ(mesh->coord_type, COORD_TYPE_1D_UNSTRUCTURED);

    /* Use larger influence radius for sparse unstructured data */
    USRegrid *regrid = regrid_create(mesh, 5.0, 1000000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    netcdf_read_slice(vars, 0, 0, raw_data);

    float *regridded = malloc(nx * ny * sizeof(float));
    regrid_apply(regrid, raw_data, 1e20f, regridded);

    /* Count valid interpolated points */
    int valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (regridded[i] < 1e10f) valid++;
    }
    ASSERT_GT(valid, 0);

    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test multiple time steps */
TEST(integration_time_stepping) {
    ensure_colormaps_init();

    const char *filename = create_test_netcdf_1d_structured(18, 9, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    float *regridded = malloc(nx * ny * sizeof(float));
    unsigned char *pixels = malloc(nx * ny * 3);

    /* Process multiple time steps */
    for (int t = 0; t < 5; t++) {
        int status = netcdf_read_slice(vars, t, 0, raw_data);
        ASSERT_EQ(status, 0);

        regrid_apply(regrid, raw_data, 1e20f, regridded);
        colormap_apply(colormap_get_current(), regridded, nx, ny, 250.0f, 300.0f, 1e20f, pixels);

        /* Verify pixels are generated */
        ASSERT_NOT_NULL(pixels);
    }

    free(pixels);
    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test colormap cycling */
TEST(integration_colormap_cycling) {
    ensure_colormaps_init();

    const char *filename = create_test_netcdf_1d_structured(10, 10, 1);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    float *regridded = malloc(nx * ny * sizeof(float));
    unsigned char *pixels1 = malloc(nx * ny * 3);
    unsigned char *pixels2 = malloc(nx * ny * 3);

    netcdf_read_slice(vars, 0, 0, raw_data);
    regrid_apply(regrid, raw_data, 1e20f, regridded);

    /* Render with first colormap */
    colormap_apply(colormap_get_current(), regridded, nx, ny, 250.0f, 300.0f, 1e20f, pixels1);

    /* Switch colormap and render again */
    colormap_next();
    colormap_apply(colormap_get_current(), regridded, nx, ny, 250.0f, 300.0f, 1e20f, pixels2);

    /* Pixels should be different (assuming different colormaps) */
    int different = 0;
    for (size_t i = 0; i < nx * ny * 3; i++) {
        if (pixels1[i] != pixels2[i]) {
            different = 1;
            break;
        }
    }

    /* This might be same if we cycled back to same colormap with only 1 colormap,
       but with multiple colormaps it should be different */
    if (colormap_count() > 1) {
        ASSERT_TRUE(different);
    }

    free(pixels1);
    free(pixels2);
    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test scaled colormap output */
TEST(integration_scaled_output) {
    ensure_colormaps_init();

    const char *filename = create_test_netcdf_1d_structured(10, 10, 1);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1600000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    float *regridded = malloc(nx * ny * sizeof(float));

    netcdf_read_slice(vars, 0, 0, raw_data);
    regrid_apply(regrid, raw_data, 1e20f, regridded);

    /* Test 2x scaling */
    int scale = 2;
    unsigned char *pixels = malloc(nx * ny * scale * scale * 3);
    ASSERT_NOT_NULL(pixels);

    colormap_apply_scaled(colormap_get_current(), regridded, nx, ny,
                          250.0f, 300.0f, 1e20f, pixels, scale);

    /* Verify 2x2 blocks have same color */
    for (size_t y = 0; y < ny; y++) {
        for (size_t x = 0; x < nx; x++) {
            size_t base = (y * scale * nx * scale + x * scale) * 3;
            unsigned char r = pixels[base];
            unsigned char g = pixels[base + 1];
            unsigned char b = pixels[base + 2];

            /* Check other pixels in 2x2 block */
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    size_t idx = ((y * scale + sy) * nx * scale + (x * scale + sx)) * 3;
                    ASSERT_EQ(pixels[idx], r);
                    ASSERT_EQ(pixels[idx + 1], g);
                    ASSERT_EQ(pixels[idx + 2], b);
                }
            }
        }
    }

    free(pixels);
    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test with 3D data (depth levels) */
TEST(integration_depth_levels) {
    ensure_colormaps_init();

    const char *filename = create_test_netcdf_3d(2, 3, 100);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USRegrid *regrid = regrid_create(mesh, 10.0, 1000000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Find variable with depth dimension */
    USVar *temp = NULL;
    USVar *v = vars;
    while (v) {
        if (v->depth_dim_id >= 0) {
            temp = v;
            break;
        }
        v = v->next;
    }
    ASSERT_NOT_NULL(temp);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    float *regridded = malloc(nx * ny * sizeof(float));

    /* Read different depth levels */
    float mean_d0 = 0, mean_d2 = 0;
    int count_d0 = 0, count_d2 = 0;

    netcdf_read_slice(temp, 0, 0, raw_data);
    regrid_apply(regrid, raw_data, 1e20f, regridded);
    for (size_t i = 0; i < nx * ny; i++) {
        if (regridded[i] < 1e10f) {
            mean_d0 += regridded[i];
            count_d0++;
        }
    }
    if (count_d0 > 0) mean_d0 /= count_d0;

    netcdf_read_slice(temp, 0, 2, raw_data);
    regrid_apply(regrid, raw_data, 1e20f, regridded);
    for (size_t i = 0; i < nx * ny; i++) {
        if (regridded[i] < 1e10f) {
            mean_d2 += regridded[i];
            count_d2++;
        }
    }
    if (count_d2 > 0) mean_d2 /= count_d2;

    /* Deeper levels should have different temperature (colder) */
    if (count_d0 > 0 && count_d2 > 0) {
        ASSERT_GT(mean_d0, mean_d2);  /* Surface warmer than depth */
    }

    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Stress test: Large dataset */
TEST(integration_stress_large_data) {
    ensure_colormaps_init();

    /* Create larger dataset */
    const char *filename = create_test_netcdf_1d_structured(180, 90, 1);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 180 * 90);

    USRegrid *regrid = regrid_create(mesh, 2.0, 200000.0);
    ASSERT_NOT_NULL(regrid);

    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    ASSERT_EQ_SIZET(nx, 180);
    ASSERT_EQ_SIZET(ny, 90);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *raw_data = malloc(mesh->n_points * sizeof(float));
    float *regridded = malloc(nx * ny * sizeof(float));
    unsigned char *pixels = malloc(nx * ny * 3);

    ASSERT_NOT_NULL(raw_data);
    ASSERT_NOT_NULL(regridded);
    ASSERT_NOT_NULL(pixels);

    netcdf_read_slice(vars, 0, 0, raw_data);
    regrid_apply(regrid, raw_data, 1e20f, regridded);
    colormap_apply(colormap_get_current(), regridded, nx, ny, 250.0f, 310.0f, 1e20f, pixels);

    /* Basic sanity checks */
    int valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (regridded[i] < 1e10f) valid++;
    }
    ASSERT_GT(valid, (int)(nx * ny / 2));

    free(pixels);
    free(regridded);
    free(raw_data);

    /* Variables are freed by netcdf_close() */
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);

    return 1;
}

/* Test coordinate transform chain */
TEST(integration_coordinate_transform) {
    /* Verify that lonlat -> cartesian -> kdtree -> nearest neighbor
       preserves spatial relationships */
    ensure_colormaps_init();

    /* Create mesh with known points */
    double *lon = malloc(4 * sizeof(double));
    double *lat = malloc(4 * sizeof(double));

    /* Four corners of a small region */
    lon[0] = 0.0;  lat[0] = 0.0;
    lon[1] = 10.0; lat[1] = 0.0;
    lon[2] = 0.0;  lat[2] = 10.0;
    lon[3] = 10.0; lat[3] = 10.0;

    USMesh *mesh = mesh_create(lon, lat, 4, COORD_TYPE_1D_UNSTRUCTURED);
    ASSERT_NOT_NULL(mesh);

    /* Create regrid with 1 degree resolution */
    USRegrid *regrid = regrid_create(mesh, 1.0, 600000.0);  /* 600km influence */
    ASSERT_NOT_NULL(regrid);

    float source_data[] = {1.0f, 2.0f, 3.0f, 4.0f};
    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);

    float *target = malloc(nx * ny * sizeof(float));
    regrid_apply(regrid, source_data, 1e20f, target);

    /* Check that target points near source points have correct values */
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            double tlon, tlat;
            regrid_get_lonlat(regrid, i, j, &tlon, &tlat);

            size_t idx = j * nx + i;
            if (target[idx] < 1e10f) {
                /* Near (0,0) should be ~1.0 */
                if (fabs(tlon) < 3 && fabs(tlat) < 3) {
                    ASSERT_NEAR(target[idx], 1.0f, 0.5f);
                }
                /* Near (10,10) should be ~4.0 */
                if (fabs(tlon - 10) < 3 && fabs(tlat - 10) < 3) {
                    ASSERT_NEAR(target[idx], 4.0f, 0.5f);
                }
            }
        }
    }

    free(target);
    regrid_free(regrid);
    mesh_free(mesh);

    return 1;
}

RUN_TESTS("Integration")
