/*
 * mesh.c - Mesh and coordinate handling
 */

#include "mesh.h"
#include <netcdf.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdio.h>

/* Common coordinate variable names to search for */
static const char *LON_NAMES[] = {
    "lon", "longitude", "x", "nav_lon", "glon",
    "xt_ocean", "xu_ocean", "xh", "xq", NULL
};

static const char *LAT_NAMES[] = {
    "lat", "latitude", "y", "nav_lat", "glat",
    "yt_ocean", "yu_ocean", "yh", "yq", NULL
};

void lonlat_to_cartesian(double lon_deg, double lat_deg,
                         double *x, double *y, double *z) {
    double lon_rad = lon_deg * DEG2RAD;
    double lat_rad = lat_deg * DEG2RAD;
    double cos_lat = cos(lat_rad);

    *x = cos_lat * cos(lon_rad);
    *y = cos_lat * sin(lon_rad);
    *z = sin(lat_rad);
}

void lonlat_to_cartesian_batch(const double *lon, const double *lat,
                               double *xyz, size_t n_points) {
    for (size_t i = 0; i < n_points; i++) {
        double lon_rad = lon[i] * DEG2RAD;
        double lat_rad = lat[i] * DEG2RAD;
        double cos_lat = cos(lat_rad);

        xyz[i * 3 + 0] = cos_lat * cos(lon_rad);
        xyz[i * 3 + 1] = cos_lat * sin(lon_rad);
        xyz[i * 3 + 2] = sin(lat_rad);
    }
}

double meters_to_chord(double meters) {
    double arc_radians = meters / EARTH_RADIUS_M;
    return 2.0 * sin(arc_radians / 2.0);
}

USMesh *mesh_create(double *lon, double *lat, size_t n_points, CoordType type) {
    USMesh *mesh = calloc(1, sizeof(USMesh));
    if (!mesh) return NULL;

    mesh->n_points = n_points;
    mesh->lon = lon;
    mesh->lat = lat;
    mesh->coord_type = type;

    /* Convert to Cartesian */
    mesh->xyz = malloc(n_points * 3 * sizeof(double));
    if (!mesh->xyz) {
        free(mesh);
        return NULL;
    }

    lonlat_to_cartesian_batch(lon, lat, mesh->xyz, n_points);

    return mesh;
}

/* Coordinate variable info */
typedef struct {
    int varid;
    int ndims;
    size_t dims[2];  /* dims[0] = first dim size, dims[1] = second (if 2D) */
    size_t total_size;
} CoordInfo;

/* Find a coordinate variable by trying common names */
static int find_coord_var(int ncid, const char **names, CoordInfo *info) {
    int status;

    for (int i = 0; names[i] != NULL; i++) {
        status = nc_inq_varid(ncid, names[i], &info->varid);
        if (status == NC_NOERR) {
            nc_inq_varndims(ncid, info->varid, &info->ndims);

            if (info->ndims == 1) {
                int dimid;
                nc_inq_vardimid(ncid, info->varid, &dimid);
                nc_inq_dimlen(ncid, dimid, &info->dims[0]);
                info->dims[1] = 0;
                info->total_size = info->dims[0];
                return 0;
            } else if (info->ndims == 2) {
                int dimids[2];
                nc_inq_vardimid(ncid, info->varid, dimids);
                nc_inq_dimlen(ncid, dimids[0], &info->dims[0]);
                nc_inq_dimlen(ncid, dimids[1], &info->dims[1]);
                info->total_size = info->dims[0] * info->dims[1];
                return 0;
            }
        }
    }

    return -1;
}

USMesh *mesh_create_from_netcdf(int data_ncid, const char *mesh_filename) {
    int mesh_ncid;
    int status;
    CoordInfo lon_info, lat_info;

    /* Open mesh file if provided, otherwise use data file */
    if (mesh_filename && mesh_filename[0]) {
        status = nc_open(mesh_filename, NC_NOWRITE, &mesh_ncid);
        if (status != NC_NOERR) {
            fprintf(stderr, "Error opening mesh file %s: %s\n",
                    mesh_filename, nc_strerror(status));
            return NULL;
        }
    } else {
        mesh_ncid = data_ncid;
    }

    /* Find longitude variable */
    if (find_coord_var(mesh_ncid, LON_NAMES, &lon_info) != 0) {
        fprintf(stderr, "Could not find longitude coordinate variable\n");
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    /* Find latitude variable */
    if (find_coord_var(mesh_ncid, LAT_NAMES, &lat_info) != 0) {
        fprintf(stderr, "Could not find latitude coordinate variable\n");
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    printf("Coordinate info: lon %dD [%zu", lon_info.ndims, lon_info.dims[0]);
    if (lon_info.ndims == 2) printf("x%zu", lon_info.dims[1]);
    printf("], lat %dD [%zu", lat_info.ndims, lat_info.dims[0]);
    if (lat_info.ndims == 2) printf("x%zu", lat_info.dims[1]);
    printf("]\n");

    size_t n_points;
    double *lon = NULL, *lat = NULL;
    CoordType coord_type;
    size_t orig_nx = 0, orig_ny = 0;

    /* Check dimension names to distinguish structured from unstructured */
    int lon_dim_id, lat_dim_id;
    char lon_dimname[MAX_NAME_LEN] = "", lat_dimname[MAX_NAME_LEN] = "";
    if (lon_info.ndims >= 1) {
        nc_inq_vardimid(mesh_ncid, lon_info.varid, &lon_dim_id);
        nc_inq_dimname(mesh_ncid, lon_dim_id, lon_dimname);
    }
    if (lat_info.ndims >= 1) {
        nc_inq_vardimid(mesh_ncid, lat_info.varid, &lat_dim_id);
        nc_inq_dimname(mesh_ncid, lat_dim_id, lat_dimname);
    }

    /* Check if dimension names suggest unstructured (node-like) coordinates */
    static const char *NODE_DIM_NAMES[] = {
        "nod2", "nod2d", "node", "nodes", "ncells", "npoints", "nod", "n2d",
        "cell", "cells", "elem", "vertex", "vertices", NULL
    };
    int lon_is_node_dim = 0, lat_is_node_dim = 0;
    for (int i = 0; NODE_DIM_NAMES[i] != NULL; i++) {
        if (strcasecmp(lon_dimname, NODE_DIM_NAMES[i]) == 0) lon_is_node_dim = 1;
        if (strcasecmp(lat_dimname, NODE_DIM_NAMES[i]) == 0) lat_is_node_dim = 1;
    }

    /* Determine coordinate type and load accordingly */
    if (lon_info.ndims == 1 && lat_info.ndims == 1) {
        /* If both use node-like dimension names and same size, it's unstructured */
        int is_unstructured = (lon_info.total_size == lat_info.total_size) &&
                              (lon_is_node_dim || lat_is_node_dim ||
                               lon_dim_id == lat_dim_id);  /* Same dimension = unstructured */

        if (is_unstructured) {
            /* Same size 1D arrays with node-like dims -> unstructured */
            coord_type = COORD_TYPE_1D_UNSTRUCTURED;
            n_points = lon_info.total_size;
            printf("Detected: 1D unstructured coordinates (%zu points)\n", n_points);

            lon = malloc(n_points * sizeof(double));
            lat = malloc(n_points * sizeof(double));
            if (!lon || !lat) goto error;

            nc_get_var_double(mesh_ncid, lon_info.varid, lon);
            nc_get_var_double(mesh_ncid, lat_info.varid, lat);
        } else {
            /* Different size 1D arrays -> structured grid, create meshgrid */
            coord_type = COORD_TYPE_1D_STRUCTURED;
            orig_nx = lon_info.total_size;
            orig_ny = lat_info.total_size;
            n_points = orig_nx * orig_ny;
            printf("Detected: 1D structured grid (%zu x %zu = %zu points)\n",
                   orig_nx, orig_ny, n_points);

            /* Read 1D coordinate arrays */
            double *lon_1d = malloc(orig_nx * sizeof(double));
            double *lat_1d = malloc(orig_ny * sizeof(double));
            if (!lon_1d || !lat_1d) {
                free(lon_1d);
                free(lat_1d);
                goto error;
            }

            nc_get_var_double(mesh_ncid, lon_info.varid, lon_1d);
            nc_get_var_double(mesh_ncid, lat_info.varid, lat_1d);

            /* Create meshgrid (flatten in row-major order: lat varies slowest) */
            lon = malloc(n_points * sizeof(double));
            lat = malloc(n_points * sizeof(double));
            if (!lon || !lat) {
                free(lon_1d);
                free(lat_1d);
                goto error;
            }

            size_t idx = 0;
            for (size_t j = 0; j < orig_ny; j++) {
                for (size_t i = 0; i < orig_nx; i++) {
                    lon[idx] = lon_1d[i];
                    lat[idx] = lat_1d[j];
                    idx++;
                }
            }

            free(lon_1d);
            free(lat_1d);
        }
    } else if (lon_info.ndims == 2 && lat_info.ndims == 2) {
        /* Both 2D -> curvilinear grid, flatten */
        if (lon_info.dims[0] != lat_info.dims[0] || lon_info.dims[1] != lat_info.dims[1]) {
            fprintf(stderr, "2D coordinate arrays have different shapes\n");
            goto error;
        }

        coord_type = COORD_TYPE_2D_CURVILINEAR;
        orig_ny = lon_info.dims[0];
        orig_nx = lon_info.dims[1];
        n_points = orig_nx * orig_ny;
        printf("Detected: 2D curvilinear grid (%zu x %zu = %zu points)\n",
               orig_ny, orig_nx, n_points);

        lon = malloc(n_points * sizeof(double));
        lat = malloc(n_points * sizeof(double));
        if (!lon || !lat) goto error;

        nc_get_var_double(mesh_ncid, lon_info.varid, lon);
        nc_get_var_double(mesh_ncid, lat_info.varid, lat);
    } else {
        fprintf(stderr, "Unsupported coordinate combination: lon %dD, lat %dD\n",
                lon_info.ndims, lat_info.ndims);
        goto error;
    }

    /* Close mesh file if it was opened separately */
    if (mesh_filename && mesh_filename[0]) {
        nc_close(mesh_ncid);
    }

    /* Normalize longitude to [-180, 180] */
    for (size_t i = 0; i < n_points; i++) {
        while (lon[i] > 180.0) lon[i] -= 360.0;
        while (lon[i] < -180.0) lon[i] += 360.0;
    }

    /* Create mesh structure */
    USMesh *mesh = mesh_create(lon, lat, n_points, coord_type);
    if (!mesh) {
        free(lon);
        free(lat);
        return NULL;
    }

    /* Store original grid dimensions for structured grids */
    mesh->orig_nx = orig_nx;
    mesh->orig_ny = orig_ny;

    if (mesh_filename && mesh_filename[0]) {
        mesh->mesh_filename = strdup(mesh_filename);
        mesh->mesh_loaded = 1;
    }

    return mesh;

error:
    free(lon);
    free(lat);
    if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
    return NULL;
}

void mesh_free(USMesh *mesh) {
    if (!mesh) return;
    free(mesh->lon);
    free(mesh->lat);
    free(mesh->xyz);
    free(mesh->mesh_filename);
    free(mesh->lon_varname);
    free(mesh->lat_varname);
    free(mesh);
}
