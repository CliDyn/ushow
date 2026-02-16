/*
 * test_file_grib.c - Unit tests for GRIB file I/O
 *
 * Only compiled when WITH_GRIB=1 is set
 */

#include "test_framework.h"
#include "../src/ushow.defines.h"

#ifdef HAVE_GRIB

#include "../src/file_grib.h"
#include "../src/mesh.h"
#include <eccodes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

static const char *create_test_grib_file(void) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/test_ushow_grib_%d.grib", getpid());

    codes_handle *h = codes_handle_new_from_samples(NULL, "regular_ll_sfc_grib2");
    if (!h) return NULL;

    codes_set_long(h, "Ni", 4);
    codes_set_long(h, "Nj", 3);
    codes_set_double(h, "latitudeOfFirstGridPointInDegrees", -90.0);
    codes_set_double(h, "longitudeOfFirstGridPointInDegrees", 0.0);
    codes_set_double(h, "latitudeOfLastGridPointInDegrees", 90.0);
    codes_set_double(h, "longitudeOfLastGridPointInDegrees", 270.0);
    codes_set_long(h, "iDirectionIncrementInDegrees", 90);
    codes_set_long(h, "jDirectionIncrementInDegrees", 90);
    codes_set_long(h, "iScansNegatively", 0);
    codes_set_long(h, "jScansPositively", 1);

    size_t len = 0;
    len = strlen("t");
    codes_set_string(h, "shortName", "t", &len);
    len = strlen("surface");
    codes_set_string(h, "typeOfLevel", "surface", &len);
    codes_set_long(h, "level", 0);
    codes_set_long(h, "dataDate", 20250101);
    codes_set_long(h, "dataTime", 0);

    double values[12];
    for (int i = 0; i < 12; i++) values[i] = (double)i;
    codes_set_double_array(h, "values", values, 12);

    if (codes_write_message(h, path, "w") != CODES_SUCCESS) {
        codes_handle_delete(h);
        return NULL;
    }

    codes_handle_delete(h);
    return path;
}

static const char *create_test_grib_file_multilevel(void) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/test_ushow_grib_multi_%d.grib", getpid());

    codes_handle *base = codes_handle_new_from_samples(NULL, "regular_ll_sfc_grib2");
    if (!base) return NULL;

    codes_set_long(base, "Ni", 4);
    codes_set_long(base, "Nj", 3);
    codes_set_double(base, "latitudeOfFirstGridPointInDegrees", -90.0);
    codes_set_double(base, "longitudeOfFirstGridPointInDegrees", 0.0);
    codes_set_double(base, "latitudeOfLastGridPointInDegrees", 90.0);
    codes_set_double(base, "longitudeOfLastGridPointInDegrees", 270.0);
    codes_set_long(base, "iDirectionIncrementInDegrees", 90);
    codes_set_long(base, "jDirectionIncrementInDegrees", 90);
    codes_set_long(base, "iScansNegatively", 0);
    codes_set_long(base, "jScansPositively", 1);

    size_t len = strlen("t");
    codes_set_string(base, "shortName", "t", &len);
    len = strlen("isobaricInhPa");
    codes_set_string(base, "typeOfLevel", "isobaricInhPa", &len);

    int levels[] = {1000, 500};
    int times[] = {0, 600};
    int val = 0;

    for (size_t t = 0; t < 2; t++) {
        for (size_t l = 0; l < 2; l++) {
            codes_handle *h = codes_handle_clone(base);
            if (!h) {
                codes_handle_delete(base);
                return NULL;
            }
            codes_set_long(h, "level", levels[l]);
            codes_set_long(h, "dataDate", 20250101);
            codes_set_long(h, "dataTime", times[t]);

            double values[12];
            for (int i = 0; i < 12; i++) {
                values[i] = (double)(val++);
            }
            codes_set_double_array(h, "values", values, 12);

            if (codes_write_message(h, path, "a") != CODES_SUCCESS) {
                codes_handle_delete(h);
                codes_handle_delete(base);
                return NULL;
            }
            codes_handle_delete(h);
        }
    }

    codes_handle_delete(base);
    return path;
}

static void cleanup_test_grib_file(const char *path) {
    if (!path || !path[0]) return;
    unlink(path);
}

TEST(grib_is_grib_file_null) {
    ASSERT_EQ(grib_is_grib_file(NULL), 0);
    return 1;
}

TEST(grib_is_grib_file_nonexistent) {
    ASSERT_EQ(grib_is_grib_file("/nonexistent/path/to/file.grib"), 0);
    return 1;
}

TEST(grib_open_missing) {
    USFile *file = grib_open("/nonexistent/path/to/file.grib");
    ASSERT_NULL(file);
    return 1;
}

TEST(grib_open_and_mesh) {
    const char *path = create_test_grib_file();
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(grib_is_grib_file(path), 1);

    USFile *file = grib_open(path);
    ASSERT_NOT_NULL(file);
    ASSERT_EQ(file->file_type, FILE_TYPE_GRIB);

    USMesh *mesh = grib_create_mesh(file);
    ASSERT_NOT_NULL(mesh);
    ASSERT_TRUE(mesh->n_points > 0);

    mesh_free(mesh);
    grib_close(file);
    cleanup_test_grib_file(path);
    return 1;
}

TEST(grib_scan_variables_basic) {
    const char *path = create_test_grib_file();
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(grib_is_grib_file(path), 1);

    USFile *file = grib_open(path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = grib_create_mesh(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    int found = 0;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t@surface=0") == 0) {
            found = 1;
            ASSERT_TRUE(v->node_dim_id >= 0);
            ASSERT_EQ_SIZET(v->dim_sizes[v->node_dim_id], mesh->n_points);
            ASSERT_EQ_SIZET(v->n_dims, 1);
            ASSERT_EQ_INT(v->time_dim_id, -1);
            ASSERT_EQ_INT(v->depth_dim_id, -1);
        }
    }
    ASSERT_TRUE(found);

    mesh_free(mesh);
    grib_close(file);
    cleanup_test_grib_file(path);
    return 1;
}

TEST(grib_read_slice_basic) {
    const char *path = create_test_grib_file();
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(grib_is_grib_file(path), 1);

    USFile *file = grib_open(path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = grib_create_mesh(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    USVar *var = vars;
    ASSERT_NOT_NULL(var);

    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    ASSERT_EQ(grib_read_slice(var, 0, 0, data), 0);
    ASSERT_TRUE(fabsf(data[0]) < INVALID_DATA_THRESHOLD);

    free(data);
    mesh_free(mesh);
    grib_close(file);
    cleanup_test_grib_file(path);
    return 1;
}

/* Helper: write GRIB messages for given date/times into a file (append mode).
 * Grid: 4x3, variable "t", isobaricInhPa, levels 1000 & 500.
 * val_start: starting value counter for data (incremented per grid point). */
static int write_grib_messages(const char *path, const char *mode,
                                long data_date, const int *data_times, int n_times,
                                int *val_counter) {
    codes_handle *base = codes_handle_new_from_samples(NULL, "regular_ll_sfc_grib2");
    if (!base) return -1;

    codes_set_long(base, "Ni", 4);
    codes_set_long(base, "Nj", 3);
    codes_set_double(base, "latitudeOfFirstGridPointInDegrees", -90.0);
    codes_set_double(base, "longitudeOfFirstGridPointInDegrees", 0.0);
    codes_set_double(base, "latitudeOfLastGridPointInDegrees", 90.0);
    codes_set_double(base, "longitudeOfLastGridPointInDegrees", 270.0);
    codes_set_long(base, "iDirectionIncrementInDegrees", 90);
    codes_set_long(base, "jDirectionIncrementInDegrees", 90);
    codes_set_long(base, "iScansNegatively", 0);
    codes_set_long(base, "jScansPositively", 1);

    size_t len = strlen("t");
    codes_set_string(base, "shortName", "t", &len);
    len = strlen("isobaricInhPa");
    codes_set_string(base, "typeOfLevel", "isobaricInhPa", &len);

    int levels[] = {1000, 500};

    for (int t = 0; t < n_times; t++) {
        for (int l = 0; l < 2; l++) {
            codes_handle *h = codes_handle_clone(base);
            if (!h) { codes_handle_delete(base); return -1; }
            codes_set_long(h, "level", levels[l]);
            codes_set_long(h, "dataDate", data_date);
            codes_set_long(h, "dataTime", data_times[t]);

            double values[12];
            for (int i = 0; i < 12; i++) values[i] = (double)((*val_counter)++);
            codes_set_double_array(h, "values", values, 12);

            const char *wmode = (t == 0 && l == 0) ? mode : "a";
            if (codes_write_message(h, path, wmode) != CODES_SUCCESS) {
                codes_handle_delete(h);
                codes_handle_delete(base);
                return -1;
            }
            codes_handle_delete(h);
        }
    }
    codes_handle_delete(base);
    return 0;
}

/* Paths for fileset tests (static buffers) */
static char fs_path1[256];
static char fs_path2[256];

/* Create a 2-file fileset with no overlapping times.
 * File 1: date=20250101, times 0,600 (2 times x 2 levels = 4 messages)
 * File 2: date=20250102, times 0,600 (2 times x 2 levels = 4 messages)
 * Total unique times = 4 */
static int create_test_grib_fileset(void) {
    snprintf(fs_path1, sizeof(fs_path1), "/tmp/test_ushow_grib_fs1_%d.grib", getpid());
    snprintf(fs_path2, sizeof(fs_path2), "/tmp/test_ushow_grib_fs2_%d.grib", getpid());

    int val = 0;
    int times[] = {0, 600};
    if (write_grib_messages(fs_path1, "w", 20250101, times, 2, &val) != 0) return -1;
    if (write_grib_messages(fs_path2, "w", 20250102, times, 2, &val) != 0) return -1;
    return 0;
}

/* Create a 2-file fileset with overlapping times.
 * File 1: date=20250101, times 0,600
 * File 2: date=20250101 time=600 + date=20250102 time=0
 * Overlap: 20250101/600 appears in both files.
 * Total unique times = 3 */
static int create_test_grib_fileset_overlapping(void) {
    snprintf(fs_path1, sizeof(fs_path1), "/tmp/test_ushow_grib_ov1_%d.grib", getpid());
    snprintf(fs_path2, sizeof(fs_path2), "/tmp/test_ushow_grib_ov2_%d.grib", getpid());

    int val = 0;
    int times1[] = {0, 600};
    if (write_grib_messages(fs_path1, "w", 20250101, times1, 2, &val) != 0) return -1;

    /* File 2, message 1: 20250101/600 (overlap) */
    int times2a[] = {600};
    if (write_grib_messages(fs_path2, "w", 20250101, times2a, 1, &val) != 0) return -1;
    /* File 2, message 2: 20250102/0 (new) */
    int times2b[] = {0};
    if (write_grib_messages(fs_path2, "a", 20250102, times2b, 1, &val) != 0) return -1;

    return 0;
}

static void cleanup_fileset(void) {
    if (fs_path1[0]) { unlink(fs_path1); fs_path1[0] = '\0'; }
    if (fs_path2[0]) { unlink(fs_path2); fs_path2[0] = '\0'; }
}

/* ---- Fileset tests ---- */

TEST(grib_open_fileset_null) {
    ASSERT_NULL(grib_open_fileset(NULL, 0));
    ASSERT_NULL(grib_open_fileset(NULL, 2));
    const char *empty[] = {"a.grib"};
    ASSERT_NULL(grib_open_fileset(empty, 0));
    ASSERT_NULL(grib_open_fileset(empty, -1));
    return 1;
}

TEST(grib_close_fileset_null) {
    grib_close_fileset(NULL);
    return 1;
}

TEST(grib_fileset_total_times_basic) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    size_t total = grib_fileset_total_times(fs);
    /* 4 unique times: 20250101/0, 20250101/600, 20250102/0, 20250102/600 */
    ASSERT_EQ_SIZET(total, 4);

    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_fileset_total_times_overlapping) {
    ASSERT_EQ(create_test_grib_fileset_overlapping(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    size_t total = grib_fileset_total_times(fs);
    /* 3 unique times: 20250101/0, 20250101/600, 20250102/0 */
    ASSERT_EQ_SIZET(total, 3);

    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_scan_variables_fileset_basic) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = grib_create_mesh(fs->files[0]);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables_fileset(fs, mesh);
    ASSERT_NOT_NULL(vars);

    /* Should find "t" (multi-level, so name is just "t") */
    int found = 0;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t") == 0) {
            found = 1;
            break;
        }
    }
    ASSERT_TRUE(found);

    /* Clean up shallow var copies */
    USVar *v = vars;
    while (v) { USVar *next = v->next; free(v); v = next; }
    mesh_free(mesh);
    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_read_slice_fileset_basic) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = grib_create_mesh(fs->files[0]);
    ASSERT_NOT_NULL(mesh);

    /* Scan variables on file 0 to get var metadata */
    USVar *vars = grib_scan_variables(fs->files[0], mesh);
    ASSERT_NOT_NULL(vars);

    USVar *var = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t") == 0) { var = v; break; }
    }
    ASSERT_NOT_NULL(var);

    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    /* Read virtual time 0 (first file, first time) */
    ASSERT_EQ(grib_read_slice_fileset(fs, var, 0, 0, data), 0);
    ASSERT_TRUE(fabsf(data[0]) < INVALID_DATA_THRESHOLD);

    /* Read virtual time 2 (should be from second file) */
    ASSERT_EQ(grib_read_slice_fileset(fs, var, 2, 0, data), 0);
    ASSERT_TRUE(fabsf(data[0]) < INVALID_DATA_THRESHOLD);

    free(data);
    mesh_free(mesh);
    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_read_slice_fileset_boundary) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = grib_create_mesh(fs->files[0]);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables(fs->files[0], mesh);
    ASSERT_NOT_NULL(vars);

    USVar *var = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t") == 0) { var = v; break; }
    }
    ASSERT_NOT_NULL(var);

    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    /* Read last virtual time (time index 3 = second file, second time) */
    size_t last_time = grib_fileset_total_times(fs) - 1;
    ASSERT_EQ(grib_read_slice_fileset(fs, var, last_time, 0, data), 0);
    ASSERT_TRUE(fabsf(data[0]) < INVALID_DATA_THRESHOLD);

    /* Also read at depth_idx=1 (level 500) */
    ASSERT_EQ(grib_read_slice_fileset(fs, var, last_time, 1, data), 0);
    ASSERT_TRUE(fabsf(data[0]) < INVALID_DATA_THRESHOLD);

    free(data);
    mesh_free(mesh);
    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_get_dim_info_fileset_basic) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = grib_create_mesh(fs->files[0]);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables(fs->files[0], mesh);
    ASSERT_NOT_NULL(vars);

    USVar *var = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t") == 0) { var = v; break; }
    }
    ASSERT_NOT_NULL(var);

    int n_dims = 0;
    USDimInfo *dims = grib_get_dim_info_fileset(fs, var, &n_dims);
    ASSERT_NOT_NULL(dims);
    ASSERT_TRUE(n_dims >= 1);

    /* Find the time dimension */
    int found_time = 0;
    for (int i = 0; i < n_dims; i++) {
        if (strcmp(dims[i].name, "time") == 0) {
            found_time = 1;
            /* Should have 4 unique times */
            ASSERT_EQ_SIZET(dims[i].size, 4);
            ASSERT_NOT_NULL(dims[i].values);
            /* Values should be sorted (each subsequent >= previous) */
            for (size_t j = 1; j < dims[i].size; j++) {
                ASSERT_TRUE(dims[i].values[j] >= dims[i].values[j - 1]);
            }
            break;
        }
    }
    ASSERT_TRUE(found_time);

    grib_free_dim_info(dims, n_dims);
    mesh_free(mesh);
    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_read_timeseries_fileset_basic) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    USMesh *mesh = grib_create_mesh(fs->files[0]);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables(fs->files[0], mesh);
    ASSERT_NOT_NULL(vars);

    USVar *var = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t") == 0) { var = v; break; }
    }
    ASSERT_NOT_NULL(var);

    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n_out = 0;

    ASSERT_EQ(grib_read_timeseries_fileset(fs, var, 0, 0,
                                            &times, &values, &valid, &n_out), 0);
    /* Should return 4 time steps (4 unique times) */
    ASSERT_EQ_SIZET(n_out, grib_fileset_total_times(fs));
    ASSERT_NOT_NULL(times);
    ASSERT_NOT_NULL(values);
    ASSERT_NOT_NULL(valid);

    free(times);
    free(values);
    free(valid);
    mesh_free(mesh);
    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_fileset_map_time_basic) {
    ASSERT_EQ(create_test_grib_fileset(), 0);
    const char *files[] = {fs_path1, fs_path2};
    USFileSet *fs = grib_open_fileset(files, 2);
    ASSERT_NOT_NULL(fs);

    int file_idx = -1;
    size_t local_time = 999;

    /* Virtual time 0 -> file 0 */
    ASSERT_EQ(grib_fileset_map_time(fs, 0, &file_idx, &local_time), 0);
    ASSERT_EQ_INT(file_idx, 0);
    ASSERT_EQ_SIZET(local_time, 0);

    /* Virtual time 1 -> file 0, local time 1 */
    ASSERT_EQ(grib_fileset_map_time(fs, 1, &file_idx, &local_time), 0);
    ASSERT_EQ_INT(file_idx, 0);
    ASSERT_EQ_SIZET(local_time, 1);

    /* Virtual time 2 -> file 1, local time 0 */
    ASSERT_EQ(grib_fileset_map_time(fs, 2, &file_idx, &local_time), 0);
    ASSERT_EQ_INT(file_idx, 1);
    ASSERT_EQ_SIZET(local_time, 0);

    /* Virtual time 3 -> file 1, local time 1 */
    ASSERT_EQ(grib_fileset_map_time(fs, 3, &file_idx, &local_time), 0);
    ASSERT_EQ_INT(file_idx, 1);
    ASSERT_EQ_SIZET(local_time, 1);

    /* Out-of-range virtual time -> error */
    ASSERT_EQ(grib_fileset_map_time(fs, fs->total_times, &file_idx, &local_time), -1);

    /* NULL inputs -> error */
    ASSERT_EQ(grib_fileset_map_time(NULL, 0, &file_idx, &local_time), -1);
    ASSERT_EQ(grib_fileset_map_time(fs, 0, NULL, &local_time), -1);
    ASSERT_EQ(grib_fileset_map_time(fs, 0, &file_idx, NULL), -1);

    grib_close_fileset(fs);
    cleanup_fileset();
    return 1;
}

TEST(grib_multilevel_multitime) {
    const char *path = create_test_grib_file_multilevel();
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(grib_is_grib_file(path), 1);

    USFile *file = grib_open(path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = grib_create_mesh(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = grib_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    USVar *var = NULL;
    for (USVar *v = vars; v; v = v->next) {
        if (strcmp(v->name, "t") == 0) {
            var = v;
            break;
        }
    }
    ASSERT_NOT_NULL(var);
    ASSERT_TRUE(var->time_dim_id >= 0);
    ASSERT_TRUE(var->depth_dim_id >= 0);
    ASSERT_EQ_SIZET(var->dim_sizes[var->time_dim_id], 2);
    ASSERT_EQ_SIZET(var->dim_sizes[var->depth_dim_id], 2);

    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    ASSERT_EQ(grib_read_slice(var, 0, 0, data), 0);
    ASSERT_EQ(grib_read_slice(var, 1, 1, data), 0);

    free(data);
    mesh_free(mesh);
    grib_close(file);
    cleanup_test_grib_file(path);
    return 1;
}

RUN_TESTS("File Grib")

#else /* !HAVE_GRIB */

int main(void) {
    printf("GRIB support not compiled in. Skipping GRIB tests.\n");
    printf("Build with: make WITH_GRIB=1\n");
    return 0;
}

#endif /* HAVE_GRIB */
