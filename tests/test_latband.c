/*
 * test_latband.c - Unit tests for latitude-band connectivity generation
 *
 * Tests the latband_generate_connectivity() function which creates triangular
 * element connectivity from point clouds organized in latitude bands
 * (HEALPix, reduced Gaussian, etc.).
 */

#include "test_framework.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include "../src/healpix.h"
#include <stdlib.h>

/* Helper: create mesh from arrays (takes ownership of lon/lat) */
static USMesh *make_mesh(double *lon, double *lat, size_t n) {
    return mesh_create(lon, lat, n, COORD_TYPE_1D_UNSTRUCTURED);
}

/* Helper: create a regular lat-lon grid as a flat point cloud */
static USMesh *create_latlon_grid(size_t nx, size_t ny) {
    size_t n = nx * ny;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));
    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    double dlon = 360.0 / nx;
    double dlat = 180.0 / (ny + 1);
    size_t idx = 0;
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            lon[idx] = -180.0 + (i + 0.5) * dlon;
            lat[idx] = 90.0 - (j + 1) * dlat;
            idx++;
        }
    }
    return make_mesh(lon, lat, n);
}

/* Helper: create a reduced-Gaussian-like grid with varying points per band */
static USMesh *create_reduced_grid(size_t n_bands, size_t max_pts) {
    /* Count total points: each band has min(4*(j+1), max_pts) points */
    size_t total = 0;
    for (size_t j = 0; j < n_bands; j++) {
        size_t np = 4 * (j + 1);
        if (np > max_pts) np = max_pts;
        total += np;
    }

    double *lon = malloc(total * sizeof(double));
    double *lat = malloc(total * sizeof(double));
    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    size_t idx = 0;
    double dlat = 180.0 / (n_bands + 1);
    for (size_t j = 0; j < n_bands; j++) {
        size_t np = 4 * (j + 1);
        if (np > max_pts) np = max_pts;
        double lat_val = 90.0 - (j + 1) * dlat;
        double dlon = 360.0 / np;
        for (size_t i = 0; i < np; i++) {
            lon[idx] = -180.0 + (i + 0.5) * dlon;
            lat[idx] = lat_val;
            idx++;
        }
    }
    return make_mesh(lon, lat, total);
}

/* Helper: create HEALPix-like grid in RING ordering */
static USMesh *create_healpix_grid(long nside) {
    long npix = 12 * nside * nside;
    double *lon = malloc(npix * sizeof(double));
    double *lat = malloc(npix * sizeof(double));
    if (!lon || !lat) { free(lon); free(lat); return NULL; }

    long n_rings = 4 * nside - 1;
    long idx = 0;
    for (long j = 1; j <= n_rings; j++) {
        long np;
        double shift;
        double z;

        if (j < nside) {
            np = 4 * j;
            shift = 0.5;
            z = 1.0 - (double)(j * j) / (3.0 * nside * nside);
        } else if (j <= 3 * nside) {
            np = 4 * nside;
            shift = ((j - nside) % 2 == 0) ? 0.5 : 0.0;
            z = 4.0 / 3.0 - 2.0 * j / (3.0 * nside);
        } else {
            long jj = 4 * nside - j;
            np = 4 * jj;
            shift = 0.5;
            z = -(1.0 - (double)(jj * jj) / (3.0 * nside * nside));
        }

        double lat_deg = asin(z) * 180.0 / M_PI;
        double dphi = 360.0 / np;
        for (long i = 0; i < np; i++) {
            lon[idx] = -180.0 + (i + shift) * dphi;
            if (lon[idx] > 180.0) lon[idx] -= 360.0;
            lat[idx] = lat_deg;
            idx++;
        }
    }
    return make_mesh(lon, lat, (size_t)npix);
}

/* Validate triangle indices are in range and non-degenerate */
static int validate_triangles(USMesh *mesh) {
    if (mesh->n_elements == 0 || mesh->elem_nodes == NULL) return 0;
    if (mesh->n_vertices != 3) return 0;

    for (size_t i = 0; i < mesh->n_elements; i++) {
        int a = mesh->elem_nodes[i * 3 + 0];
        int b = mesh->elem_nodes[i * 3 + 1];
        int c = mesh->elem_nodes[i * 3 + 2];
        /* All indices in range */
        if (a < 0 || (size_t)a >= mesh->n_points) return 0;
        if (b < 0 || (size_t)b >= mesh->n_points) return 0;
        if (c < 0 || (size_t)c >= mesh->n_points) return 0;
        /* Non-degenerate (no repeated vertices) */
        if (a == b || b == c || a == c) return 0;
    }
    return 1;
}

/* ---- Tests ---- */

TEST(latband_null_mesh) {
    int rc = latband_generate_connectivity(NULL);
    ASSERT_EQ_INT(rc, -1);
    return 1;
}

TEST(latband_too_few_points) {
    double *lon = malloc(2 * sizeof(double));
    double *lat = malloc(2 * sizeof(double));
    lon[0] = 0; lat[0] = 0;
    lon[1] = 1; lat[1] = 1;
    USMesh *mesh = make_mesh(lon, lat, 2);
    ASSERT_NOT_NULL(mesh);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, -1);  /* single band or too few points */
    mesh_free(mesh);
    return 1;
}

TEST(latband_regular_grid_small) {
    /* 4x3 regular grid = 12 points, 3 latitude bands */
    USMesh *mesh = create_latlon_grid(4, 3);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 12);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 3 bands of 4 points each: strip triangles = (4+4) + (4+4) = 16 */
    ASSERT_EQ_SIZET(mesh->n_elements, 16);
    ASSERT_EQ_INT(mesh->n_vertices, 3);
    ASSERT_NOT_NULL(mesh->elem_nodes);
    ASSERT_TRUE(validate_triangles(mesh));

    mesh_free(mesh);
    return 1;
}

TEST(latband_regular_grid_medium) {
    /* 36x18 grid = 648 points */
    USMesh *mesh = create_latlon_grid(36, 18);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 648);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 18 bands of 36 each: 17 strips * (36+36) = 1224 triangles */
    ASSERT_EQ_SIZET(mesh->n_elements, 1224);
    ASSERT_TRUE(validate_triangles(mesh));

    mesh_free(mesh);
    return 1;
}

TEST(latband_reduced_grid) {
    /* Reduced grid: 10 bands, max 20 points per band */
    USMesh *mesh = create_reduced_grid(10, 20);
    ASSERT_NOT_NULL(mesh);
    ASSERT_GT(mesh->n_points, 0);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* Should have triangles */
    ASSERT_GT(mesh->n_elements, 0);
    ASSERT_EQ_INT(mesh->n_vertices, 3);
    ASSERT_TRUE(validate_triangles(mesh));

    /* Triangle count = sum of (n_upper + n_lower) for adjacent pairs */
    /* Verify: total triangles = sum of points in all bands * 2 - first_band - last_band */
    /* This is roughly 2 * n_points for equal-size bands */
    ASSERT_GT(mesh->n_elements, mesh->n_points);

    mesh_free(mesh);
    return 1;
}

TEST(latband_healpix_nside1) {
    /* HEALPix Nside=1: 12 pixels, 3 rings (4+4+4) */
    USMesh *mesh = create_healpix_grid(1);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 12);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 3 rings: strip(4,4) + strip(4,4) = 16 triangles */
    ASSERT_EQ_SIZET(mesh->n_elements, 16);
    ASSERT_TRUE(validate_triangles(mesh));

    mesh_free(mesh);
    return 1;
}

TEST(latband_healpix_nside2) {
    /* HEALPix Nside=2: 48 pixels, 7 rings */
    USMesh *mesh = create_healpix_grid(2);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 48);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 7 rings: 6 strips, total triangles = sum of adjacent ring sizes */
    /* Rings: 4, 8, 8, 8, 8, 8, 4 */
    /* Strips: (4+8)+(8+8)+(8+8)+(8+8)+(8+8)+(8+4) = 12+16+16+16+16+12 = 88 */
    ASSERT_EQ_SIZET(mesh->n_elements, 88);
    ASSERT_TRUE(validate_triangles(mesh));

    mesh_free(mesh);
    return 1;
}

TEST(latband_healpix_nside4) {
    /* HEALPix Nside=4: 192 pixels, 15 rings */
    USMesh *mesh = create_healpix_grid(4);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ_SIZET(mesh->n_points, 192);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    ASSERT_GT(mesh->n_elements, 0);
    ASSERT_TRUE(validate_triangles(mesh));

    /* Approximately 2*npix triangles for a sphere triangulation */
    ASSERT_GT(mesh->n_elements, 300);
    ASSERT_LT(mesh->n_elements, 500);

    mesh_free(mesh);
    return 1;
}

TEST(latband_triangle_count_formula) {
    /* For equal-size bands: n_tri = (n_bands - 1) * 2 * pts_per_band */
    size_t nx = 10, ny = 5;
    USMesh *mesh = create_latlon_grid(nx, ny);
    ASSERT_NOT_NULL(mesh);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 5 bands of 10 pts: 4 strips * (10+10) = 80 */
    size_t expected = (ny - 1) * 2 * nx;
    ASSERT_EQ_SIZET(mesh->n_elements, expected);

    mesh_free(mesh);
    return 1;
}

TEST(latband_no_overwrite_existing_connectivity) {
    /* If mesh already has connectivity, latband should still work
     * (it overwrites - this tests the case where it gets called) */
    USMesh *mesh = create_latlon_grid(6, 3);
    ASSERT_NOT_NULL(mesh);

    /* Generate first time */
    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);
    size_t n1 = mesh->n_elements;
    ASSERT_GT(n1, 0);

    /* Free old connectivity and regenerate */
    free(mesh->elem_nodes);
    mesh->elem_nodes = NULL;
    mesh->n_elements = 0;
    mesh->n_vertices = 0;

    rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_SIZET(mesh->n_elements, n1);

    mesh_free(mesh);
    return 1;
}

TEST(latband_single_band_fails) {
    /* All points at same latitude - only 1 band */
    size_t n = 10;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));
    for (size_t i = 0; i < n; i++) {
        lon[i] = -180.0 + 36.0 * i;
        lat[i] = 45.0;  /* all same latitude */
    }
    USMesh *mesh = make_mesh(lon, lat, n);
    ASSERT_NOT_NULL(mesh);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, -1);  /* only 1 band, need >= 2 */

    mesh_free(mesh);
    return 1;
}

TEST(latband_two_bands_minimal) {
    /* Minimal case: 2 bands with 3 points each */
    double *lon = malloc(6 * sizeof(double));
    double *lat = malloc(6 * sizeof(double));
    /* Band 1 (north) */
    lon[0] = 0; lat[0] = 45;
    lon[1] = 120; lat[1] = 45;
    lon[2] = 240; lat[2] = 45;
    /* Band 2 (south) */
    lon[3] = 60; lat[3] = -45;
    lon[4] = 180; lat[4] = -45;
    lon[5] = 300; lat[5] = -45;

    USMesh *mesh = make_mesh(lon, lat, 6);
    ASSERT_NOT_NULL(mesh);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 2 bands of 3: 1 strip * (3+3) = 6 triangles */
    ASSERT_EQ_SIZET(mesh->n_elements, 6);
    ASSERT_TRUE(validate_triangles(mesh));

    mesh_free(mesh);
    return 1;
}

TEST(latband_asymmetric_bands) {
    /* One band with 3 points, another with 6 - tests unequal strip */
    double *lon = malloc(9 * sizeof(double));
    double *lat = malloc(9 * sizeof(double));
    /* Band 1: 3 points */
    lon[0] = 0; lat[0] = 60;
    lon[1] = 120; lat[1] = 60;
    lon[2] = 240; lat[2] = 60;
    /* Band 2: 6 points */
    lon[3] = 0; lat[3] = -30;
    lon[4] = 60; lat[4] = -30;
    lon[5] = 120; lat[5] = -30;
    lon[6] = 180; lat[6] = -30;
    lon[7] = 240; lat[7] = -30;
    lon[8] = 300; lat[8] = -30;

    USMesh *mesh = make_mesh(lon, lat, 9);
    ASSERT_NOT_NULL(mesh);

    int rc = latband_generate_connectivity(mesh);
    ASSERT_EQ_INT(rc, 0);

    /* 3 + 6 = 9 triangles in the strip */
    ASSERT_EQ_SIZET(mesh->n_elements, 9);
    ASSERT_TRUE(validate_triangles(mesh));

    mesh_free(mesh);
    return 1;
}

RUN_TESTS("Latitude Band Connectivity")
