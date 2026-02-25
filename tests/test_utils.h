/*
 * test_utils.h - Utility functions for tests
 *
 * Provides helper functions for creating test data and temporary files.
 */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netcdf.h>

/* Counter for unique filenames */
static int test_file_counter = 0;

/* Handle NetCDF errors */
#define NC_CHECK(status) do { \
    if ((status) != NC_NOERR) { \
        fprintf(stderr, "NetCDF error at %s:%d: %s\n", \
                __FILE__, __LINE__, nc_strerror(status)); \
        return NULL; \
    } \
} while(0)

#define NC_CHECK_VOID(status) do { \
    if ((status) != NC_NOERR) { \
        fprintf(stderr, "NetCDF error at %s:%d: %s\n", \
                __FILE__, __LINE__, nc_strerror(status)); \
        return; \
    } \
} while(0)

/*
 * Create a simple NetCDF test file with 1D structured coordinates.
 * Returns filename (static buffer) or NULL on error.
 */
static const char *create_test_netcdf_1d_structured(int nx, int ny, int nt) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_1d_%d_%d.nc", getpid(), test_file_counter++);
    /* Remove any existing file */
    unlink(filename);

    int ncid, lon_dimid, lat_dimid, time_dimid;
    int lon_varid, lat_varid, time_varid, data_varid;
    int dimids[3];
    int status;

    /* Create file */
    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    /* Define dimensions */
    status = nc_def_dim(ncid, "lon", nx, &lon_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "lat", ny, &lat_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "time", nt, &time_dimid);
    NC_CHECK(status);

    /* Define coordinate variables */
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
    status = nc_put_att_text(ncid, time_varid, "units", 22, "days since 2000-01-01");
    NC_CHECK(status);

    /* Define data variable */
    dimids[0] = time_dimid;
    dimids[1] = lat_dimid;
    dimids[2] = lon_dimid;
    status = nc_def_var(ncid, "temperature", NC_FLOAT, 3, dimids, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "units", 1, "K");
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "long_name", 11, "Temperature");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    /* Write coordinate data */
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
    for (int t = 0; t < nt; t++) time_vals[t] = t;

    /* Create test data pattern */
    for (int t = 0; t < nt; t++) {
        for (int j = 0; j < ny; j++) {
            for (int i = 0; i < nx; i++) {
                int idx = t * ny * nx + j * nx + i;
                data[idx] = 273.0f + (float)lat[j] * 0.5f + (float)t * 0.1f;
            }
        }
    }

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_double(ncid, time_varid, time_vals);
    nc_put_var_float(ncid, data_varid, data);

    free(lon);
    free(lat);
    free(time_vals);
    free(data);

    nc_close(ncid);
    return filename;
}

/*
 * Create a test NetCDF file with 2D curvilinear coordinates.
 */
static const char *create_test_netcdf_2d_curvilinear(int nx, int ny) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_2d_%d_%d.nc", getpid(), test_file_counter++);
    unlink(filename);

    int ncid, x_dimid, y_dimid;
    int lon_varid, lat_varid, data_varid;
    int dimids[2];
    int status;

    /* Create file */
    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    /* Define dimensions */
    status = nc_def_dim(ncid, "x", nx, &x_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "y", ny, &y_dimid);
    NC_CHECK(status);

    /* Define 2D coordinate variables */
    dimids[0] = y_dimid;
    dimids[1] = x_dimid;

    status = nc_def_var(ncid, "lon", NC_DOUBLE, 2, dimids, &lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lon_varid, "units", 12, "degrees_east");
    NC_CHECK(status);

    status = nc_def_var(ncid, "lat", NC_DOUBLE, 2, dimids, &lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lat_varid, "units", 13, "degrees_north");
    NC_CHECK(status);

    /* Define data variable */
    status = nc_def_var(ncid, "sst", NC_FLOAT, 2, dimids, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "long_name", 22, "Sea Surface Temperature");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    /* Write coordinate data - slight rotation for curvilinear effect */
    double *lon = malloc(ny * nx * sizeof(double));
    double *lat = malloc(ny * nx * sizeof(double));
    float *data = malloc(ny * nx * sizeof(float));

    if (!lon || !lat || !data) {
        free(lon); free(lat); free(data);
        nc_close(ncid);
        return NULL;
    }

    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            int idx = j * nx + i;
            double base_lon = -180.0 + 360.0 * i / nx;
            double base_lat = -90.0 + 180.0 * j / ny;

            /* Add small rotation for curvilinear effect */
            lon[idx] = base_lon + 0.1 * base_lat;
            lat[idx] = base_lat;
            data[idx] = 280.0f + 20.0f * (float)j / ny;
        }
    }

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_float(ncid, data_varid, data);

    free(lon);
    free(lat);
    free(data);

    nc_close(ncid);
    return filename;
}

/*
 * Create a test NetCDF file with unstructured (1D) coordinates.
 */
static const char *create_test_netcdf_unstructured(int n_nodes) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_unstruct_%d_%d.nc", getpid(), test_file_counter++);
    unlink(filename);

    int ncid, node_dimid;
    int lon_varid, lat_varid, data_varid;
    int status;

    /* Create file */
    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    /* Define dimensions */
    status = nc_def_dim(ncid, "nod2", n_nodes, &node_dimid);
    NC_CHECK(status);

    /* Define 1D coordinate variables (same size = unstructured) */
    status = nc_def_var(ncid, "lon", NC_DOUBLE, 1, &node_dimid, &lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lon_varid, "units", 12, "degrees_east");
    NC_CHECK(status);

    status = nc_def_var(ncid, "lat", NC_DOUBLE, 1, &node_dimid, &lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lat_varid, "units", 13, "degrees_north");
    NC_CHECK(status);

    /* Define data variable */
    status = nc_def_var(ncid, "ssh", NC_FLOAT, 1, &node_dimid, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "long_name", 18, "Sea Surface Height");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    /* Write coordinate data - random-ish distribution */
    double *lon = malloc(n_nodes * sizeof(double));
    double *lat = malloc(n_nodes * sizeof(double));
    float *data = malloc(n_nodes * sizeof(float));

    if (!lon || !lat || !data) {
        free(lon); free(lat); free(data);
        nc_close(ncid);
        return NULL;
    }

    /* Create pseudo-random but reproducible coordinates */
    for (int i = 0; i < n_nodes; i++) {
        /* Simple LCG for reproducibility */
        unsigned int seed = (unsigned int)i * 1103515245U + 12345U;
        lon[i] = -180.0 + 360.0 * (seed % 10000) / 10000.0;
        seed = seed * 1103515245U + 12345U;
        lat[i] = -90.0 + 180.0 * (seed % 10000) / 10000.0;
        data[i] = (float)lat[i] * 0.01f;  /* SSH correlates with latitude */
    }

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_float(ncid, data_varid, data);

    free(lon);
    free(lat);
    free(data);

    nc_close(ncid);
    return filename;
}

/*
 * Create a NetCDF file with 3D data (time, depth, nodes).
 */
static const char *create_test_netcdf_3d(int nt, int nz, int n_nodes) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_3d_%d_%d.nc", getpid(), test_file_counter++);
    unlink(filename);

    int ncid, time_dimid, depth_dimid, node_dimid;
    int lon_varid, lat_varid, time_varid, depth_varid, data_varid;
    int dimids[3];
    int status;

    /* Create file */
    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    /* Define dimensions */
    status = nc_def_dim(ncid, "time", nt, &time_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "depth", nz, &depth_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "nod2", n_nodes, &node_dimid);
    NC_CHECK(status);

    /* Coordinate variables */
    status = nc_def_var(ncid, "lon", NC_DOUBLE, 1, &node_dimid, &lon_varid);
    NC_CHECK(status);
    status = nc_def_var(ncid, "lat", NC_DOUBLE, 1, &node_dimid, &lat_varid);
    NC_CHECK(status);
    status = nc_def_var(ncid, "time", NC_DOUBLE, 1, &time_dimid, &time_varid);
    NC_CHECK(status);
    status = nc_def_var(ncid, "depth", NC_DOUBLE, 1, &depth_dimid, &depth_varid);
    NC_CHECK(status);

    /* 3D data variable */
    dimids[0] = time_dimid;
    dimids[1] = depth_dimid;
    dimids[2] = node_dimid;
    status = nc_def_var(ncid, "temp", NC_FLOAT, 3, dimids, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "long_name", 11, "Temperature");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    /* Allocate and write data */
    double *lon = malloc(n_nodes * sizeof(double));
    double *lat = malloc(n_nodes * sizeof(double));
    double *time_vals = malloc(nt * sizeof(double));
    double *depth_vals = malloc(nz * sizeof(double));
    float *data = malloc(nt * nz * n_nodes * sizeof(float));

    if (!lon || !lat || !time_vals || !depth_vals || !data) {
        free(lon); free(lat); free(time_vals); free(depth_vals); free(data);
        nc_close(ncid);
        return NULL;
    }

    for (int i = 0; i < n_nodes; i++) {
        unsigned int seed = (unsigned int)i * 1103515245U + 12345U;
        lon[i] = -180.0 + 360.0 * (seed % 10000) / 10000.0;
        seed = seed * 1103515245U + 12345U;
        lat[i] = -90.0 + 180.0 * (seed % 10000) / 10000.0;
    }

    for (int t = 0; t < nt; t++) time_vals[t] = t * 24.0;  /* Hourly */
    for (int z = 0; z < nz; z++) depth_vals[z] = z * 100.0;  /* 100m levels */

    for (int t = 0; t < nt; t++) {
        for (int z = 0; z < nz; z++) {
            for (int n = 0; n < n_nodes; n++) {
                int idx = t * nz * n_nodes + z * n_nodes + n;
                data[idx] = 273.0f + (float)lat[n] * 0.5f - (float)z * 0.1f;
            }
        }
    }

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_double(ncid, time_varid, time_vals);
    nc_put_var_double(ncid, depth_varid, depth_vals);
    nc_put_var_float(ncid, data_varid, data);

    free(lon);
    free(lat);
    free(time_vals);
    free(depth_vals);
    free(data);

    nc_close(ncid);
    return filename;
}

/*
 * Create a test NetCDF file in MPAS UGRID style (face_lon/face_lat on n_face).
 * Includes node coordinates and face_node_connectivity for completeness.
 */
static const char *create_test_netcdf_mpas_ugrid(int n_faces, int n_nodes) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_mpas_ugrid_%d_%d.nc", getpid(), test_file_counter++);
    unlink(filename);

    int ncid, face_dimid, node_dimid;
    int face_lon_varid, face_lat_varid, node_lon_varid, node_lat_varid;
    int data_varid, data2_varid;
    int status;

    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    /* Define dimensions */
    status = nc_def_dim(ncid, "n_face", n_faces, &face_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "n_node", n_nodes, &node_dimid);
    NC_CHECK(status);

    /* Face coordinate variables */
    status = nc_def_var(ncid, "face_lon", NC_FLOAT, 1, &face_dimid, &face_lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, face_lon_varid, "standard_name", 9, "longitude");
    NC_CHECK(status);
    status = nc_put_att_text(ncid, face_lon_varid, "units", 12, "degrees_east");
    NC_CHECK(status);

    status = nc_def_var(ncid, "face_lat", NC_FLOAT, 1, &face_dimid, &face_lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, face_lat_varid, "standard_name", 8, "latitude");
    NC_CHECK(status);
    status = nc_put_att_text(ncid, face_lat_varid, "units", 13, "degrees_north");
    NC_CHECK(status);

    /* Node coordinate variables */
    status = nc_def_var(ncid, "node_lon", NC_FLOAT, 1, &node_dimid, &node_lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, node_lon_varid, "units", 12, "degrees_east");
    NC_CHECK(status);

    status = nc_def_var(ncid, "node_lat", NC_FLOAT, 1, &node_dimid, &node_lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, node_lat_varid, "units", 13, "degrees_north");
    NC_CHECK(status);

    /* Data variables on faces */
    status = nc_def_var(ncid, "gaussian", NC_FLOAT, 1, &face_dimid, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "long_name", 8, "Gaussian");
    NC_CHECK(status);

    status = nc_def_var(ncid, "vorticity", NC_FLOAT, 1, &face_dimid, &data2_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data2_varid, "long_name", 9, "Vorticity");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    /* Write coordinate data */
    float *face_lon = malloc(n_faces * sizeof(float));
    float *face_lat = malloc(n_faces * sizeof(float));
    float *node_lon = malloc(n_nodes * sizeof(float));
    float *node_lat = malloc(n_nodes * sizeof(float));
    float *data = malloc(n_faces * sizeof(float));
    float *data2 = malloc(n_faces * sizeof(float));

    if (!face_lon || !face_lat || !node_lon || !node_lat || !data || !data2) {
        free(face_lon); free(face_lat); free(node_lon); free(node_lat);
        free(data); free(data2);
        nc_close(ncid);
        return NULL;
    }

    for (int i = 0; i < n_faces; i++) {
        unsigned int seed = (unsigned int)i * 1103515245U + 12345U;
        face_lon[i] = -180.0f + 360.0f * (float)(seed % 10000) / 10000.0f;
        seed = seed * 1103515245U + 12345U;
        face_lat[i] = -90.0f + 180.0f * (float)(seed % 10000) / 10000.0f;
        data[i] = 0.1f + 0.9f * (float)(i % 100) / 100.0f;
        data2[i] = -1.0f + 2.0f * (float)i / (float)n_faces;
    }

    for (int i = 0; i < n_nodes; i++) {
        unsigned int seed = (unsigned int)(i + n_faces) * 1103515245U + 12345U;
        node_lon[i] = -180.0f + 360.0f * (float)(seed % 10000) / 10000.0f;
        seed = seed * 1103515245U + 12345U;
        node_lat[i] = -90.0f + 180.0f * (float)(seed % 10000) / 10000.0f;
    }

    nc_put_var_float(ncid, face_lon_varid, face_lon);
    nc_put_var_float(ncid, face_lat_varid, face_lat);
    nc_put_var_float(ncid, node_lon_varid, node_lon);
    nc_put_var_float(ncid, node_lat_varid, node_lat);
    nc_put_var_float(ncid, data_varid, data);
    nc_put_var_float(ncid, data2_varid, data2);

    free(face_lon); free(face_lat); free(node_lon); free(node_lat);
    free(data); free(data2);

    nc_close(ncid);
    return filename;
}

/*
 * Create a test NetCDF file in native MPAS style (lonCell/latCell on nCells, in radians).
 */
static const char *create_test_netcdf_mpas_native(int n_cells) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_mpas_native_%d_%d.nc", getpid(), test_file_counter++);
    unlink(filename);

    int ncid, cell_dimid;
    int lon_varid, lat_varid, data_varid;
    int status;

    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    status = nc_def_dim(ncid, "nCells", n_cells, &cell_dimid);
    NC_CHECK(status);

    status = nc_def_var(ncid, "lonCell", NC_DOUBLE, 1, &cell_dimid, &lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lon_varid, "units", 7, "radians");
    NC_CHECK(status);

    status = nc_def_var(ncid, "latCell", NC_DOUBLE, 1, &cell_dimid, &lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lat_varid, "units", 7, "radians");
    NC_CHECK(status);

    status = nc_def_var(ncid, "temperature", NC_FLOAT, 1, &cell_dimid, &data_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "long_name", 11, "Temperature");
    NC_CHECK(status);
    status = nc_put_att_text(ncid, data_varid, "units", 1, "K");
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    double *lon = malloc(n_cells * sizeof(double));
    double *lat = malloc(n_cells * sizeof(double));
    float *data = malloc(n_cells * sizeof(float));

    if (!lon || !lat || !data) {
        free(lon); free(lat); free(data);
        nc_close(ncid);
        return NULL;
    }

    /* Coordinates in radians */
    for (int i = 0; i < n_cells; i++) {
        unsigned int seed = (unsigned int)i * 1103515245U + 12345U;
        lon[i] = -3.14159265 + 6.28318530 * (double)(seed % 10000) / 10000.0;
        seed = seed * 1103515245U + 12345U;
        lat[i] = -1.57079632 + 3.14159265 * (double)(seed % 10000) / 10000.0;
        data[i] = 250.0f + 50.0f * (float)i / (float)n_cells;
    }

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_float(ncid, data_varid, data);

    free(lon); free(lat); free(data);

    nc_close(ncid);
    return filename;
}

/*
 * Create a test NetCDF file in native MPAS style with cellsOnVertex connectivity.
 * Builds a simple icosahedron-like mesh: n_cells cells with n_vertices Voronoi
 * vertices, each connecting 3 cells (vertexDegree=3).
 * Some boundary vertices reference cell 0 (invalid in 1-based) to test filtering.
 */
static const char *create_test_netcdf_mpas_with_connectivity(
    int n_cells, int n_vertices, int n_boundary) {
    static char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/test_ushow_mpas_conn_%d_%d.nc",
             getpid(), test_file_counter++);
    unlink(filename);

    int ncid, cell_dimid, vert_dimid, vdeg_dimid;
    int lon_varid, lat_varid, data_varid, cov_varid;
    int dimids[2];
    int status;

    status = nc_create(filename, NC_NETCDF4, &ncid);
    NC_CHECK(status);

    /* Dimensions */
    status = nc_def_dim(ncid, "nCells", n_cells, &cell_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "nVertices", n_vertices, &vert_dimid);
    NC_CHECK(status);
    status = nc_def_dim(ncid, "vertexDegree", 3, &vdeg_dimid);
    NC_CHECK(status);

    /* Cell center coordinates (radians) */
    status = nc_def_var(ncid, "lonCell", NC_DOUBLE, 1, &cell_dimid, &lon_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lon_varid, "units", 7, "radians");
    NC_CHECK(status);

    status = nc_def_var(ncid, "latCell", NC_DOUBLE, 1, &cell_dimid, &lat_varid);
    NC_CHECK(status);
    status = nc_put_att_text(ncid, lat_varid, "units", 7, "radians");
    NC_CHECK(status);

    /* cellsOnVertex(nVertices, vertexDegree) — 1-based indices */
    dimids[0] = vert_dimid;
    dimids[1] = vdeg_dimid;
    status = nc_def_var(ncid, "cellsOnVertex", NC_INT, 2, dimids, &cov_varid);
    NC_CHECK(status);

    /* Data variable */
    status = nc_def_var(ncid, "temperature", NC_FLOAT, 1, &cell_dimid, &data_varid);
    NC_CHECK(status);

    status = nc_enddef(ncid);
    NC_CHECK(status);

    /* Write coordinates */
    double *lon = malloc(n_cells * sizeof(double));
    double *lat = malloc(n_cells * sizeof(double));
    float *data = malloc(n_cells * sizeof(float));
    int *cov = malloc(n_vertices * 3 * sizeof(int));

    if (!lon || !lat || !data || !cov) {
        free(lon); free(lat); free(data); free(cov);
        nc_close(ncid);
        return NULL;
    }

    for (int i = 0; i < n_cells; i++) {
        unsigned int seed = (unsigned int)i * 1103515245U + 12345U;
        lon[i] = -3.14159265 + 6.28318530 * (double)(seed % 10000) / 10000.0;
        seed = seed * 1103515245U + 12345U;
        lat[i] = -1.57079632 + 3.14159265 * (double)(seed % 10000) / 10000.0;
        data[i] = 273.0f + (float)i;
    }

    /* Build cellsOnVertex: interior vertices reference valid cells (1-based),
     * boundary vertices reference cell 0 (invalid) */
    for (int v = 0; v < n_vertices; v++) {
        if (v < n_boundary) {
            /* Boundary: one index is 0 (invalid) */
            cov[v * 3 + 0] = 0;
            cov[v * 3 + 1] = (v % n_cells) + 1;
            cov[v * 3 + 2] = ((v + 1) % n_cells) + 1;
        } else {
            /* Interior: all valid, 1-based */
            cov[v * 3 + 0] = (v % n_cells) + 1;
            cov[v * 3 + 1] = ((v + 1) % n_cells) + 1;
            cov[v * 3 + 2] = ((v + 2) % n_cells) + 1;
        }
    }

    nc_put_var_double(ncid, lon_varid, lon);
    nc_put_var_double(ncid, lat_varid, lat);
    nc_put_var_float(ncid, data_varid, data);
    nc_put_var_int(ncid, cov_varid, cov);

    free(lon); free(lat); free(data); free(cov);

    nc_close(ncid);
    return filename;
}

/*
 * Remove a test file.
 */
static void cleanup_test_file(const char *filename) {
    if (filename) {
        unlink(filename);
    }
}

#endif /* TEST_UTILS_H */
