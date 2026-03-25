/*
 * test_file_mitgcm.c - Unit tests for MITgcm MDS binary data reading
 *
 * Creates synthetic MITgcm .data/.meta file pairs in a temp directory,
 * then exercises the file_mitgcm API.
 */

#include "test_framework.h"
#include "../src/us_types.h"
#include "../src/file_mitgcm.h"
#include "../src/mesh.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <math.h>

/* ---- Helpers to create synthetic MITgcm data ---- */

static char test_dir[256];

static void swap_to_big_endian(float *data, size_t n) {
    /* If the machine is little-endian, swap bytes to produce big-endian output */
    uint32_t check = 1;
    if (*(unsigned char *)&check == 0) return;  /* already big-endian */
    uint32_t *p = (uint32_t *)data;
    for (size_t i = 0; i < n; i++) {
        uint32_t v = p[i];
        p[i] = ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
               ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    }
}

static int write_binary(const char *path, float *data, size_t n) {
    float *buf = malloc(n * sizeof(float));
    if (!buf) return -1;
    memcpy(buf, data, n * sizeof(float));
    swap_to_big_endian(buf, n);
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(buf); return -1; }
    size_t written = fwrite(buf, sizeof(float), n, fp);
    fclose(fp);
    free(buf);
    return (written == n) ? 0 : -1;
}

static int write_meta(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fputs(content, fp);
    fclose(fp);
    return 0;
}

/* Small grid: 4x3 with 2 depth levels */
#define TEST_NX 4
#define TEST_NY 3
#define TEST_NZ 2
#define TEST_SLAB (TEST_NX * TEST_NY)

static void setup_test_dir(void) {
    snprintf(test_dir, sizeof(test_dir), "/tmp/test_mitgcm_%d", getpid());
    mkdir(test_dir, 0755);
}

static void cleanup_test_dir(void) {
    /* Remove all files in the test directory */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir);
    (void)system(cmd);
}

/* Create XC.meta + XC.data */
static int create_grid_xy(void) {
    char path[512];

    /* XC.meta */
    snprintf(path, sizeof(path), "%s/XC.meta", test_dir);
    write_meta(path,
        " nDims = [   2 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          1 ];\n");

    /* XC.data: lon values */
    float xc[TEST_SLAB];
    for (int j = 0; j < TEST_NY; j++)
        for (int i = 0; i < TEST_NX; i++)
            xc[j * TEST_NX + i] = -180.0f + 90.0f * i;  /* -180, -90, 0, 90 */

    snprintf(path, sizeof(path), "%s/XC.data", test_dir);
    if (write_binary(path, xc, TEST_SLAB) != 0) return -1;

    /* YC.meta */
    snprintf(path, sizeof(path), "%s/YC.meta", test_dir);
    write_meta(path,
        " nDims = [   2 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          1 ];\n");

    /* YC.data: lat values */
    float yc[TEST_SLAB];
    for (int j = 0; j < TEST_NY; j++)
        for (int i = 0; i < TEST_NX; i++)
            yc[j * TEST_NX + i] = -60.0f + 60.0f * j;  /* -60, 0, 60 */

    snprintf(path, sizeof(path), "%s/YC.data", test_dir);
    return write_binary(path, yc, TEST_SLAB);
}

/* Create RC.meta + RC.data (depth levels) */
static int create_rc(void) {
    char path[512];

    snprintf(path, sizeof(path), "%s/RC.meta", test_dir);
    write_meta(path,
        " nDims = [   3 ];\n"
        " dimList = [\n"
        "     1,    1,    1,\n"
        "     1,    1,    1,\n"
        "     2,    1,    2\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          1 ];\n");

    float rc[TEST_NZ] = {-5.0f, -15.0f};
    snprintf(path, sizeof(path), "%s/RC.data", test_dir);
    return write_binary(path, rc, TEST_NZ);
}

/* Create hFacC.meta + hFacC.data (land mask) */
static int create_hfac(void) {
    char path[512];

    snprintf(path, sizeof(path), "%s/hFacC.meta", test_dir);
    write_meta(path,
        " nDims = [   3 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3,\n"
        "    2,    1,    2\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          1 ];\n"
        " timeStepNumber = [          0 ];\n");

    /* hFacC: all ocean except point [0,0] at both depths */
    size_t total = TEST_NZ * TEST_SLAB;
    float *hfac = calloc(total, sizeof(float));
    if (!hfac) return -1;
    for (size_t i = 0; i < total; i++) hfac[i] = 1.0f;
    hfac[0] = 0.0f;                /* depth 0, point (0,0) = land */
    hfac[TEST_SLAB + 0] = 0.0f;   /* depth 1, point (0,0) = land */

    snprintf(path, sizeof(path), "%s/hFacC.data", test_dir);
    int rc = write_binary(path, hfac, total);
    free(hfac);
    return rc;
}

/* Create a 2D diagnostic file with 2 fields at a given iteration */
static int create_diag2d(int iteration) {
    char path[512];
    char meta_buf[1024];

    snprintf(path, sizeof(path), "%s/diags2D.%010d.meta", test_dir, iteration);
    snprintf(meta_buf, sizeof(meta_buf),
        " nDims = [   2 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          2 ];\n"
        " timeStepNumber = [ %d ];\n"
        " missingValue = [ -9.99000000000000E+02 ];\n"
        " nFlds = [    2 ];\n"
        " fldList = {\n"
        " 'ETAN    ' 'MXLDEPTH'\n"
        " };\n", iteration);
    write_meta(path, meta_buf);

    /* Data: 2 fields * ny * nx */
    size_t total = 2 * TEST_SLAB;
    float *data = malloc(total * sizeof(float));
    if (!data) return -1;

    /* ETAN: simple ramp */
    for (int i = 0; i < TEST_SLAB; i++)
        data[i] = 0.5f * (float)i + (float)iteration * 0.01f;

    /* MXLDEPTH: constant */
    for (int i = 0; i < TEST_SLAB; i++)
        data[TEST_SLAB + i] = 50.0f + (float)iteration * 0.1f;

    snprintf(path, sizeof(path), "%s/diags2D.%010d.data", test_dir, iteration);
    int rc = write_binary(path, data, total);
    free(data);
    return rc;
}

/* Create a 3D diagnostic file with 1 field at a given iteration */
static int create_diag3d(int iteration) {
    char path[512];
    char meta_buf[1024];

    snprintf(path, sizeof(path), "%s/diags3D.%010d.meta", test_dir, iteration);
    snprintf(meta_buf, sizeof(meta_buf),
        " nDims = [   3 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3,\n"
        "    2,    1,    2\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          1 ];\n"
        " timeStepNumber = [ %d ];\n"
        " missingValue = [ -9.99000000000000E+02 ];\n"
        " nFlds = [    1 ];\n"
        " fldList = {\n"
        " 'THETA   '\n"
        " };\n", iteration);
    write_meta(path, meta_buf);

    /* Data: nz * ny * nx */
    size_t total = TEST_NZ * TEST_SLAB;
    float *data = malloc(total * sizeof(float));
    if (!data) return -1;

    for (int z = 0; z < TEST_NZ; z++)
        for (int i = 0; i < TEST_SLAB; i++)
            data[z * TEST_SLAB + i] = 20.0f - (float)z * 5.0f + (float)i * 0.1f;

    snprintf(path, sizeof(path), "%s/diags3D.%010d.data", test_dir, iteration);
    int rc = write_binary(path, data, total);
    free(data);
    return rc;
}

/* Create a full test dataset */
static int create_full_dataset(void) {
    setup_test_dir();
    if (create_grid_xy() != 0) return -1;
    if (create_rc() != 0) return -1;
    if (create_hfac() != 0) return -1;
    if (create_diag2d(100) != 0) return -1;
    if (create_diag2d(200) != 0) return -1;
    if (create_diag3d(100) != 0) return -1;
    if (create_diag3d(200) != 0) return -1;
    return 0;
}

/* ---- Tests ---- */

/* Detection */

TEST(mitgcm_is_mitgcm_null) {
    ASSERT_FALSE(mitgcm_is_mitgcm(NULL));
    return 1;
}

TEST(mitgcm_is_mitgcm_nonexistent) {
    ASSERT_FALSE(mitgcm_is_mitgcm("/nonexistent/path"));
    return 1;
}

TEST(mitgcm_is_mitgcm_directory) {
    ASSERT_EQ(create_full_dataset(), 0);
    ASSERT_TRUE(mitgcm_is_mitgcm(test_dir));
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_is_mitgcm_data_file) {
    ASSERT_EQ(create_full_dataset(), 0);
    char path[512];
    snprintf(path, sizeof(path), "%s/diags2D.0000000100.data", test_dir);
    ASSERT_TRUE(mitgcm_is_mitgcm(path));
    cleanup_test_dir();
    return 1;
}

/* Open */

TEST(mitgcm_open_null) {
    USFile *f = mitgcm_open(NULL);
    ASSERT_NULL(f);
    return 1;
}

TEST(mitgcm_open_directory) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->file_type, FILE_TYPE_MITGCM);
    ASSERT_NOT_NULL(f->mitgcm_data);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_open_from_data_file) {
    ASSERT_EQ(create_full_dataset(), 0);
    char path[512];
    snprintf(path, sizeof(path), "%s/diags3D.0000000100.data", test_dir);
    USFile *f = mitgcm_open(path);
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->file_type, FILE_TYPE_MITGCM);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Mesh */

TEST(mitgcm_create_mesh_basic) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);

    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ_SIZET(m->n_points, TEST_SLAB);
    ASSERT_EQ(m->coord_type, COORD_TYPE_2D_CURVILINEAR);
    ASSERT_EQ_SIZET(m->orig_nx, TEST_NX);
    ASSERT_EQ_SIZET(m->orig_ny, TEST_NY);

    /* Check some coordinate values */
    ASSERT_NEAR(m->lon[0], -180.0, 0.1);   /* First point lon */
    ASSERT_NEAR(m->lat[0], -60.0, 0.1);    /* First point lat */

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Variable scanning */

TEST(mitgcm_scan_variables_count) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);

    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Should find: ETAN, MXLDEPTH (2D), THETA (3D) = 3 variables */
    int count = 0;
    for (USVar *v = vars; v; v = v->next) count++;
    ASSERT_EQ_INT(count, 3);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_scan_variables_2d_dims) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);

    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Find ETAN */
    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    /* 2D var: time + node */
    ASSERT_TRUE(etan->time_dim_id >= 0);
    ASSERT_EQ_INT(etan->depth_dim_id, -1);
    ASSERT_EQ_SIZET(etan->dim_sizes[etan->time_dim_id], 2);  /* 2 iterations */
    ASSERT_EQ_SIZET(etan->dim_sizes[etan->node_dim_id], TEST_SLAB);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_scan_variables_3d_dims) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);

    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Find THETA */
    USVar *theta = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "THETA") == 0) { theta = v; break; }
    }
    ASSERT_NOT_NULL(theta);

    /* 3D var: time + depth + node */
    ASSERT_TRUE(theta->time_dim_id >= 0);
    ASSERT_TRUE(theta->depth_dim_id >= 0);
    ASSERT_EQ_SIZET(theta->dim_sizes[theta->time_dim_id], 2);
    ASSERT_EQ_SIZET(theta->dim_sizes[theta->depth_dim_id], TEST_NZ);
    ASSERT_EQ_SIZET(theta->dim_sizes[theta->node_dim_id], TEST_SLAB);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Read slice */

TEST(mitgcm_read_slice_2d) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Find ETAN */
    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(etan, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);

    /* Point 0 is land (hFacC=0), should be fill value */
    ASSERT_NEAR(data[0], etan->fill_value, 0.1);

    /* Point 1 is ocean, should have real data */
    /* Expected: 0.5 * 1 + 100 * 0.01 = 1.5 */
    ASSERT_NEAR(data[1], 1.5f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_read_slice_3d_surface) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Find THETA */
    USVar *theta = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "THETA") == 0) { theta = v; break; }
    }
    ASSERT_NOT_NULL(theta);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(theta, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);

    /* Point 0 (land) should be fill */
    ASSERT_NEAR(data[0], theta->fill_value, 0.1);

    /* Point 1 (ocean, depth=0): 20.0 - 0*5.0 + 1*0.1 = 20.1 */
    ASSERT_NEAR(data[1], 20.1f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_read_slice_3d_deep) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *theta = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "THETA") == 0) { theta = v; break; }
    }
    ASSERT_NOT_NULL(theta);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(theta, 0, 1, data);
    ASSERT_EQ_INT(rc, 0);

    /* Point 1 (ocean, depth=1): 20.0 - 1*5.0 + 1*0.1 = 15.1 */
    ASSERT_NEAR(data[1], 15.1f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_read_slice_second_timestep) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(etan, 1, 0, data);
    ASSERT_EQ_INT(rc, 0);

    /* Point 1 (ocean, iteration=200): 0.5 * 1 + 200 * 0.01 = 2.5 */
    ASSERT_NEAR(data[1], 2.5f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Range estimation */

TEST(mitgcm_estimate_range_basic) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    float min_val, max_val;
    int rc = mitgcm_estimate_range(etan, &min_val, &max_val);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_LT(min_val, max_val);
    ASSERT_GT(min_val, -100.0f);  /* Should not be fill value */

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Dimension info */

TEST(mitgcm_get_dim_info_2d) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    int n_dims = 0;
    USDimInfo *dims = mitgcm_get_dim_info(etan, &n_dims);
    ASSERT_NOT_NULL(dims);
    ASSERT_EQ_INT(n_dims, 1);  /* time only, no depth */
    ASSERT_STR_EQ(dims[0].name, "time");
    ASSERT_EQ_SIZET(dims[0].size, 2);
    ASSERT_NOT_NULL(dims[0].values);
    ASSERT_NEAR(dims[0].values[0], 100.0, 0.01);
    ASSERT_NEAR(dims[0].values[1], 200.0, 0.01);

    mitgcm_free_dim_info(dims, n_dims);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_get_dim_info_3d) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *theta = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "THETA") == 0) { theta = v; break; }
    }
    ASSERT_NOT_NULL(theta);

    int n_dims = 0;
    USDimInfo *dims = mitgcm_get_dim_info(theta, &n_dims);
    ASSERT_NOT_NULL(dims);
    ASSERT_EQ_INT(n_dims, 2);  /* time + depth */
    ASSERT_STR_EQ(dims[0].name, "time");
    ASSERT_STR_EQ(dims[1].name, "depth");
    ASSERT_EQ_SIZET(dims[1].size, TEST_NZ);
    ASSERT_NOT_NULL(dims[1].values);
    ASSERT_NEAR(dims[1].values[0], -5.0, 0.01);
    ASSERT_NEAR(dims[1].values[1], -15.0, 0.01);

    mitgcm_free_dim_info(dims, n_dims);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Time series */

TEST(mitgcm_read_timeseries_basic) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n_out = 0;

    /* Read timeseries at ocean point (index 1) */
    int rc = mitgcm_read_timeseries(etan, 1, 0, &times, &values, &valid, &n_out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_SIZET(n_out, 2);
    ASSERT_NOT_NULL(times);
    ASSERT_NOT_NULL(values);
    ASSERT_NOT_NULL(valid);

    ASSERT_NEAR(times[0], 100.0, 0.01);
    ASSERT_NEAR(times[1], 200.0, 0.01);
    ASSERT_EQ_INT(valid[0], 1);
    ASSERT_EQ_INT(valid[1], 1);

    /* Values: iteration 100 -> 0.5*1 + 100*0.01 = 1.5, iteration 200 -> 0.5*1 + 200*0.01 = 2.5 */
    ASSERT_NEAR(values[0], 1.5f, 0.01);
    ASSERT_NEAR(values[1], 2.5f, 0.01);

    free(times); free(values); free(valid);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_read_timeseries_land_point) {
    ASSERT_EQ(create_full_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n_out = 0;

    /* Read timeseries at land point (index 0) */
    int rc = mitgcm_read_timeseries(etan, 0, 0, &times, &values, &valid, &n_out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_SIZET(n_out, 2);
    ASSERT_EQ_INT(valid[0], 0);  /* Land: not valid */
    ASSERT_EQ_INT(valid[1], 0);

    free(times); free(values); free(valid);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* ---- Velocity rotation helpers ---- */

/* Create AngleCS.data + AngleSN.data with constant angle */
static int create_angle_cs_sn(float cs_val, float sn_val) {
    char path[512];
    float cs[TEST_SLAB], sn[TEST_SLAB];

    for (int i = 0; i < TEST_SLAB; i++) {
        cs[i] = cs_val;
        sn[i] = sn_val;
    }

    snprintf(path, sizeof(path), "%s/AngleCS.data", test_dir);
    if (write_binary(path, cs, TEST_SLAB) != 0) return -1;

    snprintf(path, sizeof(path), "%s/AngleSN.data", test_dir);
    return write_binary(path, sn, TEST_SLAB);
}

/* Create a 3D velocity diagnostic with UVEL and VVEL fields */
static int create_vel_diag(int iteration, float uval, float vval) {
    char path[512];
    char meta_buf[1024];

    snprintf(path, sizeof(path), "%s/velDiag.%010d.meta", test_dir, iteration);
    snprintf(meta_buf, sizeof(meta_buf),
        " nDims = [   3 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3,\n"
        "    2,    1,    2\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          2 ];\n"
        " timeStepNumber = [ %d ];\n"
        " nFlds = [    2 ];\n"
        " fldList = {\n"
        " 'UVEL    ' 'VVEL    '\n"
        " };\n", iteration);
    write_meta(path, meta_buf);

    /* Data: 2 fields * nz * ny * nx */
    size_t field_size = TEST_NZ * TEST_SLAB;
    size_t total = 2 * field_size;
    float *data = malloc(total * sizeof(float));
    if (!data) return -1;

    /* UVEL: constant value everywhere */
    for (size_t i = 0; i < field_size; i++)
        data[i] = uval;

    /* VVEL: constant value everywhere */
    for (size_t i = 0; i < field_size; i++)
        data[field_size + i] = vval;

    snprintf(path, sizeof(path), "%s/velDiag.%010d.data", test_dir, iteration);
    int rc = write_binary(path, data, total);
    free(data);
    return rc;
}

/* Create dataset with angle files and velocity diagnostics */
static int create_rotation_dataset(void) {
    setup_test_dir();
    if (create_grid_xy() != 0) return -1;
    if (create_rc() != 0) return -1;
    if (create_hfac() != 0) return -1;
    /* CS=0.6, SN=0.8 (3-4-5 triangle, CS^2+SN^2=1) */
    if (create_angle_cs_sn(0.6f, 0.8f) != 0) return -1;
    /* UVEL=3.0, VVEL=4.0 */
    if (create_vel_diag(100, 3.0f, 4.0f) != 0) return -1;
    if (create_vel_diag(200, 6.0f, 8.0f) != 0) return -1;
    return 0;
}

/* Velocity rotation tests */

TEST(mitgcm_velocity_pairing) {
    ASSERT_EQ(create_rotation_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Find UVEL and VVEL */
    USVar *uvel = NULL, *vvel = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "UVEL") == 0) uvel = v;
        if (strcmp(v->name, "VVEL") == 0) vvel = v;
    }
    ASSERT_NOT_NULL(uvel);
    ASSERT_NOT_NULL(vvel);

    /* Check they are paired */
    ASSERT_NOT_NULL(uvel->mitgcm_data);
    ASSERT_NOT_NULL(vvel->mitgcm_data);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_velocity_rotation_slice_u) {
    ASSERT_EQ(create_rotation_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *uvel = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "UVEL") == 0) { uvel = v; break; }
    }
    ASSERT_NOT_NULL(uvel);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(uvel, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);

    /* Point 0 is land → fill value */
    ASSERT_NEAR(data[0], uvel->fill_value, 0.1);

    /* Ocean points: u_east = CS*U - SN*V = 0.6*3.0 - 0.8*4.0 = 1.8 - 3.2 = -1.4 */
    ASSERT_NEAR(data[1], -1.4f, 0.01);
    ASSERT_NEAR(data[2], -1.4f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_velocity_rotation_slice_v) {
    ASSERT_EQ(create_rotation_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *vvel = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "VVEL") == 0) { vvel = v; break; }
    }
    ASSERT_NOT_NULL(vvel);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(vvel, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);

    /* Ocean points: v_north = SN*U + CS*V = 0.8*3.0 + 0.6*4.0 = 2.4 + 2.4 = 4.8 */
    ASSERT_NEAR(data[1], 4.8f, 0.01);
    ASSERT_NEAR(data[2], 4.8f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_velocity_rotation_timeseries) {
    ASSERT_EQ(create_rotation_dataset(), 0);
    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *uvel = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "UVEL") == 0) { uvel = v; break; }
    }
    ASSERT_NOT_NULL(uvel);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n_out = 0;

    /* Read timeseries at ocean point (index 1) */
    int rc = mitgcm_read_timeseries(uvel, 1, 0, &times, &values, &valid, &n_out);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_SIZET(n_out, 2);
    ASSERT_EQ_INT(valid[0], 1);
    ASSERT_EQ_INT(valid[1], 1);

    /* t=100: u_east = 0.6*3.0 - 0.8*4.0 = -1.4 */
    ASSERT_NEAR(values[0], -1.4f, 0.01);
    /* t=200: u_east = 0.6*6.0 - 0.8*8.0 = 3.6 - 6.4 = -2.8 */
    ASSERT_NEAR(values[1], -2.8f, 0.01);

    free(times); free(values); free(valid);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_no_rotation_without_angles) {
    /* Create dataset WITHOUT angle files */
    setup_test_dir();
    ASSERT_EQ(create_grid_xy(), 0);
    ASSERT_EQ(create_rc(), 0);
    ASSERT_EQ(create_hfac(), 0);
    ASSERT_EQ(create_vel_diag(100, 3.0f, 4.0f), 0);

    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *uvel = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "UVEL") == 0) { uvel = v; break; }
    }
    ASSERT_NOT_NULL(uvel);

    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(uvel, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);

    /* Without angles: raw UVEL=3.0 at ocean points (no rotation) */
    ASSERT_NEAR(data[1], 3.0f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* ---- Pickup file exclusion ---- */

static int create_pickup_files(int iteration) {
    char path[512];
    char meta_buf[1024];

    /* pickup file */
    snprintf(path, sizeof(path), "%s/pickup.%010d.meta", test_dir, iteration);
    snprintf(meta_buf, sizeof(meta_buf),
        " nDims = [   3 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3,\n"
        "    2,    1,    2\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          2 ];\n"
        " timeStepNumber = [ %d ];\n"
        " nFlds = [    2 ];\n"
        " fldList = {\n"
        " 'Uvel    ' 'Vvel    '\n"
        " };\n", iteration);
    write_meta(path, meta_buf);

    size_t total = 2 * TEST_NZ * TEST_SLAB;
    float *data = calloc(total, sizeof(float));
    if (!data) return -1;
    snprintf(path, sizeof(path), "%s/pickup.%010d.data", test_dir, iteration);
    int rc = write_binary(path, data, total);
    free(data);
    if (rc != 0) return -1;

    /* pickup_ggl90 file */
    snprintf(path, sizeof(path), "%s/pickup_ggl90.%010d.meta", test_dir, iteration);
    snprintf(meta_buf, sizeof(meta_buf),
        " nDims = [   3 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3,\n"
        "    2,    1,    2\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          1 ];\n"
        " timeStepNumber = [ %d ];\n"
        " nFlds = [    1 ];\n"
        " fldList = {\n"
        " 'GGL90TKE'\n"
        " };\n", iteration);
    write_meta(path, meta_buf);

    data = calloc(TEST_NZ * TEST_SLAB, sizeof(float));
    if (!data) return -1;
    snprintf(path, sizeof(path), "%s/pickup_ggl90.%010d.data", test_dir, iteration);
    rc = write_binary(path, data, TEST_NZ * TEST_SLAB);
    free(data);
    return rc;
}

TEST(mitgcm_pickup_excluded) {
    setup_test_dir();
    ASSERT_EQ(create_grid_xy(), 0);
    ASSERT_EQ(create_rc(), 0);
    ASSERT_EQ(create_hfac(), 0);
    ASSERT_EQ(create_diag2d(100), 0);
    ASSERT_EQ(create_pickup_files(100), 0);

    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Verify no pickup-derived variables exist */
    for (USVar *v = vars; v; v = v->next) {
        /* pickup and pickup_ggl90 fields should not appear */
        ASSERT_TRUE(strcmp(v->name, "Uvel") != 0);
        ASSERT_TRUE(strcmp(v->name, "Vvel") != 0);
        ASSERT_TRUE(strcmp(v->name, "GGL90TKE") != 0);
    }

    /* But normal diagnostic variables should still be present */
    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* ---- Stdout calendar parsing ---- */

static int create_stdout_file(const char *filename,
                              int date1, int date2, double dt) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, filename);
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp,
        "(PID.TID 0000.0001) // ======================================================\n"
        "(PID.TID 0000.0001) // Parameter file \"data\"\n"
        "(PID.TID 0000.0001) > startDate_1=%d,\n"
        "(PID.TID 0000.0001) > startDate_2=%06d,\n"
        "(PID.TID 0000.0001) > deltaT    = %.1f,\n"
        "(PID.TID 0000.0001) // ======================================================\n",
        date1, date2, dt);
    fclose(fp);
    return 0;
}

TEST(mitgcm_stdout_calendar) {
    setup_test_dir();
    ASSERT_EQ(create_grid_xy(), 0);
    ASSERT_EQ(create_rc(), 0);
    ASSERT_EQ(create_hfac(), 0);
    ASSERT_EQ(create_diag2d(100), 0);
    ASSERT_EQ(create_diag2d(200), 0);
    ASSERT_EQ(create_stdout_file("STDOUT.0000", 19580101, 0, 3600.0), 0);

    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    int n_dims = 0;
    USDimInfo *dims = mitgcm_get_dim_info(etan, &n_dims);
    ASSERT_NOT_NULL(dims);
    ASSERT_GE(n_dims, 1);
    ASSERT_STR_EQ(dims[0].name, "time");

    /* Units should be CF-style "seconds since ..." */
    ASSERT_STR_EQ(dims[0].units, "seconds since 1958-01-01 00:00:00");

    /* Time values: iteration * deltaT = 100*3600, 200*3600 */
    ASSERT_NOT_NULL(dims[0].values);
    ASSERT_NEAR(dims[0].values[0], 360000.0, 0.01);
    ASSERT_NEAR(dims[0].values[1], 720000.0, 0.01);

    mitgcm_free_dim_info(dims, n_dims);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

TEST(mitgcm_no_stdout_uses_iterations) {
    /* Without stdout file, time values should remain as iteration numbers */
    ASSERT_EQ(create_full_dataset(), 0);

    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    USVar *etan = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "ETAN") == 0) { etan = v; break; }
    }
    ASSERT_NOT_NULL(etan);

    int n_dims = 0;
    USDimInfo *dims = mitgcm_get_dim_info(etan, &n_dims);
    ASSERT_NOT_NULL(dims);
    ASSERT_STR_EQ(dims[0].units, "iteration");
    ASSERT_NEAR(dims[0].values[0], 100.0, 0.01);
    ASSERT_NEAR(dims[0].values[1], 200.0, 0.01);

    mitgcm_free_dim_info(dims, n_dims);
    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* ---- Embedded velocity pairing (SIuice/SIvice) ---- */

static int create_seaice_vel_diag(int iteration, float uval, float vval) {
    char path[512];
    char meta_buf[1024];

    snprintf(path, sizeof(path), "%s/diagSeaice.%010d.meta", test_dir, iteration);
    snprintf(meta_buf, sizeof(meta_buf),
        " nDims = [   2 ];\n"
        " dimList = [\n"
        "    4,    1,    4,\n"
        "    3,    1,    3\n"
        " ];\n"
        " dataprec = [ 'float32' ];\n"
        " nrecords = [          2 ];\n"
        " timeStepNumber = [ %d ];\n"
        " nFlds = [    2 ];\n"
        " fldList = {\n"
        " 'SIuice  ' 'SIvice  '\n"
        " };\n", iteration);
    write_meta(path, meta_buf);

    size_t total = 2 * TEST_SLAB;
    float *data = malloc(total * sizeof(float));
    if (!data) return -1;

    for (int i = 0; i < TEST_SLAB; i++) data[i] = uval;
    for (int i = 0; i < TEST_SLAB; i++) data[TEST_SLAB + i] = vval;

    snprintf(path, sizeof(path), "%s/diagSeaice.%010d.data", test_dir, iteration);
    int rc = write_binary(path, data, total);
    free(data);
    return rc;
}

TEST(mitgcm_seaice_velocity_pairing) {
    setup_test_dir();
    ASSERT_EQ(create_grid_xy(), 0);
    ASSERT_EQ(create_rc(), 0);
    ASSERT_EQ(create_hfac(), 0);
    ASSERT_EQ(create_angle_cs_sn(0.6f, 0.8f), 0);
    ASSERT_EQ(create_seaice_vel_diag(100, 5.0f, 0.0f), 0);

    USFile *f = mitgcm_open(test_dir);
    ASSERT_NOT_NULL(f);
    USMesh *m = mitgcm_create_mesh(f);
    ASSERT_NOT_NULL(m);
    USVar *vars = mitgcm_scan_variables(f, m);
    ASSERT_NOT_NULL(vars);

    /* Find SIuice and SIvice */
    USVar *siuice = NULL, *sivice = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "SIuice") == 0) siuice = v;
        if (strcmp(v->name, "SIvice") == 0) sivice = v;
    }
    ASSERT_NOT_NULL(siuice);
    ASSERT_NOT_NULL(sivice);

    /* Verify rotation is applied by reading rotated values.
     * If pairing works, SIuice/SIvice get rotated just like UVEL/VVEL. */

    /* Read rotated SIuice: u_east = CS*U - SN*V = 0.6*5.0 - 0.8*0.0 = 3.0 */
    float data[TEST_SLAB];
    int rc = mitgcm_read_slice(siuice, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_NEAR(data[1], 3.0f, 0.01);  /* ocean point */

    /* Read rotated SIvice: v_north = SN*U + CS*V = 0.8*5.0 + 0.6*0.0 = 4.0 */
    rc = mitgcm_read_slice(sivice, 0, 0, data);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_NEAR(data[1], 4.0f, 0.01);

    mesh_free(m);
    mitgcm_close(f);
    cleanup_test_dir();
    return 1;
}

/* Close null */

TEST(mitgcm_close_null) {
    mitgcm_close(NULL);  /* Should not crash */
    return 1;
}

RUN_TESTS("MITgcm File I/O")
