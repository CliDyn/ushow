/*
 * mesh.c - Mesh and coordinate handling
 */

#include "mesh.h"
#include <netcdf.h>
#include <stdlib.h>
#include <string.h>
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

/* Find a coordinate variable by trying common names */
static int find_coord_var(int ncid, const char **names, int *varid, size_t *size) {
    int status;

    for (int i = 0; names[i] != NULL; i++) {
        status = nc_inq_varid(ncid, names[i], varid);
        if (status == NC_NOERR) {
            /* Get dimension info */
            int ndims;
            nc_inq_varndims(ncid, *varid, &ndims);

            if (ndims == 1) {
                int dimid;
                nc_inq_vardimid(ncid, *varid, &dimid);
                nc_inq_dimlen(ncid, dimid, size);
                return 0;
            } else if (ndims == 2) {
                /* 2D coordinate - get total size */
                int dimids[2];
                size_t dim0, dim1;
                nc_inq_vardimid(ncid, *varid, dimids);
                nc_inq_dimlen(ncid, dimids[0], &dim0);
                nc_inq_dimlen(ncid, dimids[1], &dim1);
                *size = dim0 * dim1;
                return 0;
            }
        }
    }

    return -1;
}

USMesh *mesh_create_from_netcdf(int data_ncid, const char *mesh_filename) {
    int mesh_ncid;
    int status;
    int lon_varid, lat_varid;
    size_t lon_size, lat_size;

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
    if (find_coord_var(mesh_ncid, LON_NAMES, &lon_varid, &lon_size) != 0) {
        fprintf(stderr, "Could not find longitude coordinate variable\n");
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    /* Find latitude variable */
    if (find_coord_var(mesh_ncid, LAT_NAMES, &lat_varid, &lat_size) != 0) {
        fprintf(stderr, "Could not find latitude coordinate variable\n");
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    /* Check sizes match */
    if (lon_size != lat_size) {
        fprintf(stderr, "Longitude and latitude arrays have different sizes (%zu vs %zu)\n",
                lon_size, lat_size);
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    size_t n_points = lon_size;
    printf("Loading mesh with %zu points\n", n_points);

    /* Allocate and read coordinates */
    double *lon = malloc(n_points * sizeof(double));
    double *lat = malloc(n_points * sizeof(double));
    if (!lon || !lat) {
        free(lon);
        free(lat);
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    status = nc_get_var_double(mesh_ncid, lon_varid, lon);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error reading longitude: %s\n", nc_strerror(status));
        free(lon);
        free(lat);
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
    }

    status = nc_get_var_double(mesh_ncid, lat_varid, lat);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error reading latitude: %s\n", nc_strerror(status));
        free(lon);
        free(lat);
        if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
        return NULL;
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
    USMesh *mesh = mesh_create(lon, lat, n_points, COORD_TYPE_1D_UNSTRUCTURED);
    if (!mesh) {
        free(lon);
        free(lat);
        return NULL;
    }

    if (mesh_filename && mesh_filename[0]) {
        mesh->mesh_filename = strdup(mesh_filename);
        mesh->mesh_loaded = 1;
    }

    return mesh;
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
