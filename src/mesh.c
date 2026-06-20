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
#include <sys/stat.h>
#include <limits.h>

#ifdef HAVE_ZARR
#include "cJSON/cJSON.h"
#include <lz4.h>
#include <blosc.h>
#endif

/* Common coordinate variable names to search for */
static const char *LON_NAMES[] = {
  "lon", "longitude", "x", "nav_lon", "glon", "clon",
    "xt_ocean", "xu_ocean", "xh", "xq",
    "face_lon", "node_lon", "edge_lon",
    "lonCell", "lonVertex", "lonEdge", NULL
};

static const char *LAT_NAMES[] = {
  "lat", "latitude", "y", "nav_lat", "glat", "clat",
    "yt_ocean", "yu_ocean", "yh", "yq",
    "face_lat", "node_lat", "edge_lat",
    "latCell", "latVertex", "latEdge", NULL
};

/* Check if units string indicates radians */
static int is_radian_units(const char *units) {
    if (!units) return 0;
    return (strcasecmp(units, "rad") == 0 || 
            strcasecmp(units, "radian") == 0 || 
            strcasecmp(units, "radians") == 0);
}

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

/* Forward declaration */
static int load_element_connectivity(USMesh *mesh, int ncid);

/* Coordinate variable info */
typedef struct {
    int varid;
    int ndims;
    size_t dims[2];  /* dims[0] = first dim size, dims[1] = second (if 2D) */
    size_t total_size;
    char units[32];  /* units attribute (e.g., "degrees", "radians") */
} CoordInfo;

/* Read units attribute from a NetCDF variable */
static void read_units_attribute(int ncid, int varid, char *units, size_t units_size) {
    /* Default to "degrees" */
    strncpy(units, "degrees", units_size - 1);
    units[units_size - 1] = '\0';
    
    size_t units_len;
    int status = nc_inq_attlen(ncid, varid, "units", &units_len);
    if (status == NC_NOERR) {
        if (units_len < units_size) {
            status = nc_get_att_text(ncid, varid, "units", units);
            if (status == NC_NOERR) {
                units[units_len] = '\0';  /* Null-terminate */
            }
        } else {
            /* Read and print the oversized units attribute */
            char *units_buf = malloc(units_len + 1);
            if (units_buf) {
                status = nc_get_att_text(ncid, varid, "units", units_buf);
                if (status == NC_NOERR) {
                    units_buf[units_len] = '\0';
                    fprintf(stderr, "Warning: units attribute too long (%zu chars): '%s', using default 'degrees'\n", 
                            units_len, units_buf);
                }
                free(units_buf);
            } else {
                fprintf(stderr, "Warning: units attribute too long (%zu chars), using default 'degrees'\n", units_len);
            }
        }
    }
}

/* Find a coordinate variable by trying common names */
static int find_coord_var(int ncid, const char **names, CoordInfo *info) {
    int status;

    for (int i = 0; names[i] != NULL; i++) {
        status = nc_inq_varid(ncid, names[i], &info->varid);
        if (status == NC_NOERR) {
            nc_inq_varndims(ncid, info->varid, &info->ndims);

            read_units_attribute(ncid, info->varid, info->units, sizeof(info->units));

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

/* FESOM2 mesh.diag.nc (and some other unstructured meshes) store the node
 * coordinates in a single variable holding both lon and lat, e.g.
 *   double nodes(n2, nod_n) ;   // nodes[0,:] = lon, nodes[1,:] = lat
 * typically in RADIANS and WITHOUT a units attribute, so the name-based
 * lon/lat search misses them entirely. This is the fallback for that layout.
 * Returns a fully built USMesh, or NULL if no such variable is present. */
static USMesh *mesh_create_from_combined_nodes(int ncid, const char *mesh_filename) {
    static const char *COMBINED_NAMES[] = {
        "nodes", "coord_nod2D", "coord_nod2d", "node_coords", NULL
    };

    int varid = -1;
    const char *matched_name = NULL;
    for (int i = 0; COMBINED_NAMES[i] != NULL; i++) {
        if (nc_inq_varid(ncid, COMBINED_NAMES[i], &varid) == NC_NOERR) {
            matched_name = COMBINED_NAMES[i];
            break;
        }
        varid = -1;
    }
    if (varid < 0) return NULL;

    int ndims = 0;
    nc_inq_varndims(ncid, varid, &ndims);
    if (ndims != 2) return NULL;

    int dimids[2];
    size_t d[2];
    nc_inq_vardimid(ncid, varid, dimids);
    nc_inq_dimlen(ncid, dimids[0], &d[0]);
    nc_inq_dimlen(ncid, dimids[1], &d[1]);

    /* One axis must be the 2-component (lon,lat) axis. */
    size_t n_points;
    int comp_first;                 /* 1: shape (2, n); 0: shape (n, 2) */
    if (d[0] == 2)      { comp_first = 1; n_points = d[1]; }
    else if (d[1] == 2) { comp_first = 0; n_points = d[0]; }
    else return NULL;
    if (n_points == 0) return NULL;

    double *raw = malloc((size_t)2 * n_points * sizeof(double));
    double *lon = malloc(n_points * sizeof(double));
    double *lat = malloc(n_points * sizeof(double));
    if (!raw || !lon || !lat) { free(raw); free(lon); free(lat); return NULL; }

    if (nc_get_var_double(ncid, varid, raw) != NC_NOERR) {
        free(raw); free(lon); free(lat);
        return NULL;
    }

    if (comp_first) {
        /* (2, n) C-order: first n values are lon, next n are lat */
        for (size_t i = 0; i < n_points; i++) {
            lon[i] = raw[i];
            lat[i] = raw[n_points + i];
        }
    } else {
        /* (n, 2) C-order: lon,lat interleaved per node */
        for (size_t i = 0; i < n_points; i++) {
            lon[i] = raw[2 * i];
            lat[i] = raw[2 * i + 1];
        }
    }
    free(raw);

    /* Convert radians -> degrees. Honor an explicit units attribute; otherwise
     * autodetect by range (radian lat <= ~pi/2, degree lat up to 90). */
    char units[32];
    read_units_attribute(ncid, varid, units, sizeof(units));
    int radians;
    if (is_radian_units(units)) {
        radians = 1;
    } else {
        double max_abs_lon = 0.0, max_abs_lat = 0.0;
        for (size_t i = 0; i < n_points; i++) {
            double ao = fabs(lon[i]), al = fabs(lat[i]);
            if (ao > max_abs_lon) max_abs_lon = ao;
            if (al > max_abs_lat) max_abs_lat = al;
        }
        radians = (max_abs_lat <= M_PI / 2.0 + 1e-6) &&
                  (max_abs_lon <= 2.0 * M_PI + 1e-6);
    }
    if (radians) {
        for (size_t i = 0; i < n_points; i++) {
            lon[i] *= RAD2DEG;
            lat[i] *= RAD2DEG;
        }
    }

    /* Normalize longitude to [-180, 180] */
    for (size_t i = 0; i < n_points; i++) {
        while (lon[i] > 180.0)  lon[i] -= 360.0;
        while (lon[i] < -180.0) lon[i] += 360.0;
    }

    printf("Detected: combined node coordinates '%s' (%zu points, %s)\n",
           matched_name, n_points, radians ? "radians->degrees" : "degrees");

    USMesh *mesh = mesh_create(lon, lat, n_points, COORD_TYPE_1D_UNSTRUCTURED);
    if (!mesh) { free(lon); free(lat); return NULL; }

    if (mesh_filename && mesh_filename[0]) {
        mesh->mesh_filename = strdup(mesh_filename);
        mesh->mesh_loaded = 1;
    }
    return mesh;
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
        /* Fallback: meshes that store lon+lat in a single combined variable
         * (e.g. FESOM2 mesh.diag.nc "nodes"(n2, nod_n)) are not matched by the
         * name-based search above. */
        USMesh *combined = mesh_create_from_combined_nodes(mesh_ncid, mesh_filename);
        if (combined) {
            if (mesh_filename && mesh_filename[0]) nc_close(mesh_ncid);
            return combined;
        }
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
    int lon_dimids[NC_MAX_VAR_DIMS], lat_dimids[NC_MAX_VAR_DIMS];
    int lon_dim_id = -1, lat_dim_id = -1;
    char lon_dimname[MAX_NAME_LEN] = "", lat_dimname[MAX_NAME_LEN] = "";
    if (lon_info.ndims >= 1) {
        nc_inq_vardimid(mesh_ncid, lon_info.varid, lon_dimids);
        lon_dim_id = lon_dimids[0];
        nc_inq_dimname(mesh_ncid, lon_dim_id, lon_dimname);
    }
    if (lat_info.ndims >= 1) {
        nc_inq_vardimid(mesh_ncid, lat_info.varid, lat_dimids);
        lat_dim_id = lat_dimids[0];
        nc_inq_dimname(mesh_ncid, lat_dim_id, lat_dimname);
    }

    /* Check if dimension names suggest unstructured (node-like) coordinates */
    static const char *NODE_DIM_NAMES[] = {
        "nod2", "nod2d", "node", "nodes", "ncells", "npoints", "nod", "n2d",
        "cell", "cells", "elem", "vertex", "vertices",
        "n_node", "n_face", "n_edge",
        "nCells", "nVertices", "nEdges", NULL
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

    /* Convert from radians to degrees if necessary */
    if (is_radian_units(lon_info.units) || is_radian_units(lat_info.units)) {
        printf("Converting coordinates from radians to degrees\n");
        for (size_t i = 0; i < n_points; i++) {
            lon[i] = lon[i] * RAD2DEG;
            lat[i] = lat[i] * RAD2DEG;
        }
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

#ifdef HAVE_ZARR

/* Helper to read file contents */
static char *mesh_read_file(const char *path, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *data = malloc(size + 1);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(data, 1, size, fp);
    fclose(fp);

    if ((long)read != size) {
        free(data);
        return NULL;
    }

    data[size] = '\0';
    if (size_out) *size_out = size;
    return data;
}

/* Helper to read JSON file */
static cJSON *mesh_read_json(const char *path) {
    size_t size;
    char *contents = mesh_read_file(path, &size);
    if (!contents) return NULL;

    cJSON *json = cJSON_Parse(contents);
    free(contents);
    return json;
}

/* Read and decompress a zarr coordinate array (handles N-dimensional, multi-chunk arrays).
 * Returns flattened array of all values. Sets *ndim_out if non-NULL.
 * For 2D coords, *dim0_out and *dim1_out give the shape. */
static double *read_zarr_coord(const char *base_path, const char *coord_name,
                                size_t *n_points_out, int *ndim_out,
                                size_t *dim0_out, size_t *dim1_out) {
    char coord_path[PATH_MAX];
    char zarray_path[PATH_MAX];

    snprintf(coord_path, sizeof(coord_path), "%s/%s", base_path, coord_name);
    snprintf(zarray_path, sizeof(zarray_path), "%s/.zarray", coord_path);

    /* Read .zarray metadata */
    cJSON *zarray = mesh_read_json(zarray_path);
    if (!zarray) {
        return NULL;
    }

    /* Get shape */
    cJSON *shape = cJSON_GetObjectItem(zarray, "shape");
    if (!shape || !cJSON_IsArray(shape) || cJSON_GetArraySize(shape) < 1) {
        cJSON_Delete(zarray);
        return NULL;
    }

    int ndim = cJSON_GetArraySize(shape);
    if (ndim > 2) {
        /* Only support 1D and 2D coordinate arrays */
        cJSON_Delete(zarray);
        return NULL;
    }

    size_t shape_dims[2] = {0, 0};
    size_t chunk_dims[2] = {0, 0};
    size_t n_points = 1;
    for (int i = 0; i < ndim; i++) {
        shape_dims[i] = (size_t)cJSON_GetArrayItem(shape, i)->valuedouble;
        chunk_dims[i] = shape_dims[i];  /* Default: one chunk */
        n_points *= shape_dims[i];
    }

    *n_points_out = n_points;
    if (ndim_out) *ndim_out = ndim;
    if (dim0_out) *dim0_out = shape_dims[0];
    if (dim1_out) *dim1_out = (ndim > 1) ? shape_dims[1] : 0;

    /* Get chunk sizes */
    cJSON *chunks = cJSON_GetObjectItem(zarray, "chunks");
    if (chunks && cJSON_IsArray(chunks)) {
        for (int i = 0; i < ndim && i < cJSON_GetArraySize(chunks); i++) {
            chunk_dims[i] = (size_t)cJSON_GetArrayItem(chunks, i)->valuedouble;
        }
    }

    /* Get dtype */
    cJSON *dtype_obj = cJSON_GetObjectItem(zarray, "dtype");
    const char *dtype_str = dtype_obj ? dtype_obj->valuestring : "<f8";
    int dtype_size = 8;  /* Default to float64 */
    char dtype = 'd';
    if (dtype_str && strlen(dtype_str) >= 3) {
        dtype = dtype_str[1];
        dtype_size = atoi(&dtype_str[2]);
    }

    /* Get compressor */
    cJSON *comp = cJSON_GetObjectItem(zarray, "compressor");
    char *compressor_id = NULL;
    if (comp && !cJSON_IsNull(comp)) {
        cJSON *comp_id = cJSON_GetObjectItem(comp, "id");
        if (comp_id && cJSON_IsString(comp_id)) {
            compressor_id = comp_id->valuestring;
        }
    }

    /* Allocate buffer for all raw data */
    size_t total_raw_size = n_points * dtype_size;
    void *raw_data = malloc(total_raw_size);
    if (!raw_data) {
        cJSON_Delete(zarray);
        return NULL;
    }

    /* Calculate number of chunks per dimension */
    size_t n_chunks_per_dim[2];
    size_t total_chunks = 1;
    for (int i = 0; i < ndim; i++) {
        n_chunks_per_dim[i] = (shape_dims[i] + chunk_dims[i] - 1) / chunk_dims[i];
        total_chunks *= n_chunks_per_dim[i];
    }

    /* Read and decompress each chunk */
    for (size_t chunk_flat = 0; chunk_flat < total_chunks; chunk_flat++) {
        /* Decompose flat chunk index into per-dimension indices */
        size_t ci[2] = {0, 0};
        if (ndim == 1) {
            ci[0] = chunk_flat;
        } else {
            ci[0] = chunk_flat / n_chunks_per_dim[1];
            ci[1] = chunk_flat % n_chunks_per_dim[1];
        }

        /* Build chunk path (e.g., "coord/0" for 1D, "coord/0.0" for 2D) */
        char chunk_path[PATH_MAX];
        if (ndim == 1) {
            snprintf(chunk_path, sizeof(chunk_path), "%s/%zu", coord_path, ci[0]);
        } else {
            snprintf(chunk_path, sizeof(chunk_path), "%s/%zu.%zu", coord_path, ci[0], ci[1]);
        }

        /* Calculate this chunk's actual element count */
        size_t chunk_elements = 1;
        size_t chunk_actual[2];
        for (int i = 0; i < ndim; i++) {
            size_t start = ci[i] * chunk_dims[i];
            size_t remaining = shape_dims[i] - start;
            chunk_actual[i] = (remaining < chunk_dims[i]) ? remaining : chunk_dims[i];
            chunk_elements *= chunk_actual[i];
        }

        /* Expected decompressed size (full chunk, may be larger than actual for edge chunks) */
        size_t full_chunk_elements = 1;
        for (int i = 0; i < ndim; i++) full_chunk_elements *= chunk_dims[i];
        size_t expected_decomp_size = full_chunk_elements * dtype_size;

        size_t comp_size;
        void *compressed = mesh_read_file(chunk_path, &comp_size);
        if (!compressed) {
            fprintf(stderr, "Failed to read chunk: %s\n", chunk_path);
            free(raw_data);
            cJSON_Delete(zarray);
            return NULL;
        }

        /* Decompress to temp buffer */
        void *chunk_data = NULL;
        if (!compressor_id) {
            chunk_data = compressed;
        } else if (strcmp(compressor_id, "lz4") == 0) {
            if (comp_size < 4) {
                free(compressed);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
            chunk_data = malloc(expected_decomp_size);
            if (!chunk_data) {
                free(compressed);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
            uint32_t uncomp_size = *(uint32_t *)compressed;
            int result = LZ4_decompress_safe((const char *)compressed + 4,
                                             chunk_data,
                                             (int)(comp_size - 4),
                                             (int)uncomp_size);
            free(compressed);
            if (result < 0) {
                fprintf(stderr, "LZ4 decompression failed for %s\n", chunk_path);
                free(chunk_data);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
        } else if (strcmp(compressor_id, "blosc") == 0) {
            size_t nbytes, cbytes, blocksize;
            blosc_cbuffer_sizes(compressed, &nbytes, &cbytes, &blocksize);
            chunk_data = malloc(nbytes);
            if (!chunk_data) {
                free(compressed);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
            int result = blosc_decompress(compressed, chunk_data, nbytes);
            free(compressed);
            if (result < 0) {
                fprintf(stderr, "Blosc decompression failed for %s\n", chunk_path);
                free(chunk_data);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
        } else {
            fprintf(stderr, "Unknown compressor: %s\n", compressor_id);
            free(compressed);
            free(raw_data);
            cJSON_Delete(zarray);
            return NULL;
        }

        /* Copy chunk data into the output buffer at the right position */
        if (ndim == 1) {
            size_t dst_offset = ci[0] * chunk_dims[0];
            memcpy((char *)raw_data + dst_offset * dtype_size,
                   chunk_data, chunk_actual[0] * dtype_size);
        } else {
            /* 2D: copy row by row to handle chunking properly */
            size_t row_start = ci[0] * chunk_dims[0];
            size_t col_start = ci[1] * chunk_dims[1];
            for (size_t r = 0; r < chunk_actual[0]; r++) {
                size_t dst_offset = (row_start + r) * shape_dims[1] + col_start;
                size_t src_offset = r * chunk_dims[1];
                memcpy((char *)raw_data + dst_offset * dtype_size,
                       (char *)chunk_data + src_offset * dtype_size,
                       chunk_actual[1] * dtype_size);
            }
        }

        if (chunk_data != compressed) free(chunk_data);
        else free(compressed);
    }

    cJSON_Delete(zarray);

    /* Convert to double array */
    double *result = malloc(n_points * sizeof(double));
    if (!result) {
        free(raw_data);
        return NULL;
    }

    if (dtype == 'f' && dtype_size == 8) {
        memcpy(result, raw_data, n_points * sizeof(double));
    } else if (dtype == 'f' && dtype_size == 4) {
        float *src = (float *)raw_data;
        for (size_t i = 0; i < n_points; i++) {
            result[i] = (double)src[i];
        }
    } else {
        fprintf(stderr, "Unsupported coordinate dtype: %c%d\n", dtype, dtype_size);
        free(result);
        free(raw_data);
        return NULL;
    }

    free(raw_data);
    return result;
}

USMesh *mesh_create_from_zarr(USFile *file) {
    if (!file || !file->zarr_data) return NULL;

    /* Get base path from file */
    /* USFile->zarr_data is ZarrStore* which has base_path as first field */
    /* Since we can't include file_zarr.h internal types, read it directly */
    char *base_path = *(char **)file->zarr_data;  /* First field is base_path */

    printf("Loading coordinates from zarr store: %s\n", base_path);

    /* Try to read latitude and longitude coordinate arrays */
    size_t lat_points = 0, lon_points = 0;
    int lat_ndim = 0, lon_ndim = 0;
    size_t lat_dim0 = 0, lat_dim1 = 0;
    size_t lon_dim0 = 0, lon_dim1 = 0;

    /* Try common coordinate names (ordered by likelihood) */
    static const char *zarr_lat_names[] = {
        "latitude", "lat", "y", "nav_lat", "glat", "clat",
        "yt_ocean", "yu_ocean", "yh", "yq",
        "latCell", "latVertex", NULL
    };
    static const char *zarr_lon_names[] = {
        "longitude", "lon", "x", "nav_lon", "glon", "clon",
        "xt_ocean", "xu_ocean", "xh", "xq",
        "lonCell", "lonVertex", NULL
    };

    double *lat = NULL;
    for (int i = 0; zarr_lat_names[i] != NULL && !lat; i++) {
        lat = read_zarr_coord(base_path, zarr_lat_names[i], &lat_points,
                              &lat_ndim, &lat_dim0, &lat_dim1);
    }

    double *lon = NULL;
    for (int i = 0; zarr_lon_names[i] != NULL && !lon; i++) {
        lon = read_zarr_coord(base_path, zarr_lon_names[i], &lon_points,
                              &lon_ndim, &lon_dim0, &lon_dim1);
    }

    if (!lat || !lon) {
        fprintf(stderr, "Could not find latitude/longitude coordinates in zarr store\n");
        free(lat);
        free(lon);
        return NULL;
    }

    printf("Coordinate info: lat %dD [%zu", lat_ndim, lat_dim0);
    if (lat_ndim == 2) printf("x%zu", lat_dim1);
    printf("], lon %dD [%zu", lon_ndim, lon_dim0);
    if (lon_ndim == 2) printf("x%zu", lon_dim1);
    printf("]\n");

    size_t n_points;
    CoordType coord_type;
    size_t orig_nx = 0, orig_ny = 0;

    if (lat_ndim == 2 && lon_ndim == 2) {
        /* Both 2D -> curvilinear grid (already flattened by read_zarr_coord) */
        if (lat_dim0 != lon_dim0 || lat_dim1 != lon_dim1) {
            fprintf(stderr, "2D coordinate arrays have different shapes\n");
            free(lat);
            free(lon);
            return NULL;
        }
        coord_type = COORD_TYPE_2D_CURVILINEAR;
        orig_ny = lat_dim0;
        orig_nx = lat_dim1;
        n_points = lat_points;  /* Already = dim0 * dim1 */
        printf("Detected: 2D curvilinear grid (%zu x %zu = %zu points)\n",
               orig_ny, orig_nx, n_points);
    } else if (lat_ndim == 1 && lon_ndim == 1 && lat_points == lon_points) {
        /* Same size 1D arrays -> unstructured */
        coord_type = COORD_TYPE_1D_UNSTRUCTURED;
        n_points = lat_points;
        printf("Detected: 1D unstructured coordinates (%zu points)\n", n_points);
    } else if (lat_ndim == 1 && lon_ndim == 1) {
        /* Different size 1D arrays -> structured grid, create meshgrid */
        coord_type = COORD_TYPE_1D_STRUCTURED;
        orig_nx = lon_points;
        orig_ny = lat_points;
        n_points = orig_nx * orig_ny;
        printf("Detected: 1D structured grid (%zu x %zu = %zu points)\n",
               orig_nx, orig_ny, n_points);

        /* Read 1D coordinate arrays and expand to meshgrid */
        double *lon_2d = malloc(n_points * sizeof(double));
        double *lat_2d = malloc(n_points * sizeof(double));
        if (!lon_2d || !lat_2d) {
            free(lon_2d);
            free(lat_2d);
            free(lon);
            free(lat);
            return NULL;
        }

        /* Create meshgrid (flatten in row-major order: lat varies slowest) */
        size_t idx = 0;
        for (size_t j = 0; j < orig_ny; j++) {
            for (size_t i = 0; i < orig_nx; i++) {
                lon_2d[idx] = lon[i];
                lat_2d[idx] = lat[j];
                idx++;
            }
        }

        free(lon);
        free(lat);
        lon = lon_2d;
        lat = lat_2d;
    } else {
        fprintf(stderr, "Unsupported coordinate combination: lat %dD, lon %dD\n",
                lat_ndim, lon_ndim);
        free(lat);
        free(lon);
        return NULL;
    }

    printf("Loaded %zu coordinate points from zarr store\n", n_points);

    /* Normalize longitude to [-180, 180] */
    for (size_t i = 0; i < n_points; i++) {
        while (lon[i] > 180.0) lon[i] -= 360.0;
        while (lon[i] < -180.0) lon[i] += 360.0;
    }

    /* Create mesh */
    USMesh *mesh = mesh_create(lon, lat, n_points, coord_type);
    if (!mesh) {
        free(lon);
        free(lat);
        return NULL;
    }

    /* Store original grid dimensions for structured grids */
    mesh->orig_nx = orig_nx;
    mesh->orig_ny = orig_ny;

    return mesh;
}

#endif /* HAVE_ZARR */

/* Try to load MPAS dual-mesh connectivity (cellsOnVertex).
 * In MPAS, each Voronoi vertex connects exactly 3 cells.
 * cellsOnVertex(nVertices, vertexDegree=3) gives triangle connectivity
 * where the "vertices" of each triangle are cell centers (our mesh points).
 * Boundary triangles referencing cell 0 (invalid in 1-based) are skipped.
 */
static int load_mpas_connectivity(USMesh *mesh, int ncid) {
    int varid, status;

    status = nc_inq_varid(ncid, "cellsOnVertex", &varid);
    if (status != NC_NOERR) return -1;

    int ndims;
    nc_inq_varndims(ncid, varid, &ndims);
    if (ndims != 2) return -1;

    int dimids[2];
    size_t dim_sizes[2];
    nc_inq_vardimid(ncid, varid, dimids);
    nc_inq_dimlen(ncid, dimids[0], &dim_sizes[0]);
    nc_inq_dimlen(ncid, dimids[1], &dim_sizes[1]);

    /* Expect (nVertices, vertexDegree=3) */
    size_t n_tri_raw, vdeg;
    if (dim_sizes[1] == 3) {
        n_tri_raw = dim_sizes[0];
        vdeg = dim_sizes[1];
    } else if (dim_sizes[0] == 3) {
        n_tri_raw = dim_sizes[1];
        vdeg = dim_sizes[0];
    } else {
        return -1;  /* Not a triangle dual mesh */
    }

    int *raw = malloc(n_tri_raw * vdeg * sizeof(int));
    if (!raw) return -1;

    status = nc_get_var_int(ncid, varid, raw);
    if (status != NC_NOERR) {
        free(raw);
        return -1;
    }

    int transpose = (dim_sizes[0] == 3);

    /* First pass: count valid triangles (skip those referencing cell 0 = boundary) */
    size_t n_valid = 0;
    for (size_t i = 0; i < n_tri_raw; i++) {
        int v0, v1, v2;
        if (transpose) {
            v0 = raw[0 * n_tri_raw + i];
            v1 = raw[1 * n_tri_raw + i];
            v2 = raw[2 * n_tri_raw + i];
        } else {
            v0 = raw[i * 3 + 0];
            v1 = raw[i * 3 + 1];
            v2 = raw[i * 3 + 2];
        }
        if (v0 > 0 && v1 > 0 && v2 > 0) n_valid++;
    }

    int *elem_nodes = malloc(n_valid * 3 * sizeof(int));
    if (!elem_nodes) {
        free(raw);
        return -1;
    }

    /* Second pass: store valid triangles, convert to 0-based */
    size_t out = 0;
    for (size_t i = 0; i < n_tri_raw; i++) {
        int v0, v1, v2;
        if (transpose) {
            v0 = raw[0 * n_tri_raw + i];
            v1 = raw[1 * n_tri_raw + i];
            v2 = raw[2 * n_tri_raw + i];
        } else {
            v0 = raw[i * 3 + 0];
            v1 = raw[i * 3 + 1];
            v2 = raw[i * 3 + 2];
        }
        if (v0 > 0 && v1 > 0 && v2 > 0) {
            /* Also validate indices are within range */
            if ((size_t)(v0 - 1) < mesh->n_points &&
                (size_t)(v1 - 1) < mesh->n_points &&
                (size_t)(v2 - 1) < mesh->n_points) {
                elem_nodes[out * 3 + 0] = v0 - 1;
                elem_nodes[out * 3 + 1] = v1 - 1;
                elem_nodes[out * 3 + 2] = v2 - 1;
                out++;
            }
        }
    }

    free(raw);

    mesh->n_elements = out;
    mesh->n_vertices = 3;
    mesh->elem_nodes = elem_nodes;

    printf("Loaded MPAS dual-mesh connectivity: %zu triangles from cellsOnVertex "
           "(%zu raw, %zu boundary skipped)\n",
           out, n_tri_raw, n_tri_raw - out);
    return 0;
}

/* Load element connectivity from mesh file for polygon rendering */
static int load_element_connectivity(USMesh *mesh, int ncid) {
    int status, varid;

    /* Try to find face_nodes variable (UGRID convention) */
    static const char *CONN_NAMES[] = {
        "face_nodes", "face_node_connectivity", "elem", "elements", NULL
    };
    int found_conn = 0;
    for (int i = 0; CONN_NAMES[i] != NULL; i++) {
        status = nc_inq_varid(ncid, CONN_NAMES[i], &varid);
        if (status == NC_NOERR) {
            found_conn = 1;
            break;
        }
    }
    if (!found_conn) {
        /* Try MPAS dual-mesh connectivity (cellsOnVertex) */
        return load_mpas_connectivity(mesh, ncid);
    }

    /* Get dimensions */
    int ndims;
    nc_inq_varndims(ncid, varid, &ndims);
    if (ndims != 2) {
        fprintf(stderr, "face_nodes: expected 2D, got %dD\n", ndims);
        return -1;
    }

    int dimids[2];
    size_t dim_sizes[2];
    nc_inq_vardimid(ncid, varid, dimids);
    nc_inq_dimlen(ncid, dimids[0], &dim_sizes[0]);
    nc_inq_dimlen(ncid, dimids[1], &dim_sizes[1]);

    /* Determine which dimension is vertices (n3=3) vs elements */
    size_t n_vertices, n_elements;
    int transpose = 0;
    if (dim_sizes[0] == 3 || dim_sizes[0] == 4) {
        n_vertices = dim_sizes[0];
        n_elements = dim_sizes[1];
        transpose = 1;  /* Data is (n3, elem), need to transpose */
    } else if (dim_sizes[1] == 3 || dim_sizes[1] == 4) {
        n_elements = dim_sizes[0];
        n_vertices = dim_sizes[1];
        transpose = 0;
    } else {
        fprintf(stderr, "face_nodes: cannot identify vertex dimension\n");
        return -1;
    }

    printf("Loading element connectivity: %zu elements, %zu vertices each\n",
           n_elements, n_vertices);

    /* Allocate and read connectivity */
    int *raw_data = malloc(n_elements * n_vertices * sizeof(int));
    if (!raw_data) return -1;

    status = nc_get_var_int(ncid, varid, raw_data);
    if (status != NC_NOERR) {
        fprintf(stderr, "Failed to read face_nodes: %s\n", nc_strerror(status));
        free(raw_data);
        return -1;
    }

    /* Get start_index attribute (1-based or 0-based indexing) */
    int start_index = 1;  /* Default to 1-based (Fortran) */
    nc_get_att_int(ncid, varid, "start_index", &start_index);

    /* Allocate final array and transpose if needed */
    mesh->elem_nodes = malloc(n_elements * n_vertices * sizeof(int));
    if (!mesh->elem_nodes) {
        free(raw_data);
        return -1;
    }

    if (transpose) {
        /* Data is (n3, elem), transpose to (elem, n3) and convert to 0-based */
        for (size_t e = 0; e < n_elements; e++) {
            for (size_t v = 0; v < n_vertices; v++) {
                int node_idx = raw_data[v * n_elements + e];
                mesh->elem_nodes[e * n_vertices + v] = node_idx - start_index;
            }
        }
    } else {
        /* Data is (elem, n3), just convert to 0-based */
        for (size_t i = 0; i < n_elements * n_vertices; i++) {
            mesh->elem_nodes[i] = raw_data[i] - start_index;
        }
    }

    free(raw_data);

    mesh->n_elements = n_elements;
    mesh->n_vertices = (int)n_vertices;

    printf("Loaded %zu triangular elements for polygon rendering\n", n_elements);
    return 0;
}

int mesh_load_connectivity(USMesh *mesh, const char *mesh_filename) {
    if (!mesh) return -1;

    int ncid;
    const char *filename = (mesh_filename && mesh_filename[0])
                           ? mesh_filename : mesh->mesh_filename;
    if (!filename || !filename[0]) return -1;

    if (nc_open(filename, NC_NOWRITE, &ncid) != NC_NOERR) return -1;

    int result = load_element_connectivity(mesh, ncid);
    nc_close(ncid);
    return result;
}

void mesh_free(USMesh *mesh) {
    if (!mesh) return;
    free(mesh->lon);
    free(mesh->lat);
    free(mesh->xyz);
    free(mesh->elem_nodes);
    free(mesh->mesh_filename);
    free(mesh->lon_varname);
    free(mesh->lat_varname);
    free(mesh);
}
