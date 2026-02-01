/*
 * file_zarr.c - Zarr v2 file reading implementation
 *
 * Supports:
 * - Zarr v2 format stores
 * - LZ4 and Blosc compressors
 * - Float32, Float64, Int64 data types
 * - Consolidated metadata (.zmetadata)
 */

#ifdef HAVE_ZARR

#include "file_zarr.h"
#include "cJSON/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <lz4.h>
#include <blosc.h>

/* Dimension name patterns (same as file_netcdf.c) */
static const char *TIME_NAMES[] = {"time", "t", "Time", "TIME", NULL};
static const char *DEPTH_NAMES[] = {"depth", "z", "lev", "level", "nz", "nz1", NULL};
static const char *NODE_NAMES[] = {"values", "nod2", "nod2d", "node", "nodes", "ncells", "npoints", NULL};

/* Internal zarr store data */
typedef struct {
    char *base_path;              /* Path to .zarr directory */
    cJSON *metadata;              /* Parsed .zmetadata (consolidated) */
    int use_consolidated;         /* 1 if .zmetadata exists */
} ZarrStore;

/* Internal zarr array data */
typedef struct {
    char *array_path;             /* Path to array directory */
    cJSON *zarray;                /* Parsed .zarray metadata */
    cJSON *zattrs;                /* Parsed .zattrs */
    size_t *shape;                /* Array shape */
    size_t *chunks;               /* Chunk sizes */
    int ndim;                     /* Number of dimensions */
    char dtype;                   /* 'f' = float, 'd' = double, 'i' = int64 */
    int dtype_size;               /* Bytes per element */
    int is_little_endian;         /* Byte order */
    char *compressor_id;          /* "lz4", "blosc", or NULL */
    int blosc_shuffle;            /* Blosc shuffle mode */
    char *blosc_cname;            /* Blosc inner codec */
    float fill_value;             /* Fill value for missing data */
} ZarrArray;

/* Forward declarations */
static cJSON *read_json_file(const char *path);
static char *read_file_contents(const char *path, size_t *size_out);
static void *zarr_decompress(const void *compressed, size_t comp_size,
                             size_t expected_size, ZarrArray *za);
static int parse_dtype(const char *dtype_str, char *dtype, int *size, int *little_endian);
static int matches_name_list(const char *name, const char **list);

/*
 * Check if path is a zarr store
 */
int zarr_is_zarr_store(const char *path) {
    if (!path) return 0;

    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return 0;

    /* Check for .zgroup file */
    char zgroup_path[PATH_MAX];
    snprintf(zgroup_path, sizeof(zgroup_path), "%s/.zgroup", path);

    if (stat(zgroup_path, &st) == 0 && S_ISREG(st.st_mode)) {
        return 1;
    }

    return 0;
}

/*
 * Open a zarr store
 */
USFile *zarr_open(const char *path) {
    if (!path) return NULL;

    if (!zarr_is_zarr_store(path)) {
        fprintf(stderr, "Not a zarr store: %s\n", path);
        return NULL;
    }

    /* Allocate file structure */
    USFile *file = calloc(1, sizeof(USFile));
    if (!file) return NULL;

    ZarrStore *store = calloc(1, sizeof(ZarrStore));
    if (!store) {
        free(file);
        return NULL;
    }

    file->file_type = FILE_TYPE_ZARR;
    file->zarr_data = store;
    file->ncid = -1;  /* Not used for zarr */
    strncpy(file->filename, path, MAX_NAME_LEN - 1);

    /* Store base path */
    store->base_path = strdup(path);

    /* Try to read consolidated metadata */
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/.zmetadata", path);

    store->metadata = read_json_file(meta_path);
    store->use_consolidated = (store->metadata != NULL);

    if (store->use_consolidated) {
        printf("Zarr: Using consolidated metadata\n");
    } else {
        printf("Zarr: No consolidated metadata, will read individual .zarray files\n");
    }

    return file;
}

/*
 * Parse ZarrArray from JSON metadata
 */
static ZarrArray *parse_zarray(const char *array_path, cJSON *zarray, cJSON *zattrs) {
    if (!zarray) return NULL;

    ZarrArray *za = calloc(1, sizeof(ZarrArray));
    if (!za) return NULL;

    za->array_path = strdup(array_path);

    /* Keep references to JSON objects */
    za->zarray = zarray;
    za->zattrs = zattrs;

    /* Parse shape */
    cJSON *shape = cJSON_GetObjectItem(zarray, "shape");
    if (shape && cJSON_IsArray(shape)) {
        za->ndim = cJSON_GetArraySize(shape);
        za->shape = malloc(za->ndim * sizeof(size_t));
        for (int i = 0; i < za->ndim; i++) {
            za->shape[i] = (size_t)cJSON_GetArrayItem(shape, i)->valuedouble;
        }
    }

    /* Parse chunks */
    cJSON *chunks = cJSON_GetObjectItem(zarray, "chunks");
    if (chunks && cJSON_IsArray(chunks)) {
        za->chunks = malloc(za->ndim * sizeof(size_t));
        for (int i = 0; i < za->ndim; i++) {
            za->chunks[i] = (size_t)cJSON_GetArrayItem(chunks, i)->valuedouble;
        }
    }

    /* Parse dtype */
    cJSON *dtype = cJSON_GetObjectItem(zarray, "dtype");
    if (dtype && cJSON_IsString(dtype)) {
        parse_dtype(dtype->valuestring, &za->dtype, &za->dtype_size, &za->is_little_endian);
    }

    /* Parse fill_value */
    cJSON *fill = cJSON_GetObjectItem(zarray, "fill_value");
    if (fill) {
        if (cJSON_IsNumber(fill)) {
            za->fill_value = (float)fill->valuedouble;
        } else if (cJSON_IsString(fill) && strcasecmp(fill->valuestring, "NaN") == 0) {
            za->fill_value = NAN;
        } else if (cJSON_IsNull(fill)) {
            za->fill_value = DEFAULT_FILL_VALUE;
        } else {
            za->fill_value = DEFAULT_FILL_VALUE;
        }
    } else {
        za->fill_value = DEFAULT_FILL_VALUE;
    }

    /* Parse compressor */
    cJSON *comp = cJSON_GetObjectItem(zarray, "compressor");
    if (comp && !cJSON_IsNull(comp)) {
        cJSON *comp_id = cJSON_GetObjectItem(comp, "id");
        if (comp_id && cJSON_IsString(comp_id)) {
            za->compressor_id = strdup(comp_id->valuestring);

            /* Blosc-specific settings */
            if (strcmp(za->compressor_id, "blosc") == 0) {
                cJSON *shuffle = cJSON_GetObjectItem(comp, "shuffle");
                if (shuffle) za->blosc_shuffle = shuffle->valueint;

                cJSON *cname = cJSON_GetObjectItem(comp, "cname");
                if (cname && cJSON_IsString(cname)) {
                    za->blosc_cname = strdup(cname->valuestring);
                }
            }
        }
    }

    return za;
}

/*
 * Free ZarrArray
 */
static void free_zarray(ZarrArray *za) {
    if (!za) return;
    free(za->array_path);
    free(za->shape);
    free(za->chunks);
    free(za->compressor_id);
    free(za->blosc_cname);
    free(za);
}

/*
 * Scan for displayable variables
 */
USVar *zarr_scan_variables(USFile *file, USMesh *mesh) {
    if (!file || !file->zarr_data || !mesh) return NULL;

    ZarrStore *store = (ZarrStore *)file->zarr_data;
    USVar *var_list = NULL;
    USVar *var_tail = NULL;
    int var_count = 0;

    if (store->use_consolidated) {
        /* Parse from consolidated metadata */
        cJSON *metadata = cJSON_GetObjectItem(store->metadata, "metadata");
        if (!metadata) return NULL;

        cJSON *item;
        cJSON_ArrayForEach(item, metadata) {
            const char *key = item->string;

            /* Look for .zarray entries */
            if (!strstr(key, "/.zarray")) continue;

            /* Extract variable name */
            char varname[MAX_NAME_LEN];
            const char *slash = strchr(key, '/');
            if (!slash) continue;
            size_t len = slash - key;
            if (len >= MAX_NAME_LEN) len = MAX_NAME_LEN - 1;
            strncpy(varname, key, len);
            varname[len] = '\0';

            /* Skip coordinate variables */
            if (strcasecmp(varname, "latitude") == 0 ||
                strcasecmp(varname, "longitude") == 0 ||
                strcasecmp(varname, "lat") == 0 ||
                strcasecmp(varname, "lon") == 0 ||
                strcasecmp(varname, "time") == 0) {
                continue;
            }

            /* Get .zattrs for this variable */
            char attrs_key[MAX_NAME_LEN + 16];
            snprintf(attrs_key, sizeof(attrs_key), "%s/.zattrs", varname);
            cJSON *zattrs = cJSON_GetObjectItem(metadata, attrs_key);

            /* Build array path */
            char array_path[PATH_MAX];
            snprintf(array_path, sizeof(array_path), "%s/%s", store->base_path, varname);

            /* Parse array metadata */
            ZarrArray *za = parse_zarray(array_path, item, zattrs);
            if (!za) continue;

            /* Check if this is a data variable (has spatial dimension matching mesh) */
            int has_spatial = 0;
            int node_dim = -1;
            for (int d = 0; d < za->ndim; d++) {
                if (za->shape[d] == mesh->n_points) {
                    has_spatial = 1;
                    node_dim = d;
                    break;
                }
            }
            if (!has_spatial) {
                free_zarray(za);
                continue;
            }

            /* Identify dimensions from _ARRAY_DIMENSIONS attribute */
            int time_dim = -1;
            int depth_dim = -1;

            if (zattrs) {
                cJSON *dims = cJSON_GetObjectItem(zattrs, "_ARRAY_DIMENSIONS");
                if (dims && cJSON_IsArray(dims)) {
                    for (int d = 0; d < cJSON_GetArraySize(dims); d++) {
                        cJSON *dim_item = cJSON_GetArrayItem(dims, d);
                        if (!dim_item || !cJSON_IsString(dim_item)) continue;
                        const char *dim_name = dim_item->valuestring;

                        if (matches_name_list(dim_name, TIME_NAMES)) {
                            time_dim = d;
                        } else if (matches_name_list(dim_name, DEPTH_NAMES)) {
                            depth_dim = d;
                        } else if (matches_name_list(dim_name, NODE_NAMES)) {
                            node_dim = d;
                        }
                    }
                }
            }

            /* Create variable structure */
            USVar *var = calloc(1, sizeof(USVar));
            if (!var) {
                free_zarray(za);
                continue;
            }

            strncpy(var->name, varname, MAX_NAME_LEN - 1);
            var->n_dims = za->ndim;
            var->file = file;
            var->mesh = mesh;
            var->time_dim_id = time_dim;
            var->depth_dim_id = depth_dim;
            var->node_dim_id = node_dim;
            var->fill_value = za->fill_value;
            var->zarr_data = za;

            /* Copy dimension info */
            for (int d = 0; d < za->ndim && d < MAX_DIMS; d++) {
                var->dim_sizes[d] = za->shape[d];

                /* Get dimension name from _ARRAY_DIMENSIONS */
                if (zattrs) {
                    cJSON *dims = cJSON_GetObjectItem(zattrs, "_ARRAY_DIMENSIONS");
                    if (dims && d < cJSON_GetArraySize(dims)) {
                        cJSON *dim_item = cJSON_GetArrayItem(dims, d);
                        if (dim_item && cJSON_IsString(dim_item)) {
                            strncpy(var->dim_names[d], dim_item->valuestring, MAX_NAME_LEN - 1);
                        }
                    }
                }
            }

            /* Get long_name and units from attributes */
            if (zattrs) {
                cJSON *long_name = cJSON_GetObjectItem(zattrs, "long_name");
                if (long_name && cJSON_IsString(long_name)) {
                    strncpy(var->long_name, long_name->valuestring, MAX_NAME_LEN - 1);
                }
                cJSON *units = cJSON_GetObjectItem(zattrs, "units");
                if (units && cJSON_IsString(units)) {
                    strncpy(var->units, units->valuestring, MAX_NAME_LEN - 1);
                }
            }

            /* Add to list */
            if (!var_list) {
                var_list = var;
            } else {
                var_tail->next = var;
            }
            var_tail = var;
            var_count++;

            printf("Found zarr variable: %s [", varname);
            for (int d = 0; d < za->ndim; d++) {
                printf("%s%s=%zu", d > 0 ? ", " : "", var->dim_names[d], za->shape[d]);
            }
            printf("]");
            if (time_dim >= 0) printf(" (time=%d)", time_dim);
            if (depth_dim >= 0) printf(" (depth=%d)", depth_dim);
            printf("\n");
        }
    } else {
        /* Scan directory for arrays */
        DIR *dir = opendir(store->base_path);
        if (!dir) return NULL;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            /* Check if it's a directory with .zarray */
            char array_path[PATH_MAX];
            char zarray_path[PATH_MAX];
            snprintf(array_path, sizeof(array_path), "%s/%s", store->base_path, entry->d_name);
            snprintf(zarray_path, sizeof(zarray_path), "%s/.zarray", array_path);

            struct stat st;
            if (stat(zarray_path, &st) != 0) continue;

            /* Skip coordinate variables */
            if (strcasecmp(entry->d_name, "latitude") == 0 ||
                strcasecmp(entry->d_name, "longitude") == 0 ||
                strcasecmp(entry->d_name, "time") == 0) {
                continue;
            }

            /* Read .zarray and .zattrs */
            cJSON *zarray = read_json_file(zarray_path);
            if (!zarray) continue;

            char zattrs_path[PATH_MAX];
            snprintf(zattrs_path, sizeof(zattrs_path), "%s/.zattrs", array_path);
            cJSON *zattrs = read_json_file(zattrs_path);

            ZarrArray *za = parse_zarray(array_path, zarray, zattrs);
            if (!za) {
                cJSON_Delete(zarray);
                if (zattrs) cJSON_Delete(zattrs);
                continue;
            }

            /* Check spatial dimension */
            int has_spatial = 0;
            int node_dim = -1;
            for (int d = 0; d < za->ndim; d++) {
                if (za->shape[d] == mesh->n_points) {
                    has_spatial = 1;
                    node_dim = d;
                    break;
                }
            }
            if (!has_spatial) {
                free_zarray(za);
                cJSON_Delete(zarray);
                if (zattrs) cJSON_Delete(zattrs);
                continue;
            }

            /* Identify dimensions */
            int time_dim = -1, depth_dim = -1;
            if (zattrs) {
                cJSON *dims = cJSON_GetObjectItem(zattrs, "_ARRAY_DIMENSIONS");
                if (dims && cJSON_IsArray(dims)) {
                    for (int d = 0; d < cJSON_GetArraySize(dims); d++) {
                        cJSON *dim_item = cJSON_GetArrayItem(dims, d);
                        if (!dim_item || !cJSON_IsString(dim_item)) continue;
                        const char *dim_name = dim_item->valuestring;
                        if (matches_name_list(dim_name, TIME_NAMES)) time_dim = d;
                        else if (matches_name_list(dim_name, DEPTH_NAMES)) depth_dim = d;
                        else if (matches_name_list(dim_name, NODE_NAMES)) node_dim = d;
                    }
                }
            }

            /* Create variable */
            USVar *var = calloc(1, sizeof(USVar));
            if (!var) {
                free_zarray(za);
                cJSON_Delete(zarray);
                if (zattrs) cJSON_Delete(zattrs);
                continue;
            }

            strncpy(var->name, entry->d_name, MAX_NAME_LEN - 1);
            var->n_dims = za->ndim;
            var->file = file;
            var->mesh = mesh;
            var->time_dim_id = time_dim;
            var->depth_dim_id = depth_dim;
            var->node_dim_id = node_dim;
            var->fill_value = za->fill_value;
            var->zarr_data = za;

            for (int d = 0; d < za->ndim && d < MAX_DIMS; d++) {
                var->dim_sizes[d] = za->shape[d];
            }

            if (zattrs) {
                cJSON *long_name = cJSON_GetObjectItem(zattrs, "long_name");
                if (long_name && cJSON_IsString(long_name)) {
                    strncpy(var->long_name, long_name->valuestring, MAX_NAME_LEN - 1);
                }
                cJSON *units = cJSON_GetObjectItem(zattrs, "units");
                if (units && cJSON_IsString(units)) {
                    strncpy(var->units, units->valuestring, MAX_NAME_LEN - 1);
                }
            }

            if (!var_list) var_list = var;
            else var_tail->next = var;
            var_tail = var;
            var_count++;

            printf("Found zarr variable: %s [shape=", entry->d_name);
            for (int d = 0; d < za->ndim; d++) {
                printf("%s%zu", d > 0 ? "x" : "", za->shape[d]);
            }
            printf("]\n");
        }
        closedir(dir);
    }

    file->vars = var_list;
    file->n_vars = var_count;
    printf("Found %d displayable zarr variables\n", var_count);

    return var_list;
}

/*
 * Read and decompress a chunk file
 */
static void *zarr_read_chunk(const char *chunk_path, ZarrArray *za, size_t expected_size) {
    size_t comp_size;
    void *compressed = read_file_contents(chunk_path, &comp_size);
    if (!compressed) {
        fprintf(stderr, "Failed to read chunk: %s\n", chunk_path);
        return NULL;
    }

    void *output = NULL;

    if (!za->compressor_id) {
        /* No compression */
        output = compressed;
    } else {
        output = zarr_decompress(compressed, comp_size, expected_size, za);
        free(compressed);
    }

    return output;
}

/*
 * Read a 2D slice from zarr variable
 */
int zarr_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data) {
    if (!var || !var->zarr_data || !data) return -1;

    ZarrArray *za = (ZarrArray *)var->zarr_data;
    size_t n_points = var->mesh->n_points;

    /* Determine chunk indices */
    size_t time_chunk = 0, depth_chunk = 0;
    size_t local_time = time_idx, local_depth = depth_idx;

    if (var->time_dim_id >= 0) {
        time_chunk = time_idx / za->chunks[var->time_dim_id];
        local_time = time_idx % za->chunks[var->time_dim_id];
    }
    if (var->depth_dim_id >= 0) {
        depth_chunk = depth_idx / za->chunks[var->depth_dim_id];
        local_depth = depth_idx % za->chunks[var->depth_dim_id];
    }

    /* Build chunk filename */
    char chunk_path[PATH_MAX];
    char chunk_key[256] = "";

    for (int d = 0; d < za->ndim; d++) {
        char part[32];
        size_t chunk_idx;

        if (d == var->time_dim_id) {
            chunk_idx = time_chunk;
        } else if (d == var->depth_dim_id) {
            chunk_idx = depth_chunk;
        } else {
            chunk_idx = 0;  /* Spatial dimension assumed to be in single chunk */
        }

        if (d > 0) strcat(chunk_key, ".");
        snprintf(part, sizeof(part), "%zu", chunk_idx);
        strcat(chunk_key, part);
    }

    snprintf(chunk_path, sizeof(chunk_path), "%s/%s", za->array_path, chunk_key);

    /* Calculate expected uncompressed size */
    size_t chunk_elements = 1;
    for (int d = 0; d < za->ndim; d++) {
        chunk_elements *= za->chunks[d];
    }
    size_t expected_size = chunk_elements * za->dtype_size;

    /* Read chunk */
    void *chunk_data = zarr_read_chunk(chunk_path, za, expected_size);
    if (!chunk_data) return -1;

    /* Extract the slice we need */
    /* For shape [time, values] with chunks [1, n], each chunk has exactly one time slice */
    size_t slice_offset = 0;

    /* Calculate offset within chunk for our time/depth indices */
    if (var->time_dim_id >= 0 && var->time_dim_id == 0) {
        slice_offset = local_time * za->chunks[1];
    }

    /* Copy and convert to float */
    if (za->dtype == 'f' && za->dtype_size == 4) {
        /* Already float32 */
        memcpy(data, (char *)chunk_data + slice_offset * sizeof(float), n_points * sizeof(float));
    } else if (za->dtype == 'd') {
        /* Double to float */
        double *src = (double *)((char *)chunk_data + slice_offset * sizeof(double));
        for (size_t i = 0; i < n_points; i++) {
            data[i] = (float)src[i];
        }
    } else if (za->dtype == 'i' && za->dtype_size == 8) {
        /* Int64 to float */
        int64_t *src = (int64_t *)((char *)chunk_data + slice_offset * sizeof(int64_t));
        for (size_t i = 0; i < n_points; i++) {
            data[i] = (float)src[i];
        }
    } else {
        fprintf(stderr, "Unsupported dtype: %c (size %d)\n", za->dtype, za->dtype_size);
        free(chunk_data);
        return -1;
    }

    free(chunk_data);
    return 0;
}

/*
 * Estimate data range by sampling
 */
int zarr_estimate_range(USVar *var, float *min_val, float *max_val) {
    if (!var || !var->mesh) return -1;

    size_t n_points = var->mesh->n_points;
    float *data = malloc(n_points * sizeof(float));
    if (!data) return -1;

    float global_min = 1e30f;
    float global_max = -1e30f;

    size_t n_times = (var->time_dim_id >= 0) ? var->dim_sizes[var->time_dim_id] : 1;
    size_t sample_times[] = {0, n_times / 2, n_times - 1};
    int n_samples = (n_times > 2) ? 3 : (int)n_times;

    for (int t = 0; t < n_samples; t++) {
        size_t time_idx = sample_times[t];
        if (time_idx >= n_times) continue;

        if (zarr_read_slice(var, time_idx, 0, data) != 0) continue;

        for (size_t i = 0; i < n_points; i++) {
            float v = data[i];
            if (!isfinite(v)) continue;
            if (fabsf(v) > 1e10f) continue;

            if (v < global_min) global_min = v;
            if (v > global_max) global_max = v;
        }
    }

    free(data);

    if (global_min > global_max) {
        *min_val = 0.0f;
        *max_val = 1.0f;
        return -1;
    }

    *min_val = global_min;
    *max_val = global_max;

    printf("Estimated zarr range for %s: [%.4f, %.4f]\n", var->name, global_min, global_max);
    return 0;
}

/*
 * Get dimension info for time/depth sliders
 */
USDimInfo *zarr_get_dim_info(USVar *var, int *n_dims_out) {
    if (!var || !var->file || !n_dims_out) return NULL;

    ZarrStore *store = (ZarrStore *)var->file->zarr_data;
    ZarrArray *za = (ZarrArray *)var->zarr_data;

    /* Count scannable dimensions */
    int n_scannable = 0;
    if (var->time_dim_id >= 0) n_scannable++;
    if (var->depth_dim_id >= 0) n_scannable++;

    if (n_scannable == 0) {
        *n_dims_out = 0;
        return NULL;
    }

    USDimInfo *dims = calloc(n_scannable, sizeof(USDimInfo));
    if (!dims) {
        *n_dims_out = 0;
        return NULL;
    }

    int idx = 0;
    for (int d = 0; d < var->n_dims; d++) {
        if (d != var->time_dim_id && d != var->depth_dim_id) continue;

        USDimInfo *di = &dims[idx];
        strncpy(di->name, var->dim_names[d], MAX_NAME_LEN - 1);
        di->size = var->dim_sizes[d];
        di->current = 0;
        di->is_scannable = (di->size > 1);

        /* Try to read coordinate values from zarr */
        char coord_path[PATH_MAX];
        char zarray_path[PATH_MAX];

        snprintf(coord_path, sizeof(coord_path), "%s/%s", store->base_path, var->dim_names[d]);
        snprintf(zarray_path, sizeof(zarray_path), "%s/.zarray", coord_path);

        struct stat st;
        if (stat(zarray_path, &st) == 0) {
            /* Read coordinate array metadata */
            cJSON *coord_zarray = NULL;
            cJSON *coord_zattrs = NULL;

            if (store->use_consolidated) {
                cJSON *metadata = cJSON_GetObjectItem(store->metadata, "metadata");
                char key[MAX_NAME_LEN + 16];
                snprintf(key, sizeof(key), "%s/.zarray", var->dim_names[d]);
                coord_zarray = cJSON_GetObjectItem(metadata, key);
                snprintf(key, sizeof(key), "%s/.zattrs", var->dim_names[d]);
                coord_zattrs = cJSON_GetObjectItem(metadata, key);
            } else {
                coord_zarray = read_json_file(zarray_path);
                char attrs_path[PATH_MAX];
                snprintf(attrs_path, sizeof(attrs_path), "%s/.zattrs", coord_path);
                coord_zattrs = read_json_file(attrs_path);
            }

            if (coord_zarray) {
                ZarrArray *coord_za = parse_zarray(coord_path, coord_zarray, coord_zattrs);
                if (coord_za) {
                    /* Read coordinate values */
                    size_t coord_size = di->size * coord_za->dtype_size;
                    char chunk_path[PATH_MAX];
                    snprintf(chunk_path, sizeof(chunk_path), "%s/0", coord_path);

                    void *coord_data = zarr_read_chunk(chunk_path, coord_za, coord_size);
                    if (coord_data) {
                        di->values = malloc(di->size * sizeof(double));
                        if (di->values) {
                            if (coord_za->dtype == 'd') {
                                memcpy(di->values, coord_data, di->size * sizeof(double));
                            } else if (coord_za->dtype == 'i' && coord_za->dtype_size == 8) {
                                int64_t *src = (int64_t *)coord_data;
                                for (size_t i = 0; i < di->size; i++) {
                                    di->values[i] = (double)src[i];
                                }
                            } else if (coord_za->dtype == 'f') {
                                float *src = (float *)coord_data;
                                for (size_t i = 0; i < di->size; i++) {
                                    di->values[i] = (double)src[i];
                                }
                            }

                            di->min_val = di->values[0];
                            di->max_val = di->values[di->size - 1];
                        }
                        free(coord_data);
                    }

                    /* Get units from attributes */
                    if (coord_zattrs) {
                        cJSON *units = cJSON_GetObjectItem(coord_zattrs, "units");
                        if (units && cJSON_IsString(units)) {
                            strncpy(di->units, units->valuestring, MAX_NAME_LEN - 1);
                        }
                    }

                    free_zarray(coord_za);
                }

                if (!store->use_consolidated) {
                    cJSON_Delete(coord_zarray);
                    if (coord_zattrs) cJSON_Delete(coord_zattrs);
                }
            }
        }

        if (!di->values) {
            di->min_val = 0;
            di->max_val = (double)(di->size - 1);
        }

        idx++;
    }

    *n_dims_out = n_scannable;
    return dims;
}

/*
 * Free dimension info
 */
void zarr_free_dim_info(USDimInfo *dims, int n_dims) {
    if (!dims) return;
    for (int i = 0; i < n_dims; i++) {
        free(dims[i].values);
    }
    free(dims);
}

/*
 * Close zarr store
 */
void zarr_close(USFile *file) {
    if (!file) return;

    ZarrStore *store = (ZarrStore *)file->zarr_data;
    if (store) {
        free(store->base_path);
        if (store->metadata) cJSON_Delete(store->metadata);
        free(store);
    }

    /* Free variables */
    USVar *var = file->vars;
    while (var) {
        USVar *next = var->next;
        if (var->zarr_data) {
            free_zarray((ZarrArray *)var->zarr_data);
        }
        free(var);
        var = next;
    }

    free(file);
}

/* ---- Helper functions ---- */

static char *read_file_contents(const char *path, size_t *size_out) {
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

static cJSON *read_json_file(const char *path) {
    size_t size;
    char *contents = read_file_contents(path, &size);
    if (!contents) return NULL;

    cJSON *json = cJSON_Parse(contents);
    free(contents);
    return json;
}

static void *zarr_decompress(const void *compressed, size_t comp_size,
                             size_t expected_size, ZarrArray *za) {
    void *output = malloc(expected_size);
    if (!output) return NULL;

    if (strcmp(za->compressor_id, "lz4") == 0) {
        /* LZ4 format: 4-byte little-endian uncompressed size header */
        if (comp_size < 4) {
            free(output);
            return NULL;
        }

        uint32_t uncomp_size = *(uint32_t *)compressed;
        int result = LZ4_decompress_safe((const char *)compressed + 4,
                                         output,
                                         (int)(comp_size - 4),
                                         (int)uncomp_size);
        if (result < 0) {
            fprintf(stderr, "LZ4 decompression failed\n");
            free(output);
            return NULL;
        }
    } else if (strcmp(za->compressor_id, "blosc") == 0) {
        /* Blosc handles its own header */
        int result = blosc_decompress(compressed, output, expected_size);
        if (result < 0) {
            fprintf(stderr, "Blosc decompression failed: %d\n", result);
            free(output);
            return NULL;
        }
    } else {
        fprintf(stderr, "Unknown compressor: %s\n", za->compressor_id);
        free(output);
        return NULL;
    }

    return output;
}

static int parse_dtype(const char *dtype_str, char *dtype, int *size, int *little_endian) {
    if (!dtype_str || strlen(dtype_str) < 2) return -1;

    /* First char is byte order: < = little-endian, > = big-endian, | = not applicable */
    *little_endian = (dtype_str[0] == '<' || dtype_str[0] == '|');

    /* Second char is type */
    char type_char = dtype_str[1];
    int type_size = atoi(&dtype_str[2]);

    switch (type_char) {
        case 'f':  /* Float */
            *dtype = 'f';
            *size = type_size;
            break;
        case 'i':  /* Signed integer */
        case 'u':  /* Unsigned integer */
            *dtype = 'i';
            *size = type_size;
            break;
        default:
            *dtype = type_char;
            *size = type_size;
    }

    return 0;
}

static int matches_name_list(const char *name, const char **list) {
    if (!name || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (strcasecmp(name, list[i]) == 0) return 1;
    }
    return 0;
}

#endif /* HAVE_ZARR */
