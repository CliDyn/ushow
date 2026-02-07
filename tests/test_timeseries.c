/*
 * test_timeseries.c - Unit tests for time series reading and CF time conversion
 *
 * Tests netcdf_read_timeseries(), netcdf_read_timeseries_fileset(),
 * and CF time unit normalization across multi-file datasets.
 */

#include "test_framework.h"
#include "test_utils.h"
#include "../src/ushow.defines.h"
#include "../src/file_netcdf.h"
#include "../src/mesh.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ========== Helper: find variable by name ========== */

static USVar *find_var(USVar *vars, const char *name) {
    while (vars) {
        if (strcmp(vars->name, name) == 0) return vars;
        vars = vars->next;
    }
    return NULL;
}

/* ========== Helper: create multi-file test data ========== */

/*
 * Create a NetCDF file with a specific time units string and time offset.
 * Used for testing CF time unit conversion across filesets.
 */
static const char *create_test_netcdf_with_time_units(
    int nx, int ny, int nt, const char *time_units, double time_start) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_ts_%d_%d.nc",
             getpid(), test_file_counter++);
    unlink(filename);

    int ncid, lon_dimid, lat_dimid, time_dimid;
    int lon_varid, lat_varid, time_varid, data_varid;
    int dimids[3];
    int status;

    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    status = nc_def_dim(ncid, "lon", nx, &lon_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "lat", ny, &lat_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "time", nt, &time_dimid);
    NC_CHECK(status);

    status = nc_def_var(ncid, "lon", NC_DOUBLE, 1, &lon_dimid, &lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lon_varid, "units", 12, "degrees_east");
    NC_CHECK(status);

    status = nc_def_var(ncid, "lat", NC_DOUBLE, 1, &lat_dimid, &lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lat_varid, "units", 13, "degrees_north");
    NC_CHECK(status);

    status = nc_def_var(ncid, "time", NC_DOUBLE, 1, &time_dimid, &time_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, time_varid, "units",
                             strlen(time_units), time_units);
    NC_CHECK(status);

    dimids[0] = time_dimid;
    dimids[1] = lat_dimid;
    dimids[2] = lon_dimid;
    status = nc_def_var(ncid, "temperature", NC_FLOAT, 3, dimids, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "units", 1, "K");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    double *lon = malloc(nx * sizeof(double));
    double *lat = malloc(ny * sizeof(double));
    double *time_vals = malloc(nt * sizeof(double));
    float *data = malloc(nt * ny * nx * sizeof(float));

    if (!lon || !lat || !time_vals || !data) {
        free(lon); free(lat); free(time_vals); free(data);
        nc_close(ncid);
        return NULL;
    }

    for (int i = 0; i < nx; i++) lon[i] = -180.0 + 360.0 * i / nx;
    for (int j = 0; j < ny; j++) lat[j] = -90.0 + 180.0 * j / ny;
    for (int t = 0; t < nt; t++) time_vals[t] = time_start + t;

    for (int t = 0; t < nt; t++)
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++)
                data[t * ny * nx + j * nx + i] =
                    273.0f + (float)lat[j] * 0.5f + (float)(time_start + t) * 0.1f;

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_double(ncid, time_varid, time_vals);
    nc_put_var_float(ncid, data_varid, data);

    free(lon); free(lat); free(time_vals); free(data);
    nc_close(ncid);
    return filename;
}

/* ========== netcdf_read_timeseries: single file tests ========== */

/* Read time series from 1D structured file */
TEST(timeseries_1d_structured) {
    const char *filename = create_test_netcdf_1d_structured(10, 8, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    int status = netcdf_read_timeseries(temp, 0, 0, &times, &values, &valid, &n);
    ASSERT_EQ(status, 0);
    ASSERT_EQ_SIZET(n, 5);
    ASSERT_NOT_NULL(times);
    ASSERT_NOT_NULL(values);
    ASSERT_NOT_NULL(valid);

    /* Time values should be 0, 1, 2, 3, 4 */
    for (size_t t = 0; t < n; t++) {
        ASSERT_NEAR(times[t], (double)t, 1e-10);
    }

    /* All values should be valid */
    for (size_t t = 0; t < n; t++) {
        ASSERT_EQ_INT(valid[t], 1);
    }

    /* Values should be in reasonable temperature range */
    for (size_t t = 0; t < n; t++) {
        ASSERT_GT(values[t], 200.0f);
        ASSERT_LT(values[t], 400.0f);
    }

    free(times); free(values); free(valid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Time series values should change over time */
TEST(timeseries_values_change_over_time) {
    const char *filename = create_test_netcdf_1d_structured(10, 8, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    netcdf_read_timeseries(temp, 0, 0, &times, &values, &valid, &n);
    ASSERT_EQ_SIZET(n, 5);

    /* Data pattern: 273 + lat*0.5 + t*0.1, so values increase with time */
    int values_change = 0;
    for (size_t t = 1; t < n; t++) {
        if (values[t] != values[0]) { values_change = 1; break; }
    }
    ASSERT_TRUE(values_change);

    free(times); free(values); free(valid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Read time series from 3D data (time + depth + nodes) */
TEST(timeseries_3d_variable) {
    const char *filename = create_test_netcdf_3d(4, 3, 50);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    USVar *temp = find_var(vars, "temp");
    ASSERT_NOT_NULL(temp);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    /* Read at node 10, depth 1 */
    int status = netcdf_read_timeseries(temp, 10, 1, &times, &values, &valid, &n);
    ASSERT_EQ(status, 0);
    ASSERT_EQ_SIZET(n, 4);

    for (size_t t = 0; t < n; t++) {
        ASSERT_EQ_INT(valid[t], 1);
        ASSERT_GT(values[t], 200.0f);
        ASSERT_LT(values[t], 400.0f);
    }

    free(times); free(values); free(valid);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Different nodes should give different values */
TEST(timeseries_different_nodes) {
    const char *filename = create_test_netcdf_1d_structured(10, 8, 3);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    double *times1 = NULL, *times2 = NULL;
    float *values1 = NULL, *values2 = NULL;
    int *valid1 = NULL, *valid2 = NULL;
    size_t n1 = 0, n2 = 0;

    netcdf_read_timeseries(temp, 0, 0, &times1, &values1, &valid1, &n1);
    /* Node at a different latitude should have different values */
    netcdf_read_timeseries(temp, 10, 0, &times2, &values2, &valid2, &n2);

    ASSERT_EQ_SIZET(n1, n2);

    int different = 0;
    for (size_t t = 0; t < n1; t++) {
        if (values1[t] != values2[t]) { different = 1; break; }
    }
    ASSERT_TRUE(different);

    free(times1); free(values1); free(valid1);
    free(times2); free(values2); free(valid2);
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* ========== netcdf_read_timeseries: null/edge cases ========== */

/* NULL var should return error */
TEST(timeseries_null_var) {
    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    int status = netcdf_read_timeseries(NULL, 0, 0, &times, &values, &valid, &n);
    ASSERT_EQ(status, -1);
    ASSERT_NULL(times);
    ASSERT_NULL(values);
    return 1;
}

/* NULL output pointers should return error */
TEST(timeseries_null_outputs) {
    const char *filename = create_test_netcdf_1d_structured(4, 4, 2);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    USVar *vars = netcdf_scan_variables(file, mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    int status = netcdf_read_timeseries(temp, 0, 0, NULL, NULL, NULL, NULL);
    ASSERT_EQ(status, -1);

    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* ========== netcdf_read_timeseries_fileset tests ========== */

/* Basic fileset timeseries: two files with same time units */
TEST(timeseries_fileset_basic) {
    const char *f1 = create_test_netcdf_with_time_units(
        6, 4, 3, "days since 2000-01-01", 0.0);
    ASSERT_NOT_NULL(f1);
    char f1_copy[256];
    strncpy(f1_copy, f1, sizeof(f1_copy) - 1);
    f1_copy[sizeof(f1_copy) - 1] = '\0';

    const char *f2 = create_test_netcdf_with_time_units(
        6, 4, 2, "days since 2000-01-01", 3.0);
    ASSERT_NOT_NULL(f2);
    char f2_copy[256];
    strncpy(f2_copy, f2, sizeof(f2_copy) - 1);
    f2_copy[sizeof(f2_copy) - 1] = '\0';

    const char *filenames[] = {f1_copy, f2_copy};
    USFileSet *fs = netcdf_open_fileset(filenames, 2);
    ASSERT_NOT_NULL(fs);

    ASSERT_EQ_SIZET(fs->total_times, 5);

    USMesh *mesh = mesh_create_from_netcdf(fs->files[0]->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(fs->files[0], mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    int status = netcdf_read_timeseries_fileset(fs, temp, 0, 0,
                                                 &times, &values, &valid, &n);
    ASSERT_EQ(status, 0);
    ASSERT_EQ_SIZET(n, 5);

    /* Time values should be continuous: 0, 1, 2, 3, 4 */
    for (size_t t = 0; t < n; t++) {
        ASSERT_NEAR(times[t], (double)t, 1e-6);
    }

    /* All values should be valid */
    for (size_t t = 0; t < n; t++) {
        ASSERT_EQ_INT(valid[t], 1);
        ASSERT_GT(values[t], 200.0f);
        ASSERT_LT(values[t], 400.0f);
    }

    free(times); free(values); free(valid);
    mesh_free(mesh);
    netcdf_close_fileset(fs);
    cleanup_test_file(f1_copy);
    cleanup_test_file(f2_copy);
    return 1;
}

/* Fileset with different time units: tests CF time conversion */
TEST(timeseries_fileset_different_time_units) {
    /* File 1: days since 1950-01-01, times 0,1,2 (= Jan 1-3, 1950) */
    const char *f1 = create_test_netcdf_with_time_units(
        4, 4, 3, "days since 1950-01-01", 0.0);
    ASSERT_NOT_NULL(f1);
    char f1_copy[256];
    strncpy(f1_copy, f1, sizeof(f1_copy) - 1);
    f1_copy[sizeof(f1_copy) - 1] = '\0';

    /* File 2: days since 1960-01-01, times 0,1 (= Jan 1-2, 1960)
     * 1960-01-01 is 3652 days after 1950-01-01 (accounting for leap years) */
    const char *f2 = create_test_netcdf_with_time_units(
        4, 4, 2, "days since 1960-01-01", 0.0);
    ASSERT_NOT_NULL(f2);
    char f2_copy[256];
    strncpy(f2_copy, f2, sizeof(f2_copy) - 1);
    f2_copy[sizeof(f2_copy) - 1] = '\0';

    const char *filenames[] = {f1_copy, f2_copy};
    USFileSet *fs = netcdf_open_fileset(filenames, 2);
    ASSERT_NOT_NULL(fs);

    ASSERT_EQ_SIZET(fs->total_times, 5);

    USMesh *mesh = mesh_create_from_netcdf(fs->files[0]->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(fs->files[0], mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    int status = netcdf_read_timeseries_fileset(fs, temp, 0, 0,
                                                 &times, &values, &valid, &n);
    ASSERT_EQ(status, 0);
    ASSERT_EQ_SIZET(n, 5);

    /* First 3 times from file 1: 0, 1, 2 days since 1950 */
    ASSERT_NEAR(times[0], 0.0, 1e-6);
    ASSERT_NEAR(times[1], 1.0, 1e-6);
    ASSERT_NEAR(times[2], 2.0, 1e-6);

    /* File 2 times: 0 and 1 in "days since 1960-01-01"
     * Should be converted to "days since 1950-01-01"
     * 1950-01-01 to 1960-01-01 = 3652 days (1952 and 1956 are leap years)
     */
    ASSERT_NEAR(times[3], 3652.0, 1e-6);
    ASSERT_NEAR(times[4], 3653.0, 1e-6);

    /* Time values should be monotonically increasing */
    for (size_t t = 1; t < n; t++) {
        ASSERT_GT(times[t], times[t - 1]);
    }

    free(times); free(values); free(valid);
    mesh_free(mesh);
    netcdf_close_fileset(fs);
    cleanup_test_file(f1_copy);
    cleanup_test_file(f2_copy);
    return 1;
}

/* Fileset with different unit scales (hours vs days) */
TEST(timeseries_fileset_different_unit_scales) {
    /* Use explicit filenames to ensure deterministic sort order */
    const char *sorted_f1 = "/tmp/test_ushow_units_a.nc";
    const char *sorted_f2 = "/tmp/test_ushow_units_b.nc";
    unlink(sorted_f1);
    unlink(sorted_f2);

    /* File 1: days since 2000-01-01, times 0,1 */
    const char *f1 = create_test_netcdf_with_time_units(
        4, 4, 2, "days since 2000-01-01", 0.0);
    ASSERT_NOT_NULL(f1);
    rename(f1, sorted_f1);

    /* File 2: hours since 2000-01-03, times 0,24
     * 0 hours since 2000-01-03 = 2 days since 2000-01-01
     * 24 hours since 2000-01-03 = 3 days since 2000-01-01
     */
    const char *f2 = create_test_netcdf_with_time_units(
        4, 4, 2, "hours since 2000-01-03", 0.0);
    ASSERT_NOT_NULL(f2);
    rename(f2, sorted_f2);

    /* Override file 2 time values to be 0 and 24 hours */
    {
        int ncid;
        nc_open(sorted_f2, NC_WRITE, &ncid);
        int time_varid;
        nc_inq_varid(ncid, "time", &time_varid);
        double time_vals[] = {0.0, 24.0};
        nc_put_var_double(ncid, time_varid, time_vals);
        nc_close(ncid);
    }

    const char *filenames[] = {sorted_f1, sorted_f2};
    USFileSet *fs = netcdf_open_fileset(filenames, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = mesh_create_from_netcdf(fs->files[0]->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(fs->files[0], mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    int status = netcdf_read_timeseries_fileset(fs, temp, 0, 0,
                                                 &times, &values, &valid, &n);
    ASSERT_EQ(status, 0);
    ASSERT_EQ_SIZET(n, 4);

    /* File 1: 0, 1 days since 2000-01-01 */
    ASSERT_NEAR(times[0], 0.0, 1e-6);
    ASSERT_NEAR(times[1], 1.0, 1e-6);

    /* File 2: 0h, 24h since 2000-01-03 → 2.0, 3.0 days since 2000-01-01 */
    ASSERT_NEAR(times[2], 2.0, 1e-6);
    ASSERT_NEAR(times[3], 3.0, 1e-6);

    free(times); free(values); free(valid);
    mesh_free(mesh);
    netcdf_close_fileset(fs);
    cleanup_test_file(sorted_f1);
    cleanup_test_file(sorted_f2);
    return 1;
}

/* Fileset null inputs */
TEST(timeseries_fileset_null) {
    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n = 0;

    int status = netcdf_read_timeseries_fileset(NULL, NULL, 0, 0,
                                                 &times, &values, &valid, &n);
    ASSERT_EQ(status, -1);
    return 1;
}

/* ========== TSData struct tests ========== */

/* TSData can be initialized and populated */
TEST(tsdata_struct_basic) {
    TSData ts;
    memset(&ts, 0, sizeof(TSData));

    ts.n_points = 10;
    ts.times = calloc(ts.n_points, sizeof(double));
    ts.values = calloc(ts.n_points, sizeof(float));
    ts.valid = calloc(ts.n_points, sizeof(int));

    ASSERT_NOT_NULL(ts.times);
    ASSERT_NOT_NULL(ts.values);
    ASSERT_NOT_NULL(ts.valid);

    for (size_t i = 0; i < ts.n_points; i++) {
        ts.times[i] = (double)i;
        ts.values[i] = 273.0f + (float)i;
        ts.valid[i] = 1;
    }
    ts.n_valid = ts.n_points;

    snprintf(ts.title, sizeof(ts.title), "temperature (K) at 10.5, 55.3");
    snprintf(ts.x_label, sizeof(ts.x_label), "days since 2000-01-01");
    snprintf(ts.y_label, sizeof(ts.y_label), "temperature (K)");

    ASSERT_EQ_SIZET(ts.n_points, 10);
    ASSERT_EQ_SIZET(ts.n_valid, 10);
    ASSERT_NEAR(ts.times[0], 0.0, 1e-10);
    ASSERT_NEAR(ts.values[9], 282.0f, 1e-6);
    ASSERT_STR_EQ(ts.x_label, "days since 2000-01-01");

    free(ts.times); free(ts.values); free(ts.valid);
    return 1;
}

/* TSData with fill values */
TEST(tsdata_with_fill_values) {
    TSData ts;
    memset(&ts, 0, sizeof(TSData));

    ts.n_points = 5;
    ts.times = calloc(ts.n_points, sizeof(double));
    ts.values = calloc(ts.n_points, sizeof(float));
    ts.valid = calloc(ts.n_points, sizeof(int));
    ASSERT_NOT_NULL(ts.times);

    /* Mark some points as invalid */
    ts.valid[0] = 1; ts.values[0] = 273.0f;
    ts.valid[1] = 0; ts.values[1] = DEFAULT_FILL_VALUE;
    ts.valid[2] = 1; ts.values[2] = 275.0f;
    ts.valid[3] = 0; ts.values[3] = DEFAULT_FILL_VALUE;
    ts.valid[4] = 1; ts.values[4] = 277.0f;

    ts.n_valid = 0;
    for (size_t i = 0; i < ts.n_points; i++)
        if (ts.valid[i]) ts.n_valid++;

    ASSERT_EQ_SIZET(ts.n_valid, 3);

    free(ts.times); free(ts.values); free(ts.valid);
    return 1;
}

/* ========== Dimension info with fileset time normalization ========== */

/* get_dim_info_fileset should normalize time values across files */
TEST(dim_info_fileset_time_normalization) {
    const char *f1 = create_test_netcdf_with_time_units(
        4, 4, 2, "days since 1950-01-01", 0.0);
    ASSERT_NOT_NULL(f1);
    char f1_copy[256];
    strncpy(f1_copy, f1, sizeof(f1_copy) - 1);
    f1_copy[sizeof(f1_copy) - 1] = '\0';

    const char *f2 = create_test_netcdf_with_time_units(
        4, 4, 2, "days since 1960-01-01", 0.0);
    ASSERT_NOT_NULL(f2);
    char f2_copy[256];
    strncpy(f2_copy, f2, sizeof(f2_copy) - 1);
    f2_copy[sizeof(f2_copy) - 1] = '\0';

    const char *filenames[] = {f1_copy, f2_copy};
    USFileSet *fs = netcdf_open_fileset(filenames, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = mesh_create_from_netcdf(fs->files[0]->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(fs->files[0], mesh);
    USVar *temp = find_var(vars, "temperature");
    ASSERT_NOT_NULL(temp);

    int n_dims;
    USDimInfo *dims = netcdf_get_dim_info_fileset(fs, temp, &n_dims);
    ASSERT_NOT_NULL(dims);
    ASSERT_GT(n_dims, 0);

    /* Find the time dimension */
    int found_time = 0;
    for (int i = 0; i < n_dims; i++) {
        if (strcmp(dims[i].name, "time") == 0) {
            found_time = 1;
            ASSERT_EQ_SIZET(dims[i].size, 4);
            ASSERT_NOT_NULL(dims[i].values);

            /* File 1 values: 0, 1 (days since 1950) */
            ASSERT_NEAR(dims[i].values[0], 0.0, 1e-6);
            ASSERT_NEAR(dims[i].values[1], 1.0, 1e-6);

            /* File 2 values: converted from days since 1960 to days since 1950 */
            ASSERT_NEAR(dims[i].values[2], 3652.0, 1e-6);
            ASSERT_NEAR(dims[i].values[3], 3653.0, 1e-6);
        }
    }
    ASSERT_TRUE(found_time);

    netcdf_free_dim_info(dims, n_dims);
    mesh_free(mesh);
    netcdf_close_fileset(fs);
    cleanup_test_file(f1_copy);
    cleanup_test_file(f2_copy);
    return 1;
}

RUN_TESTS("Timeseries")
