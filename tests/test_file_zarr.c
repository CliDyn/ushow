/*
 * test_file_zarr.c - Unit tests for Zarr file I/O
 *
 * Only compiled when WITH_ZARR=1 is set
 */

#include "test_framework.h"
#include "../src/ushow.defines.h"

#ifdef HAVE_ZARR

#include "../src/file_zarr.h"
#include "../src/mesh.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

/* Counter for unique store names */
static int zarr_test_counter = 0;

/*
 * Helper: Write a string to a file in a zarr store
 */
static int write_zarr_file(const char *store_path, const char *name, const char *content) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", store_path, name);

    FILE *f = fopen(filepath, "w");
    if (!f) return -1;
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

/*
 * Helper: Write binary data to a chunk file (uncompressed)
 */
static int write_zarr_chunk(const char *store_path, const char *array_name,
                            const char *chunk_name, const void *data, size_t size) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s/%s", store_path, array_name, chunk_name);

    FILE *f = fopen(filepath, "wb");
    if (!f) return -1;
    fwrite(data, 1, size, f);
    fclose(f);
    return 0;
}

/*
 * Create a minimal zarr store for testing.
 * Returns path to store (static buffer) or NULL on error.
 */
static const char *create_test_zarr_store(int n_nodes, int n_times) {
    static char store_path[256];
    snprintf(store_path, sizeof(store_path), "/tmp/test_ushow_zarr_%d_%d.zarr",
             getpid(), zarr_test_counter++);

    /* Remove any existing store */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", store_path);
    int ret = system(cmd);
    (void)ret;

    /* Create store directory */
    if (mkdir(store_path, 0755) != 0) return NULL;

    /* Write .zgroup */
    if (write_zarr_file(store_path, ".zgroup", "{\"zarr_format\":2}") != 0) return NULL;

    /* Write .zattrs */
    if (write_zarr_file(store_path, ".zattrs", "{}") != 0) return NULL;

    /* Create latitude array directory */
    char array_dir[512];
    snprintf(array_dir, sizeof(array_dir), "%s/latitude", store_path);
    if (mkdir(array_dir, 0755) != 0) return NULL;

    /* latitude .zarray metadata */
    char zarray_lat[1024];
    snprintf(zarray_lat, sizeof(zarray_lat),
             "{"
             "\"chunks\":[%d],"
             "\"compressor\":null,"
             "\"dtype\":\"<f8\","
             "\"fill_value\":\"NaN\","
             "\"filters\":null,"
             "\"order\":\"C\","
             "\"shape\":[%d],"
             "\"zarr_format\":2"
             "}",
             n_nodes, n_nodes);
    if (write_zarr_file(store_path, "latitude/.zarray", zarray_lat) != 0) return NULL;
    if (write_zarr_file(store_path, "latitude/.zattrs", "{\"units\":\"degrees_north\"}") != 0) return NULL;

    /* Write latitude data chunk */
    double *lat_data = malloc(n_nodes * sizeof(double));
    if (!lat_data) return NULL;
    for (int i = 0; i < n_nodes; i++) {
        lat_data[i] = -90.0 + 180.0 * i / (n_nodes - 1);
    }
    if (write_zarr_chunk(store_path, "latitude", "0", lat_data, n_nodes * sizeof(double)) != 0) {
        free(lat_data);
        return NULL;
    }
    free(lat_data);

    /* Create longitude array directory */
    snprintf(array_dir, sizeof(array_dir), "%s/longitude", store_path);
    if (mkdir(array_dir, 0755) != 0) return NULL;

    /* longitude .zarray metadata */
    char zarray_lon[1024];
    snprintf(zarray_lon, sizeof(zarray_lon),
             "{"
             "\"chunks\":[%d],"
             "\"compressor\":null,"
             "\"dtype\":\"<f8\","
             "\"fill_value\":\"NaN\","
             "\"filters\":null,"
             "\"order\":\"C\","
             "\"shape\":[%d],"
             "\"zarr_format\":2"
             "}",
             n_nodes, n_nodes);
    if (write_zarr_file(store_path, "longitude/.zarray", zarray_lon) != 0) return NULL;
    if (write_zarr_file(store_path, "longitude/.zattrs", "{\"units\":\"degrees_east\"}") != 0) return NULL;

    /* Write longitude data chunk */
    double *lon_data = malloc(n_nodes * sizeof(double));
    if (!lon_data) return NULL;
    for (int i = 0; i < n_nodes; i++) {
        lon_data[i] = -180.0 + 360.0 * i / (n_nodes - 1);
    }
    if (write_zarr_chunk(store_path, "longitude", "0", lon_data, n_nodes * sizeof(double)) != 0) {
        free(lon_data);
        return NULL;
    }
    free(lon_data);

    /* Create time array directory */
    snprintf(array_dir, sizeof(array_dir), "%s/time", store_path);
    if (mkdir(array_dir, 0755) != 0) return NULL;

    char zarray_time[1024];
    snprintf(zarray_time, sizeof(zarray_time),
             "{"
             "\"chunks\":[%d],"
             "\"compressor\":null,"
             "\"dtype\":\"<f8\","
             "\"fill_value\":\"NaN\","
             "\"filters\":null,"
             "\"order\":\"C\","
             "\"shape\":[%d],"
             "\"zarr_format\":2"
             "}",
             n_times, n_times);
    if (write_zarr_file(store_path, "time/.zarray", zarray_time) != 0) return NULL;
    if (write_zarr_file(store_path, "time/.zattrs", "{\"units\":\"days since 2000-01-01\"}") != 0) return NULL;

    double *time_data = malloc(n_times * sizeof(double));
    if (!time_data) return NULL;
    for (int t = 0; t < n_times; t++) {
        time_data[t] = t;
    }
    if (write_zarr_chunk(store_path, "time", "0", time_data, n_times * sizeof(double)) != 0) {
        free(time_data);
        return NULL;
    }
    free(time_data);

    /* Create data variable (temperature) */
    snprintf(array_dir, sizeof(array_dir), "%s/temperature", store_path);
    if (mkdir(array_dir, 0755) != 0) return NULL;

    char zarray_data[1024];
    snprintf(zarray_data, sizeof(zarray_data),
             "{"
             "\"chunks\":[1,%d],"
             "\"compressor\":null,"
             "\"dtype\":\"<f4\","
             "\"fill_value\":1e20,"
             "\"filters\":null,"
             "\"order\":\"C\","
             "\"shape\":[%d,%d],"
             "\"zarr_format\":2"
             "}",
             n_nodes, n_times, n_nodes);
    if (write_zarr_file(store_path, "temperature/.zarray", zarray_data) != 0) return NULL;

    char zattrs_data[256];
    snprintf(zattrs_data, sizeof(zattrs_data),
             "{\"units\":\"K\",\"long_name\":\"Temperature\",\"_ARRAY_DIMENSIONS\":[\"time\",\"ncells\"]}");
    if (write_zarr_file(store_path, "temperature/.zattrs", zattrs_data) != 0) return NULL;

    /* Write data chunks - one per time step */
    float *data = malloc(n_nodes * sizeof(float));
    if (!data) return NULL;

    for (int t = 0; t < n_times; t++) {
        for (int i = 0; i < n_nodes; i++) {
            double lat = -90.0 + 180.0 * i / (n_nodes - 1);
            data[i] = 273.0f + (float)lat * 0.5f + (float)t * 0.1f;
        }
        char chunk_name[32];
        snprintf(chunk_name, sizeof(chunk_name), "%d.0", t);
        if (write_zarr_chunk(store_path, "temperature", chunk_name, data, n_nodes * sizeof(float)) != 0) {
            free(data);
            return NULL;
        }
    }
    free(data);

    return store_path;
}

/*
 * Remove a test zarr store.
 */
static void cleanup_test_zarr(const char *store_path) {
    if (store_path) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", store_path);
        int ret = system(cmd);
        (void)ret;
    }
}

/* Test zarr_is_zarr_store with NULL */
TEST(zarr_is_zarr_store_null) {
    int result = zarr_is_zarr_store(NULL);
    ASSERT_EQ(result, 0);
    return 1;
}

/* Test zarr_is_zarr_store with nonexistent path */
TEST(zarr_is_zarr_store_nonexistent) {
    int result = zarr_is_zarr_store("/nonexistent/path/to/store.zarr");
    ASSERT_EQ(result, 0);
    return 1;
}

/* Test zarr_is_zarr_store with valid zarr store */
TEST(zarr_is_zarr_store_valid) {
    const char *store_path = create_test_zarr_store(100, 3);
    ASSERT_NOT_NULL(store_path);

    int result = zarr_is_zarr_store(store_path);
    ASSERT_EQ(result, 1);

    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_is_zarr_store with regular directory */
TEST(zarr_is_zarr_store_not_zarr) {
    /* /tmp is a directory but not a zarr store */
    int result = zarr_is_zarr_store("/tmp");
    ASSERT_EQ(result, 0);
    return 1;
}

/* Test zarr_open with NULL */
TEST(zarr_open_null) {
    USFile *file = zarr_open(NULL);
    ASSERT_NULL(file);
    return 1;
}

/* Test zarr_open with nonexistent store */
TEST(zarr_open_nonexistent) {
    USFile *file = zarr_open("/nonexistent/path/to/store.zarr");
    ASSERT_NULL(file);
    return 1;
}

/* Test zarr_open with valid store */
TEST(zarr_open_valid) {
    const char *store_path = create_test_zarr_store(100, 3);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    ASSERT_EQ(file->file_type, FILE_TYPE_ZARR);
    ASSERT_STR_EQ(file->filename, store_path);

    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_close with NULL */
TEST(zarr_close_null) {
    zarr_close(NULL);  /* Should not crash */
    return 1;
}

/* Test mesh creation from zarr */
TEST(mesh_from_zarr) {
    const char *store_path = create_test_zarr_store(100, 2);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    ASSERT_EQ_SIZET(mesh->n_points, 100);

    /* Check coordinate values are reasonable */
    ASSERT_GE(mesh->lon[0], -180.0);
    ASSERT_LE(mesh->lon[0], 180.0);
    ASSERT_GE(mesh->lat[0], -90.0);
    ASSERT_LE(mesh->lat[0], 90.0);

    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_scan_variables */
TEST(zarr_scan_variables_basic) {
    const char *store_path = create_test_zarr_store(100, 3);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = zarr_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Should find temperature variable */
    int found_temp = 0;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "temperature") == 0) {
            found_temp = 1;
            ASSERT_STR_EQ(v->units, "K");
        }
        v = v->next;
    }
    ASSERT_TRUE(found_temp);

    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_read_slice */
TEST(zarr_read_slice_basic) {
    const char *store_path = create_test_zarr_store(100, 3);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = zarr_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Find temperature variable */
    USVar *temp = NULL;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "temperature") == 0) {
            temp = v;
            break;
        }
        v = v->next;
    }
    ASSERT_NOT_NULL(temp);

    /* Read first time step */
    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    int status = zarr_read_slice(temp, 0, 0, data);
    ASSERT_EQ(status, 0);

    /* Verify data is in expected range (temperature in K) */
    int valid_count = 0;
    for (size_t i = 0; i < mesh->n_points; i++) {
        if (data[i] > 200.0f && data[i] < 400.0f) {
            valid_count++;
        }
    }
    ASSERT_GT(valid_count, 0);

    free(data);
    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_read_slice at different time steps */
TEST(zarr_read_slice_time_steps) {
    const char *store_path = create_test_zarr_store(100, 5);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = zarr_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Find temperature variable */
    USVar *temp = NULL;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "temperature") == 0) {
            temp = v;
            break;
        }
        v = v->next;
    }
    ASSERT_NOT_NULL(temp);

    float *data0 = malloc(mesh->n_points * sizeof(float));
    float *data4 = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data0);
    ASSERT_NOT_NULL(data4);

    zarr_read_slice(temp, 0, 0, data0);
    zarr_read_slice(temp, 4, 0, data4);

    /* Data should differ between time steps */
    int different = 0;
    for (size_t i = 0; i < mesh->n_points; i++) {
        if (data0[i] != data4[i]) {
            different = 1;
            break;
        }
    }
    ASSERT_TRUE(different);

    free(data0);
    free(data4);
    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_estimate_range */
TEST(zarr_estimate_range_basic) {
    const char *store_path = create_test_zarr_store(100, 3);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = zarr_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Find temperature variable */
    USVar *temp = NULL;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "temperature") == 0) {
            temp = v;
            break;
        }
        v = v->next;
    }
    ASSERT_NOT_NULL(temp);

    float min_val, max_val;
    int status = zarr_estimate_range(temp, &min_val, &max_val);
    ASSERT_EQ(status, 0);

    /* Temperature data should be in reasonable range */
    ASSERT_GT(min_val, 200.0f);
    ASSERT_LT(max_val, 400.0f);
    ASSERT_LT(min_val, max_val);

    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_get_dim_info */
TEST(zarr_get_dim_info_basic) {
    const char *store_path = create_test_zarr_store(100, 5);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = zarr_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Find temperature variable */
    USVar *temp = NULL;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "temperature") == 0) {
            temp = v;
            break;
        }
        v = v->next;
    }
    ASSERT_NOT_NULL(temp);

    int n_dims;
    USDimInfo *dims = zarr_get_dim_info(temp, &n_dims);

    if (dims) {
        ASSERT_GT(n_dims, 0);

        /* Check that dimensions have valid size info
         * Note: dimension names may be empty for zarr stores without
         * consolidated metadata or _ARRAY_DIMENSIONS attributes */
        for (int i = 0; i < n_dims; i++) {
            ASSERT_GT(dims[i].size, 0);
        }

        zarr_free_dim_info(dims, n_dims);
    }

    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

/* Test zarr_free_dim_info with NULL */
TEST(zarr_free_dim_info_null) {
    zarr_free_dim_info(NULL, 0);  /* Should not crash */
    return 1;
}

/* Test USVar structure fields for zarr */
TEST(zarr_usvar_structure) {
    const char *store_path = create_test_zarr_store(50, 2);
    ASSERT_NOT_NULL(store_path);

    USFile *file = zarr_open(store_path);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_zarr(file);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = zarr_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Check USVar structure is properly populated */
    USVar *v = vars;
    while (v) {
        ASSERT_GT(strlen(v->name), 0);
        ASSERT_EQ(v->file, file);
        ASSERT_EQ(v->file->file_type, FILE_TYPE_ZARR);

        v = v->next;
    }

    mesh_free(mesh);
    zarr_close(file);
    cleanup_test_zarr(store_path);
    return 1;
}

RUN_TESTS("File Zarr")

#else /* !HAVE_ZARR */

/* Stub main when zarr support is not compiled in */
int main(void) {
    printf("Zarr support not compiled in. Skipping zarr tests.\n");
    printf("Build with: make WITH_ZARR=1\n");
    return 0;
}

#endif /* HAVE_ZARR */
