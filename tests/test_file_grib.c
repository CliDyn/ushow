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
