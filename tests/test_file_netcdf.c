/*
 * test_file_netcdf.c - Unit tests for NetCDF file I/O
 */

#include "test_framework.h"
#include "test_utils.h"
#include "../src/ushow.defines.h"
#include "../src/file_netcdf.h"
#include "../src/mesh.h"
#include <stdlib.h>
#include <string.h>

/* Test netcdf_open with NULL filename */
TEST(netcdf_open_null) {
    USFile *file = netcdf_open(NULL);
    ASSERT_NULL(file);
    return 1;
}

/* Test netcdf_open with nonexistent file */
TEST(netcdf_open_nonexistent) {
    USFile *file = netcdf_open("/nonexistent/path/to/file.nc");
    ASSERT_NULL(file);
    return 1;
}

/* Test netcdf_open with valid 1D structured file */
TEST(netcdf_open_1d_structured) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    ASSERT_STR_EQ(file->filename, filename);
    ASSERT_EQ(file->file_type, FILE_TYPE_NETCDF);
    ASSERT_TRUE(file->ncid > 0);

    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_open with 2D curvilinear file */
TEST(netcdf_open_2d_curvilinear) {
    const char *filename = create_test_netcdf_2d_curvilinear(20, 15);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_open with unstructured file */
TEST(netcdf_open_unstructured) {
    const char *filename = create_test_netcdf_unstructured(1000);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_close with NULL */
TEST(netcdf_close_null) {
    netcdf_close(NULL);  /* Should not crash */
    return 1;
}

/* Test netcdf_scan_variables finds temperature variable */
TEST(netcdf_scan_variables_1d) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    /* Create mesh from file */
    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Should find "temperature" variable */
    USVar *v = vars;
    int found_temp = 0;
    while (v) {
        if (strcmp(v->name, "temperature") == 0) {
            found_temp = 1;
            ASSERT_STR_EQ(v->units, "K");
            ASSERT_EQ(v->n_dims, 3);  /* time, lat, lon */
            ASSERT_TRUE(v->time_dim_id >= 0);  /* Has time dimension */
        }
        v = v->next;
    }
    ASSERT_TRUE(found_temp);

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_scan_variables with 2D curvilinear */
TEST(netcdf_scan_variables_2d) {
    const char *filename = create_test_netcdf_2d_curvilinear(20, 15);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Should find "sst" variable */
    int found_sst = 0;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "sst") == 0) {
            found_sst = 1;
        }
        v = v->next;
    }
    ASSERT_TRUE(found_sst);

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_read_slice basic */
TEST(netcdf_read_slice_basic) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Read first time step */
    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    int status = netcdf_read_slice(vars, 0, 0, data);
    ASSERT_EQ(status, 0);

    /* Verify data is in expected range (temperature in K) */
    for (size_t i = 0; i < mesh->n_points; i++) {
        ASSERT_GT(data[i], 200.0f);
        ASSERT_LT(data[i], 400.0f);
    }

    free(data);

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_read_slice at different time steps */
TEST(netcdf_read_slice_time_steps) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float *data0 = malloc(mesh->n_points * sizeof(float));
    float *data4 = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data0);
    ASSERT_NOT_NULL(data4);

    netcdf_read_slice(vars, 0, 0, data0);
    netcdf_read_slice(vars, 4, 0, data4);

    /* Data should differ between time steps (we added time-dependent offset) */
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

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_estimate_range */
TEST(netcdf_estimate_range_basic) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 3);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    float min_val, max_val;
    int status = netcdf_estimate_range(vars, &min_val, &max_val);
    ASSERT_EQ(status, 0);

    /* Temperature data should be in reasonable range */
    ASSERT_GT(min_val, 200.0f);
    ASSERT_LT(max_val, 400.0f);
    ASSERT_LT(min_val, max_val);

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_get_dim_info */
TEST(netcdf_get_dim_info_basic) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 5);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    int n_dims;
    USDimInfo *dims = netcdf_get_dim_info(vars, &n_dims);

    if (dims) {
        ASSERT_GT(n_dims, 0);

        /* Check that dimensions have valid info */
        for (int i = 0; i < n_dims; i++) {
            ASSERT_GT(strlen(dims[i].name), 0);
            ASSERT_GT(dims[i].size, 0);
        }

        netcdf_free_dim_info(dims, n_dims);
    }

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test with 3D data (time, depth, nodes) */
TEST(netcdf_3d_variable) {
    const char *filename = create_test_netcdf_3d(3, 5, 100);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Should find "temp" variable */
    USVar *temp = NULL;
    USVar *v = vars;
    while (v) {
        if (strcmp(v->name, "temp") == 0) {
            temp = v;
            break;
        }
        v = v->next;
    }
    ASSERT_NOT_NULL(temp);

    /* Should have both time and depth dimensions */
    ASSERT_TRUE(temp->time_dim_id >= 0);
    ASSERT_TRUE(temp->depth_dim_id >= 0);

    /* Read a slice at specific time/depth */
    float *data = malloc(mesh->n_points * sizeof(float));
    ASSERT_NOT_NULL(data);

    int status = netcdf_read_slice(temp, 1, 2, data);
    ASSERT_EQ(status, 0);

    /* Verify data range */
    for (size_t i = 0; i < mesh->n_points; i++) {
        ASSERT_GT(data[i], 200.0f);
        ASSERT_LT(data[i], 400.0f);
    }

    free(data);

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test mesh creation from NetCDF for structured data */
TEST(mesh_from_netcdf_structured) {
    const char *filename = create_test_netcdf_1d_structured(36, 18, 1);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    /* 36 x 18 = 648 points */
    ASSERT_EQ_SIZET(mesh->n_points, 36 * 18);
    ASSERT_EQ(mesh->coord_type, COORD_TYPE_1D_STRUCTURED);

    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test mesh creation from NetCDF for curvilinear data */
TEST(mesh_from_netcdf_curvilinear) {
    const char *filename = create_test_netcdf_2d_curvilinear(20, 15);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    /* 20 x 15 = 300 points */
    ASSERT_EQ_SIZET(mesh->n_points, 20 * 15);
    ASSERT_EQ(mesh->coord_type, COORD_TYPE_2D_CURVILINEAR);

    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test mesh creation from NetCDF for unstructured data */
TEST(mesh_from_netcdf_unstructured) {
    const char *filename = create_test_netcdf_unstructured(500);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    ASSERT_EQ_SIZET(mesh->n_points, 500);
    ASSERT_EQ(mesh->coord_type, COORD_TYPE_1D_UNSTRUCTURED);

    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test netcdf_free_dim_info with NULL */
TEST(netcdf_free_dim_info_null) {
    netcdf_free_dim_info(NULL, 0);  /* Should not crash */
    return 1;
}

/* Test reading multiple variables */
TEST(netcdf_multiple_variables) {
    const char *filename = create_test_netcdf_1d_structured(18, 9, 2);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Count variables */
    int var_count = 0;
    USVar *v = vars;
    while (v) {
        var_count++;
        v = v->next;
    }

    /* Should have at least one data variable */
    ASSERT_GE(var_count, 1);

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

/* Test USVar structure fields */
TEST(usvar_structure) {
    const char *filename = create_test_netcdf_1d_structured(10, 10, 2);
    ASSERT_NOT_NULL(filename);

    USFile *file = netcdf_open(filename);
    ASSERT_NOT_NULL(file);

    USMesh *mesh = mesh_create_from_netcdf(file->ncid, NULL);
    ASSERT_NOT_NULL(mesh);

    USVar *vars = netcdf_scan_variables(file, mesh);
    ASSERT_NOT_NULL(vars);

    /* Check USVar structure is properly populated */
    USVar *v = vars;
    while (v) {
        ASSERT_GT(strlen(v->name), 0);
        ASSERT_GT(v->n_dims, 0);
        ASSERT_EQ(v->file, file);
        ASSERT_TRUE(v->varid >= 0);

        /* Fill value should be set to something reasonable */
        /* global_min/max might not be set until range is estimated */

        v = v->next;
    }

    /* Variables are freed by netcdf_close() */
    mesh_free(mesh);
    netcdf_close(file);
    cleanup_test_file(filename);
    return 1;
}

RUN_TESTS("File NetCDF")
