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
    "xt_ocean", "xu_ocean", "xh", "xq", NULL
};

static const char *LAT_NAMES[] = {
  "lat", "latitude", "y", "nav_lat", "glat", "clat",
    "yt_ocean", "yu_ocean", "yh", "yq", NULL
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
        
        /* Try to load element connectivity for polygon rendering */
        int mesh_ncid_conn;
        if (nc_open(mesh_filename, NC_NOWRITE, &mesh_ncid_conn) == NC_NOERR) {
            load_element_connectivity(mesh, mesh_ncid_conn);
            nc_close(mesh_ncid_conn);
        }
    } else {
        /* Try to load connectivity from data file */
        load_element_connectivity(mesh, data_ncid);
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

/* Read and decompress a zarr coordinate array (handles multi-chunk arrays) */
static double *read_zarr_coord(const char *base_path, const char *coord_name,
                                size_t *n_points_out) {
    char coord_path[PATH_MAX];
    char zarray_path[PATH_MAX];

    snprintf(coord_path, sizeof(coord_path), "%s/%s", base_path, coord_name);
    snprintf(zarray_path, sizeof(zarray_path), "%s/.zarray", coord_path);

    /* Read .zarray metadata */
    cJSON *zarray = mesh_read_json(zarray_path);
    if (!zarray) {
        fprintf(stderr, "Failed to read %s\n", zarray_path);
        return NULL;
    }

    /* Get shape */
    cJSON *shape = cJSON_GetObjectItem(zarray, "shape");
    if (!shape || !cJSON_IsArray(shape) || cJSON_GetArraySize(shape) < 1) {
        cJSON_Delete(zarray);
        return NULL;
    }
    size_t n_points = (size_t)cJSON_GetArrayItem(shape, 0)->valuedouble;
    *n_points_out = n_points;

    /* Get chunk size */
    cJSON *chunks = cJSON_GetObjectItem(zarray, "chunks");
    size_t chunk_size = n_points;  /* Default: single chunk */
    if (chunks && cJSON_IsArray(chunks) && cJSON_GetArraySize(chunks) >= 1) {
        chunk_size = (size_t)cJSON_GetArrayItem(chunks, 0)->valuedouble;
    }

    /* Calculate number of chunks */
    size_t n_chunks = (n_points + chunk_size - 1) / chunk_size;

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

    /* Read and decompress each chunk */
    size_t offset = 0;
    for (size_t chunk_idx = 0; chunk_idx < n_chunks; chunk_idx++) {
        char chunk_path[PATH_MAX];
        snprintf(chunk_path, sizeof(chunk_path), "%s/%zu", coord_path, chunk_idx);

        size_t comp_size;
        void *compressed = mesh_read_file(chunk_path, &comp_size);
        if (!compressed) {
            fprintf(stderr, "Failed to read chunk: %s\n", chunk_path);
            free(raw_data);
            cJSON_Delete(zarray);
            return NULL;
        }

        /* Calculate this chunk's actual size (last chunk may be smaller) */
        size_t remaining = n_points - offset;
        size_t this_chunk_points = (remaining < chunk_size) ? remaining : chunk_size;
        size_t this_chunk_bytes = this_chunk_points * dtype_size;

        /* Decompress into the correct offset */
        void *chunk_dest = (char *)raw_data + offset * dtype_size;

        if (!compressor_id) {
            /* No compression */
            memcpy(chunk_dest, compressed, this_chunk_bytes);
            free(compressed);
        } else if (strcmp(compressor_id, "lz4") == 0) {
            if (comp_size < 4) {
                fprintf(stderr, "LZ4 chunk too small: %s\n", chunk_path);
                free(compressed);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
            uint32_t uncomp_size = *(uint32_t *)compressed;
            int result = LZ4_decompress_safe((const char *)compressed + 4,
                                             chunk_dest,
                                             (int)(comp_size - 4),
                                             (int)uncomp_size);
            free(compressed);
            if (result < 0) {
                fprintf(stderr, "LZ4 decompression failed for %s chunk %zu\n",
                        coord_name, chunk_idx);
                free(raw_data);
                cJSON_Delete(zarray);
                return NULL;
            }
        } else if (strcmp(compressor_id, "blosc") == 0) {
            /* Get actual uncompressed size from blosc header */
            size_t nbytes, cbytes, blocksize;
            blosc_cbuffer_sizes(compressed, &nbytes, &cbytes, &blocksize);

            /* Decompress to temp buffer if chunk is larger than needed (last chunk case) */
            if (nbytes > this_chunk_bytes) {
                void *temp = malloc(nbytes);
                if (!temp) {
                    free(compressed);
                    free(raw_data);
                    cJSON_Delete(zarray);
                    return NULL;
                }
                int result = blosc_decompress(compressed, temp, nbytes);
                free(compressed);
                if (result < 0) {
                    fprintf(stderr, "Blosc decompression failed for %s chunk %zu\n",
                            coord_name, chunk_idx);
                    free(temp);
                    free(raw_data);
                    cJSON_Delete(zarray);
                    return NULL;
                }
                /* Copy only the needed portion */
                memcpy(chunk_dest, temp, this_chunk_bytes);
                free(temp);
            } else {
                int result = blosc_decompress(compressed, chunk_dest, nbytes);
                free(compressed);
                if (result < 0) {
                    fprintf(stderr, "Blosc decompression failed for %s chunk %zu\n",
                            coord_name, chunk_idx);
                    free(raw_data);
                    cJSON_Delete(zarray);
                    return NULL;
                }
            }
        } else {
            fprintf(stderr, "Unknown compressor: %s\n", compressor_id);
            free(compressed);
            free(raw_data);
            cJSON_Delete(zarray);
            return NULL;
        }

        offset += this_chunk_points;
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

    /* Try to read latitude and longitude */
    size_t lat_points = 0, lon_points = 0;

    /* Try different coordinate names */
    double *lat = read_zarr_coord(base_path, "latitude", &lat_points);
    if (!lat) {
        lat = read_zarr_coord(base_path, "lat", &lat_points);
    }

    double *lon = read_zarr_coord(base_path, "longitude", &lon_points);
    if (!lon) {
        lon = read_zarr_coord(base_path, "lon", &lon_points);
    }

    if (!lat || !lon) {
        fprintf(stderr, "Could not find latitude/longitude coordinates in zarr store\n");
        free(lat);
        free(lon);
        return NULL;
    }

    if (lat_points != lon_points) {
        fprintf(stderr, "Coordinate array size mismatch: lat=%zu, lon=%zu\n",
                lat_points, lon_points);
        free(lat);
        free(lon);
        return NULL;
    }

    size_t n_points = lat_points;
    printf("Loaded %zu coordinate points from zarr store\n", n_points);

    /* Normalize longitude to [-180, 180] */
    for (size_t i = 0; i < n_points; i++) {
        while (lon[i] > 180.0) lon[i] -= 360.0;
        while (lon[i] < -180.0) lon[i] += 360.0;
    }

    /* Create mesh - zarr data is always unstructured (1D coordinate arrays) */
    USMesh *mesh = mesh_create(lon, lat, n_points, COORD_TYPE_1D_UNSTRUCTURED);
    if (!mesh) {
        free(lon);
        free(lat);
        return NULL;
    }

    return mesh;
}

#endif /* HAVE_ZARR */

/* Load element connectivity from mesh file for polygon rendering */
static int load_element_connectivity(USMesh *mesh, int ncid) {
    int status, varid;
    
    /* Try to find face_nodes variable (UGRID convention) */
    status = nc_inq_varid(ncid, "face_nodes", &varid);
    if (status != NC_NOERR) {
        /* Try alternate names */
        status = nc_inq_varid(ncid, "elem", &varid);
        if (status != NC_NOERR) {
            return -1;  /* No connectivity found - not an error, just no polygon mode */
        }
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
