/*
 * test_yac_curve2d.c - Tests for YAC curvilinear/structured grid support
 *
 * Requires: make WITH_YAC=1 test-yac-curve2d
 *
 * Verifies that curvilinear (2D) and structured (1D) grids use YAC's native
 * curve_2d grid API instead of auto-generated triangulation, which fails on
 * grids with folds or irregular latitude distributions (e.g., NEMO ORCA).
 */

#include "test_framework.h"
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include "../src/regrid_yac.h"
#include <stdlib.h>
#include <math.h>

/*
 * Create a curvilinear mesh (simulating NEMO-like 2D coordinates).
 * The grid has a slight longitude distortion that varies with latitude,
 * mimicking real curvilinear ocean grids.
 */
static USMesh *create_curvilinear_mesh(size_t nx, size_t ny) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));
    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            double base_lon = -180.0 + 360.0 * (double)i / (double)nx;
            double base_lat = -90.0 + 180.0 * (double)j / (double)ny;
            /* Add curvilinear distortion */
            lon[idx] = base_lon + 2.0 * sin(base_lat * M_PI / 180.0);
            lat[idx] = base_lat + 0.5 * sin(base_lon * M_PI / 180.0);
            /* Clamp to valid range */
            if (lon[idx] > 180.0) lon[idx] -= 360.0;
            if (lon[idx] < -180.0) lon[idx] += 360.0;
            if (lat[idx] > 90.0) lat[idx] = 90.0;
            if (lat[idx] < -90.0) lat[idx] = -90.0;
        }
    }

    USMesh *mesh = mesh_create(lon, lat, n, COORD_TYPE_2D_CURVILINEAR);
    if (!mesh) { free(lon); free(lat); return NULL; }
    mesh->orig_nx = nx;
    mesh->orig_ny = ny;
    return mesh;
}

/*
 * Create a 1D structured mesh (regular lon-lat grid stored as meshgrid).
 */
static USMesh *create_structured_mesh(size_t nx, size_t ny) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));
    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            lon[idx] = -180.0 + 360.0 * (double)i / (double)nx;
            lat[idx] = -90.0 + 180.0 * (double)j / (double)ny;
        }
    }

    USMesh *mesh = mesh_create(lon, lat, n, COORD_TYPE_1D_STRUCTURED);
    if (!mesh) { free(lon); free(lat); return NULL; }
    mesh->orig_nx = nx;
    mesh->orig_ny = ny;
    return mesh;
}

/* Fill source buffer with a latitude-dependent pattern */
static void fill_lat_pattern(float *src, const double *lat, size_t n) {
    for (size_t i = 0; i < n; i++)
        src[i] = 273.0f + (float)(lat[i] * 0.5);
}

/* ---- Tests ---- */

TEST(curve2d_create_avg_arith) {
    /* avg_arith requires connectivity — should use curve_2d, not latband */
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_elements, 0);  /* No pre-existing connectivity */

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);

    /* Mesh should still have no elem_nodes (curve_2d handles it internally) */
    ASSERT_EQ_SIZET(mesh->n_elements, 0);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(curve2d_create_avg_dist) {
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_DIST);
    ASSERT_NOT_NULL(r);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(curve2d_create_nnn1) {
    /* NNN methods don't need connectivity — should still work */
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_1);
    ASSERT_NOT_NULL(r);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(curve2d_apply_produces_valid_output) {
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    fill_lat_pattern(src, mesh->lat, mesh->n_points);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    /* Check that we got valid (non-fill) output values */
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

TEST(curve2d_output_values_reasonable) {
    /* Output values should be within the source data range */
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    fill_lat_pattern(src, mesh->lat, mesh->n_points);

    /* Find source min/max */
    float src_min = src[0], src_max = src[0];
    for (size_t i = 1; i < mesh->n_points; i++) {
        if (src[i] < src_min) src_min = src[i];
        if (src[i] > src_max) src_max = src[i];
    }

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    size_t n_tgt = nx * ny;
    float *dst = malloc(n_tgt * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    /* All non-fill values should be within source range (with tolerance) */
    for (size_t i = 0; i < n_tgt; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) {
            ASSERT_GE(dst[i], src_min - 1.0f);
            ASSERT_LE(dst[i], src_max + 1.0f);
        }
    }

    free(src);
    free(dst);
    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(curve2d_method_getter) {
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ_INT(yac_regrid_get_method(r), YAC_METHOD_AVERAGE_ARITH);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(structured_1d_create_avg_arith) {
    /* 1D structured grids should also use curve_2d path */
    USMesh *mesh = create_structured_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_elements, 0);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);

    /* No auto-generated connectivity */
    ASSERT_EQ_SIZET(mesh->n_elements, 0);

    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(structured_1d_apply_produces_valid_output) {
    USMesh *mesh = create_structured_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    fill_lat_pattern(src, mesh->lat, mesh->n_points);

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

TEST(curve2d_larger_grid) {
    /* Test with a larger grid to verify no edge/cell issues */
    USMesh *mesh = create_curvilinear_mesh(50, 40);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 5.0, YAC_METHOD_AVERAGE_ARITH);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    fill_lat_pattern(src, mesh->lat, mesh->n_points);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    float *dst = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    int n_valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
        if (fabsf(dst[i] - fill) > 1.0f) n_valid++;
    }
    ASSERT_GT(n_valid, 0);

    free(src);
    free(dst);
    yac_regrid_free(r);
    mesh_free(mesh);
    return 1;
}

TEST(curve2d_nnn4dist) {
    /* NNN-4 distance weighted with curvilinear grid */
    USMesh *mesh = create_curvilinear_mesh(20, 15);
    ASSERT_NOT_NULL(mesh);

    USYacRegrid *r = yac_regrid_create(mesh, 10.0, YAC_METHOD_NNN_4_DIST);
    ASSERT_NOT_NULL(r);

    float *src = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(src);
    fill_lat_pattern(src, mesh->lat, mesh->n_points);

    size_t nx, ny;
    yac_regrid_get_target_dims(r, &nx, &ny);
    float *dst = malloc(nx * ny * sizeof(float));
    ASSERT_NOT_NULL(dst);

    float fill = 1.0e20f;
    yac_regrid_apply(r, src, fill, dst);

    int n_valid = 0;
    for (size_t i = 0; i < nx * ny; i++) {
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

    int result = run_all_tests("YAC Curvilinear/Structured Grid Support");

    yac_regrid_finalize();
    return result;
}
