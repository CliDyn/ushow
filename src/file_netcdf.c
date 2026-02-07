/*
 * file_netcdf.c - NetCDF file reading implementation
 */

#define _GNU_SOURCE

#include "file_netcdf.h"
#include <netcdf.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

/* Dimension name patterns */
static const char *TIME_NAMES[] = {"time", "t", "Time", "TIME", NULL};
static const char *DEPTH_NAMES[] = {"depth", "z", "lev", "level", "nz", "nz1", "deptht", "depthu", "depthv", "depthw", NULL};
static const char *NODE_NAMES[] = {"nod2", "nod2d", "node", "nodes", "ncells", "npoints", "nod", "n2d", NULL};
static const char *LAT_NAMES[] = {"lat", "latitude", "y", "nlat", "rlat", "j", NULL};
static const char *LON_NAMES[] = {"lon", "longitude", "x", "nlon", "rlon", "i", NULL};

static int name_contains_ci(const char *name, const char *needle) {
    if (!name || !needle) return 0;
    return strcasestr(name, needle) != NULL;
}

static int name_starts_with_ci(const char *name, const char *prefix) {
    if (!name || !prefix) return 0;
    size_t len = strlen(prefix);
    return strncasecmp(name, prefix, len) == 0;
}

static int name_ends_with_ci(const char *name, const char *suffix) {
    if (!name || !suffix) return 0;
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > name_len) return 0;
    return strncasecmp(name + (name_len - suffix_len), suffix, suffix_len) == 0;
}

static int get_att_text(int ncid, int varid, const char *att, char *buf, size_t bufsize) {
    size_t len = 0;
    if (!buf || bufsize == 0) return 0;
    buf[0] = '\0';
    if (nc_inq_attlen(ncid, varid, att, &len) != NC_NOERR) return 0;
    if (len >= bufsize) len = bufsize - 1;
    if (nc_get_att_text(ncid, varid, att, buf) != NC_NOERR) return 0;
    buf[len] = '\0';
    return 1;
}

static int coord_var_is_time(int ncid, const char *dim_name) {
    int varid;
    char buf[256];
    if (nc_inq_varid(ncid, dim_name, &varid) != NC_NOERR) return 0;

    if (get_att_text(ncid, varid, "axis", buf, sizeof(buf)) && (buf[0] == 'T' || buf[0] == 't')) {
        return 1;
    }
    if (get_att_text(ncid, varid, "standard_name", buf, sizeof(buf)) && strcasecmp(buf, "time") == 0) {
        return 1;
    }
    if (get_att_text(ncid, varid, "units", buf, sizeof(buf)) && name_contains_ci(buf, "since")) {
        return 1;
    }
    if (get_att_text(ncid, varid, "long_name", buf, sizeof(buf)) && name_contains_ci(buf, "time")) {
        return 1;
    }
    return 0;
}

static int coord_var_is_depth(int ncid, const char *dim_name) {
    int varid;
    char buf[256];
    if (nc_inq_varid(ncid, dim_name, &varid) != NC_NOERR) return 0;

    if (get_att_text(ncid, varid, "axis", buf, sizeof(buf)) && (buf[0] == 'Z' || buf[0] == 'z')) {
        return 1;
    }
    if (get_att_text(ncid, varid, "standard_name", buf, sizeof(buf))) {
        if (strcasecmp(buf, "depth") == 0 || strcasecmp(buf, "altitude") == 0) return 1;
    }
    if (get_att_text(ncid, varid, "positive", buf, sizeof(buf)) &&
        (strcasecmp(buf, "down") == 0 || strcasecmp(buf, "up") == 0)) {
        return 1;
    }
    if (get_att_text(ncid, varid, "long_name", buf, sizeof(buf)) && name_contains_ci(buf, "depth")) {
        return 1;
    }
    if (get_att_text(ncid, varid, "units", buf, sizeof(buf))) {
        if (name_contains_ci(buf, "meter") || name_contains_ci(buf, "metre")) return 1;
    }
    return 0;
}

/* Check if a name matches any in a list */
static int matches_name_list(const char *name, const char **list) {
    for (int i = 0; list[i] != NULL; i++) {
        if (strcasecmp(name, list[i]) == 0) return 1;
    }
    return 0;
}

/* Check if dimension is likely a coordinate variable (not data) */
static int is_coord_dim(int ncid __attribute__((unused)), const char *dimname) {
    /* Common coordinate dimension names */
    static const char *COORD_DIMS[] = {
        "lon", "lat", "longitude", "latitude", "x", "y",
        "time", "t", "depth", "z", "lev", "level",
        NULL
    };
    return matches_name_list(dimname, COORD_DIMS);
}

/*
 * CF time unit parsing and conversion helpers.
 * Used to normalize time values when files have different epochs.
 */

static int parse_cf_time_units(const char *units, double *unit_seconds,
                               int *y, int *mo, int *d, int *h, int *mi, double *sec) {
    if (!units || !unit_seconds || !y || !mo || !d || !h || !mi || !sec) return 0;
    const char *since = strstr(units, "since");
    if (!since) return 0;

    char unit_buf[32] = {0};
    if (sscanf(units, "%31s", unit_buf) != 1) return 0;
    for (char *p = unit_buf; *p; ++p)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');

    if (strcmp(unit_buf, "seconds") == 0 || strcmp(unit_buf, "second") == 0 ||
        strcmp(unit_buf, "secs") == 0 || strcmp(unit_buf, "sec") == 0 || strcmp(unit_buf, "s") == 0)
        *unit_seconds = 1.0;
    else if (strcmp(unit_buf, "minutes") == 0 || strcmp(unit_buf, "minute") == 0 ||
             strcmp(unit_buf, "mins") == 0 || strcmp(unit_buf, "min") == 0)
        *unit_seconds = 60.0;
    else if (strcmp(unit_buf, "hours") == 0 || strcmp(unit_buf, "hour") == 0 ||
             strcmp(unit_buf, "hrs") == 0 || strcmp(unit_buf, "hr") == 0)
        *unit_seconds = 3600.0;
    else if (strcmp(unit_buf, "days") == 0 || strcmp(unit_buf, "day") == 0)
        *unit_seconds = 86400.0;
    else
        return 0;

    const char *p = since + 5;
    while (*p == ' ') p++;
    int n = sscanf(p, "%d-%d-%d %d:%d:%lf", y, mo, d, h, mi, sec);
    if (n < 3) return 0;
    if (n == 3) { *h = 0; *mi = 0; *sec = 0.0; }
    return 1;
}

static int64_t nc_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)(era * 146097 + (int)doe - 719468);
}

/*
 * Convert a CF time value from src_units to dst_units.
 * E.g., value=0 in "days since 1960-01-01" → 3652.0 in "days since 1950-01-01"
 * Returns the converted value, or the original if parsing fails.
 */
static double convert_time_units(double value, const char *src_units, const char *dst_units) {
    if (!src_units || !dst_units) return value;
    if (strcmp(src_units, dst_units) == 0) return value;

    double src_unit_sec, dst_unit_sec;
    int sy, smo, sd, sh, smi, dy, dmo, dd, dh, dmi;
    double ssec, dsec;

    if (!parse_cf_time_units(src_units, &src_unit_sec, &sy, &smo, &sd, &sh, &smi, &ssec))
        return value;
    if (!parse_cf_time_units(dst_units, &dst_unit_sec, &dy, &dmo, &dd, &dh, &dmi, &dsec))
        return value;

    /* Convert to absolute seconds */
    double src_epoch = (double)nc_days_from_civil(sy, (unsigned)smo, (unsigned)sd) * 86400.0
                     + sh * 3600.0 + smi * 60.0 + ssec;
    double abs_sec = src_epoch + value * src_unit_sec;

    /* Convert to destination units */
    double dst_epoch = (double)nc_days_from_civil(dy, (unsigned)dmo, (unsigned)dd) * 86400.0
                     + dh * 3600.0 + dmi * 60.0 + dsec;
    return (abs_sec - dst_epoch) / dst_unit_sec;
}

USFile *netcdf_open(const char *filename) {
    int status, ncid;

    status = nc_open(filename, NC_NOWRITE, &ncid);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error opening %s: %s\n", filename, nc_strerror(status));
        return NULL;
    }

    USFile *file = calloc(1, sizeof(USFile));
    if (!file) {
        nc_close(ncid);
        return NULL;
    }

    file->ncid = ncid;
    file->file_type = FILE_TYPE_NETCDF;
    strncpy(file->filename, filename, MAX_NAME_LEN - 1);

    return file;
}

USVar *netcdf_scan_variables(USFile *file, USMesh *mesh) {
    if (!file || !mesh) return NULL;

    int ncid = file->ncid;
    int nvars, ndims;
    nc_inq(ncid, &ndims, &nvars, NULL, NULL);

    USVar *var_list = NULL;
    USVar *var_tail = NULL;
    int var_count = 0;

    for (int varid = 0; varid < nvars; varid++) {
        char varname[MAX_NAME_LEN];
        nc_type vartype;
        int var_ndims;
        int dimids[MAX_DIMS];

        nc_inq_var(ncid, varid, varname, &vartype, &var_ndims, dimids, NULL);

        /* Skip 0D (scalar) variables */
        if (var_ndims < 1) continue;

        /* For 1D variables, only allow if they match unstructured mesh size
           and are not coordinate variables */
        if (var_ndims == 1) {
            size_t dim_size;
            char dim_name[MAX_NAME_LEN];
            nc_inq_dim(ncid, dimids[0], dim_name, &dim_size);

            /* Skip if it's a coordinate variable name */
            if (is_coord_dim(ncid, varname)) continue;

            /* Skip if dimension doesn't match mesh size */
            if (dim_size != mesh->n_points) continue;

            /* Skip if mesh is not unstructured */
            if (mesh->coord_type != COORD_TYPE_1D_UNSTRUCTURED) continue;
        }

        /* Get dimension info */
        size_t dim_sizes[MAX_DIMS];
        char dim_names[MAX_DIMS][MAX_NAME_LEN];
        int time_dim = -1;
        int depth_dim = -1;
        int node_dim = -1;
        int lat_dim = -1;
        int lon_dim = -1;

        for (int d = 0; d < var_ndims; d++) {
            nc_inq_dim(ncid, dimids[d], dim_names[d], &dim_sizes[d]);

            if (matches_name_list(dim_names[d], TIME_NAMES) ||
                name_contains_ci(dim_names[d], "time")) {
                time_dim = d;
            } else if (matches_name_list(dim_names[d], DEPTH_NAMES) ||
                       name_contains_ci(dim_names[d], "depth") ||
                       name_contains_ci(dim_names[d], "lev") ||
                       strcasecmp(dim_names[d], "z") == 0 ||
                       name_starts_with_ci(dim_names[d], "z_") ||
                       name_ends_with_ci(dim_names[d], "_z")) {
                depth_dim = d;
            } else if (matches_name_list(dim_names[d], NODE_NAMES)) {
                node_dim = d;
            } else if (matches_name_list(dim_names[d], LAT_NAMES)) {
                lat_dim = d;
            } else if (matches_name_list(dim_names[d], LON_NAMES)) {
                lon_dim = d;
            }
        }

        /* Fallback: infer time/depth from coordinate variable attributes */
        if (time_dim < 0 || depth_dim < 0) {
            for (int d = 0; d < var_ndims; d++) {
                if (d == lat_dim || d == lon_dim || d == node_dim) continue;
                if (time_dim < 0 && coord_var_is_time(ncid, dim_names[d])) {
                    time_dim = d;
                }
                if (depth_dim < 0 && coord_var_is_depth(ncid, dim_names[d])) {
                    depth_dim = d;
                }
            }
        }

        /* For unstructured data, find node dimension by name or size match */
        if (node_dim < 0) {
            for (int d = var_ndims - 1; d >= 0; d--) {
                if (dim_sizes[d] == mesh->n_points) {
                    node_dim = d;
                    break;
                }
            }
        }

        /* For structured data, check if lat*lon = n_points */
        if (node_dim < 0 && lat_dim >= 0 && lon_dim >= 0) {
            if (dim_sizes[lat_dim] * dim_sizes[lon_dim] == mesh->n_points) {
                /* Use the last spatial dimension as "node_dim" for reading */
                node_dim = (lat_dim > lon_dim) ? lat_dim : lon_dim;
            }
        }

        /* Skip if no spatial dimension found */
        if (node_dim < 0) continue;

        /* Skip if it looks like a coordinate variable */
        if (is_coord_dim(ncid, varname)) continue;

        /* Create variable entry */
        USVar *var = calloc(1, sizeof(USVar));
        if (!var) continue;

        strncpy(var->name, varname, MAX_NAME_LEN - 1);
        var->name[MAX_NAME_LEN - 1] = '\0';
        var->n_dims = var_ndims;
        var->varid = varid;
        var->file = file;
        var->mesh = mesh;
        var->time_dim_id = time_dim;
        var->depth_dim_id = depth_dim;
        var->node_dim_id = node_dim;
        var->fill_value = DEFAULT_FILL_VALUE;

        for (int d = 0; d < var_ndims; d++) {
            var->dim_sizes[d] = dim_sizes[d];
            strncpy(var->dim_names[d], dim_names[d], MAX_NAME_LEN - 1);
        }

        /* Get long_name and units attributes */
        nc_get_att_text(ncid, varid, "long_name", var->long_name);
        nc_get_att_text(ncid, varid, "units", var->units);

        /* Get fill value */
        float fv;
        if (nc_get_att_float(ncid, varid, "_FillValue", &fv) == NC_NOERR) {
            var->fill_value = fv;
        } else if (nc_get_att_float(ncid, varid, "missing_value", &fv) == NC_NOERR) {
            var->fill_value = fv;
        }

        /* Add to list */
        if (!var_list) {
            var_list = var;
        } else {
            var_tail->next = var;
        }
        var_tail = var;
        var_count++;

        printf("Found variable: %s [", varname);
        for (int d = 0; d < var_ndims; d++) {
            printf("%s%s=%zu", d > 0 ? ", " : "", dim_names[d], dim_sizes[d]);
        }
        printf("]");
        if (time_dim >= 0) printf(" (time=%d)", time_dim);
        if (depth_dim >= 0) printf(" (depth=%d)", depth_dim);
        if (node_dim >= 0) printf(" (node=%d)", node_dim);
        printf("\n");
    }

    file->vars = var_list;
    file->n_vars = var_count;
    printf("Found %d displayable variables\n", var_count);

    return var_list;
}

int netcdf_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data) {
    if (!var || !var->file || !data) return -1;

    int ncid = var->file->ncid;
    size_t start[MAX_DIMS] = {0};
    size_t count[MAX_DIMS];

    /* Set up hyperslab */
    for (int d = 0; d < var->n_dims; d++) {
        if (d == var->time_dim_id) {
            start[d] = time_idx;
            count[d] = 1;
        } else if (d == var->depth_dim_id) {
            start[d] = depth_idx;
            count[d] = 1;
        } else {
            start[d] = 0;
            count[d] = var->dim_sizes[d];
        }
    }

    /* Read data */
    int status = nc_get_vara_float(ncid, var->varid, start, count, data);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error reading %s: %s\n", var->name, nc_strerror(status));
        return -1;
    }

    /* Apply scale_factor and add_offset if present */
    float scale = 1.0f, offset = 0.0f;
    nc_get_att_float(ncid, var->varid, "scale_factor", &scale);
    nc_get_att_float(ncid, var->varid, "add_offset", &offset);

    if (scale != 1.0f || offset != 0.0f) {
        for (size_t i = 0; i < var->mesh->n_points; i++) {
            if (fabsf(data[i] - var->fill_value) > 1e-6f * fabsf(var->fill_value)) {
                data[i] = data[i] * scale + offset;
            }
        }
    }

    return 0;
}

int netcdf_estimate_range(USVar *var, float *min_val, float *max_val) {
    if (!var || !var->mesh) return -1;

    size_t n_points = var->mesh->n_points;
    float *data = malloc(n_points * sizeof(float));
    if (!data) return -1;

    float global_min = 1e30f;
    float global_max = -1e30f;

    /* Sample a few time slices at surface depth */
    size_t n_times = (var->time_dim_id >= 0) ? var->dim_sizes[var->time_dim_id] : 1;

    /* Sample first, middle, last time at surface */
    size_t sample_times[] = {0, n_times / 2, n_times - 1};
    int n_samples = (n_times > 2) ? 3 : (int)n_times;

    for (int t = 0; t < n_samples; t++) {
        size_t time_idx = sample_times[t];
        if (time_idx >= n_times) continue;

        if (netcdf_read_slice(var, time_idx, 0, data) != 0) continue;

        for (size_t i = 0; i < n_points; i++) {
            float v = data[i];
            /* Skip fill values */
            if (fabsf(v) > INVALID_DATA_THRESHOLD) continue;
            if (fabsf(v - var->fill_value) < 1e-6f * fabsf(var->fill_value)) continue;

            if (v < global_min) global_min = v;
            if (v > global_max) global_max = v;
        }
    }

    free(data);

    if (global_min > global_max) {
        /* No valid data found */
        *min_val = 0.0f;
        *max_val = 1.0f;
        return -1;
    }

    *min_val = global_min;
    *max_val = global_max;

    printf("Estimated range for %s: [%.4f, %.4f]\n", var->name, global_min, global_max);

    return 0;
}

void netcdf_close(USFile *file) {
    if (!file) return;

    /* Free variables */
    USVar *var = file->vars;
    while (var) {
        USVar *next = var->next;
        free(var);
        var = next;
    }

    if (file->ncid >= 0) {
        nc_close(file->ncid);
    }

    free(file);
}

USDimInfo *netcdf_get_dim_info(USVar *var, int *n_dims_out) {
    if (!var || !var->file || !n_dims_out) return NULL;

    int ncid = var->file->ncid;

    /* Count non-spatial dimensions that are scannable (time, depth) */
    int n_scannable = 0;
    for (int d = 0; d < var->n_dims; d++) {
        if (d == var->time_dim_id || d == var->depth_dim_id) {
            n_scannable++;
        }
    }

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
        /* Only include time and depth dimensions */
        if (d != var->time_dim_id && d != var->depth_dim_id) continue;

        USDimInfo *di = &dims[idx];
        strncpy(di->name, var->dim_names[d], MAX_NAME_LEN - 1);
        di->size = var->dim_sizes[d];
        di->current = (d == var->time_dim_id) ? 0 : 0;  /* Will be updated by caller */
        di->is_scannable = (di->size > 1);

        /* Try to find a coordinate variable with the same name as the dimension */
        int coord_varid;
        if (nc_inq_varid(ncid, var->dim_names[d], &coord_varid) == NC_NOERR) {
            /* Get units attribute */
            nc_get_att_text(ncid, coord_varid, "units", di->units);

            /* Read all coordinate values */
            di->values = malloc(di->size * sizeof(double));
            if (di->values) {
                int status = nc_get_var_double(ncid, coord_varid, di->values);
                if (status == NC_NOERR && di->size > 0) {
                    di->min_val = di->values[0];
                    di->max_val = di->values[di->size - 1];
                } else {
                    /* Fall back to indices */
                    free(di->values);
                    di->values = NULL;
                    di->min_val = 0;
                    di->max_val = (double)(di->size - 1);
                }
            }
        } else {
            /* No coordinate variable, use indices */
            di->values = NULL;
            di->min_val = 0;
            di->max_val = (double)(di->size - 1);
        }

        idx++;
    }

    *n_dims_out = n_scannable;
    return dims;
}

void netcdf_free_dim_info(USDimInfo *dims, int n_dims) {
    if (!dims) return;
    for (int i = 0; i < n_dims; i++) {
        free(dims[i].values);
    }
    free(dims);
}

/*
 * Multi-file support implementation
 */

#include <glob.h>

/* Comparison function for sorting filenames */
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

USFileSet *netcdf_open_fileset(const char **filenames, int n_files) {
    if (!filenames || n_files <= 0) return NULL;

    USFileSet *fs = calloc(1, sizeof(USFileSet));
    if (!fs) return NULL;

    /* Allocate arrays */
    fs->files = calloc(n_files, sizeof(USFile *));
    fs->time_offsets = calloc(n_files + 1, sizeof(size_t));
    if (!fs->files || !fs->time_offsets) {
        free(fs->files);
        free(fs->time_offsets);
        free(fs);
        return NULL;
    }

    /* Create sorted copy of filenames */
    char **sorted_names = malloc(n_files * sizeof(char *));
    if (!sorted_names) {
        free(fs->files);
        free(fs->time_offsets);
        free(fs);
        return NULL;
    }
    for (int i = 0; i < n_files; i++) {
        sorted_names[i] = strdup(filenames[i]);
    }
    qsort(sorted_names, n_files, sizeof(char *), compare_strings);

    /* Open each file and count time steps */
    fs->time_offsets[0] = 0;
    for (int i = 0; i < n_files; i++) {
        printf("Opening file %d/%d: %s\n", i + 1, n_files, sorted_names[i]);
        fs->files[i] = netcdf_open(sorted_names[i]);
        if (!fs->files[i]) {
            fprintf(stderr, "Failed to open file: %s\n", sorted_names[i]);
            /* Cleanup */
            for (int j = 0; j < i; j++) {
                netcdf_close(fs->files[j]);
            }
            for (int j = 0; j < n_files; j++) {
                free(sorted_names[j]);
            }
            free(sorted_names);
            free(fs->files);
            free(fs->time_offsets);
            free(fs);
            return NULL;
        }

        /* Get time dimension size from this file */
        int ncid = fs->files[i]->ncid;
        int time_dimid;
        size_t time_size = 0;

        /* Try common time dimension names */
        for (const char **name = TIME_NAMES; *name; name++) {
            if (nc_inq_dimid(ncid, *name, &time_dimid) == NC_NOERR) {
                nc_inq_dimlen(ncid, time_dimid, &time_size);
                break;
            }
        }

        /* Also try to find unlimited dimension */
        if (time_size == 0) {
            int unlim_dimid;
            if (nc_inq_unlimdim(ncid, &unlim_dimid) == NC_NOERR && unlim_dimid >= 0) {
                nc_inq_dimlen(ncid, unlim_dimid, &time_size);
            }
        }

        if (time_size == 0) {
            time_size = 1;  /* Assume single time step */
        }

        fs->time_offsets[i + 1] = fs->time_offsets[i] + time_size;
        printf("  File %d: %zu time steps (offset %zu)\n", i, time_size, fs->time_offsets[i]);
    }

    fs->n_files = n_files;
    fs->total_times = fs->time_offsets[n_files];
    fs->base_filename = strdup(sorted_names[0]);

    printf("Total virtual time steps: %zu across %d files\n", fs->total_times, n_files);

    /* Cleanup sorted names */
    for (int i = 0; i < n_files; i++) {
        free(sorted_names[i]);
    }
    free(sorted_names);

    return fs;
}

USFileSet *netcdf_open_glob(const char *pattern) {
    if (!pattern) return NULL;

    glob_t glob_result;
    int ret = glob(pattern, GLOB_TILDE | GLOB_NOSORT, NULL, &glob_result);

    if (ret != 0) {
        if (ret == GLOB_NOMATCH) {
            fprintf(stderr, "No files match pattern: %s\n", pattern);
        } else {
            fprintf(stderr, "Glob error for pattern: %s\n", pattern);
        }
        return NULL;
    }

    if (glob_result.gl_pathc == 0) {
        fprintf(stderr, "No files match pattern: %s\n", pattern);
        globfree(&glob_result);
        return NULL;
    }

    printf("Pattern '%s' matched %zu files\n", pattern, glob_result.gl_pathc);

    USFileSet *fs = netcdf_open_fileset((const char **)glob_result.gl_pathv,
                                        (int)glob_result.gl_pathc);
    globfree(&glob_result);

    return fs;
}

int netcdf_fileset_map_time(USFileSet *fs, size_t virtual_time,
                            int *file_idx_out, size_t *local_time_out) {
    if (!fs || !file_idx_out || !local_time_out) return -1;
    if (virtual_time >= fs->total_times) return -1;

    /* Binary search for the file containing this time step */
    int lo = 0, hi = fs->n_files - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (fs->time_offsets[mid] <= virtual_time) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    *file_idx_out = lo;
    *local_time_out = virtual_time - fs->time_offsets[lo];
    return 0;
}

int netcdf_read_slice_fileset(USFileSet *fs, USVar *var,
                              size_t virtual_time, size_t depth_idx, float *data) {
    if (!fs || !var || !data) return -1;

    int file_idx;
    size_t local_time;
    if (netcdf_fileset_map_time(fs, virtual_time, &file_idx, &local_time) != 0) {
        fprintf(stderr, "Invalid virtual time index: %zu\n", virtual_time);
        return -1;
    }

    /* Get the file and find matching variable */
    USFile *file = fs->files[file_idx];

    /* For files other than the first, we need to find the variable by name */
    int varid = var->varid;
    if (file_idx > 0) {
        if (nc_inq_varid(file->ncid, var->name, &varid) != NC_NOERR) {
            fprintf(stderr, "Variable '%s' not found in file %d\n", var->name, file_idx);
            return -1;
        }
    }

    /* Build hyperslab for this file */
    size_t start[MAX_DIMS] = {0};
    size_t count[MAX_DIMS];

    for (int d = 0; d < var->n_dims; d++) {
        if (d == var->time_dim_id) {
            start[d] = local_time;
            count[d] = 1;
        } else if (d == var->depth_dim_id) {
            start[d] = depth_idx;
            count[d] = 1;
        } else {
            start[d] = 0;
            count[d] = var->dim_sizes[d];
        }
    }

    /* Read data */
    int status = nc_get_vara_float(file->ncid, varid, start, count, data);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error reading %s from file %d: %s\n",
                var->name, file_idx, nc_strerror(status));
        return -1;
    }

    /* Apply scale_factor and add_offset if present */
    float scale = 1.0f, offset = 0.0f;
    nc_get_att_float(file->ncid, varid, "scale_factor", &scale);
    nc_get_att_float(file->ncid, varid, "add_offset", &offset);

    if (scale != 1.0f || offset != 0.0f) {
        for (size_t i = 0; i < var->mesh->n_points; i++) {
            if (fabsf(data[i] - var->fill_value) > 1e-6f * fabsf(var->fill_value)) {
                data[i] = data[i] * scale + offset;
            }
        }
    }

    return 0;
}

size_t netcdf_fileset_total_times(USFileSet *fs) {
    return fs ? fs->total_times : 0;
}

USDimInfo *netcdf_get_dim_info_fileset(USFileSet *fs, USVar *var, int *n_dims_out) {
    if (!fs || !var || !n_dims_out || fs->n_files == 0) return NULL;

    /* Get base dimension info from the variable */
    USDimInfo *dims = netcdf_get_dim_info(var, n_dims_out);
    if (!dims) return NULL;

    /* Update time dimension with virtual total and concatenated values */
    for (int i = 0; i < *n_dims_out; i++) {
        if (var->time_dim_id >= 0 &&
            strcmp(dims[i].name, var->dim_names[var->time_dim_id]) == 0) {
            /* This is the time dimension - update with virtual info */
            double *old_values = dims[i].values;

            dims[i].size = fs->total_times;
            dims[i].is_scannable = (fs->total_times > 1);

            /* Allocate space for all time values */
            dims[i].values = malloc(fs->total_times * sizeof(double));
            if (dims[i].values) {
                size_t offset = 0;

                /* Reference units from file 0 (stored in dims[i].units) */
                const char *ref_units = dims[i].units;

                /* Collect time values from all files, normalizing to file 0 units */
                for (int f = 0; f < fs->n_files; f++) {
                    int ncid = fs->files[f]->ncid;
                    int coord_varid;
                    size_t file_times = fs->time_offsets[f + 1] - fs->time_offsets[f];

                    if (nc_inq_varid(ncid, dims[i].name, &coord_varid) == NC_NOERR) {
                        double *file_values = malloc(file_times * sizeof(double));
                        if (file_values) {
                            if (nc_get_var_double(ncid, coord_varid, file_values) == NC_NOERR) {
                                /* Read this file's time units */
                                char file_units[MAX_NAME_LEN] = {0};
                                get_att_text(ncid, coord_varid, "units",
                                             file_units, sizeof(file_units));

                                /* Convert to reference units if different */
                                for (size_t t = 0; t < file_times; t++) {
                                    dims[i].values[offset + t] =
                                        convert_time_units(file_values[t],
                                                           file_units, ref_units);
                                }
                            } else {
                                for (size_t t = 0; t < file_times; t++) {
                                    dims[i].values[offset + t] = (double)(offset + t);
                                }
                            }
                            free(file_values);
                        }
                    } else {
                        for (size_t t = 0; t < file_times; t++) {
                            dims[i].values[offset + t] = (double)(offset + t);
                        }
                    }
                    offset += file_times;
                }

                dims[i].min_val = dims[i].values[0];
                dims[i].max_val = dims[i].values[fs->total_times - 1];
            }

            free(old_values);
            break;
        }
    }

    return dims;
}

int netcdf_read_timeseries(USVar *var, size_t node_idx, size_t depth_idx,
                           double **times_out, float **values_out,
                           int **valid_out, size_t *n_out) {
    if (!var || !var->file || !var->mesh || !times_out || !values_out || !valid_out || !n_out)
        return -1;

    *times_out = NULL;
    *values_out = NULL;
    *valid_out = NULL;
    *n_out = 0;

    int ncid = var->file->ncid;
    size_t n_times = (var->time_dim_id >= 0) ? var->dim_sizes[var->time_dim_id] : 1;
    if (n_times == 0) return -1;

    /* Allocate output arrays */
    double *times = calloc(n_times, sizeof(double));
    float *values = calloc(n_times, sizeof(float));
    int *valid = calloc(n_times, sizeof(int));
    if (!times || !values || !valid) {
        free(times); free(values); free(valid);
        return -1;
    }

    /* Decompose node_idx for structured grids (lat/lon dimensions) */
    size_t lat_idx = 0, lon_idx = 0;
    int is_structured = 0;
    if (var->mesh->coord_type != COORD_TYPE_1D_UNSTRUCTURED &&
        var->mesh->orig_nx > 0 && var->mesh->orig_ny > 0) {
        is_structured = 1;
        lon_idx = node_idx % var->mesh->orig_nx;
        lat_idx = node_idx / var->mesh->orig_nx;
    }

    /* Get scale_factor and add_offset */
    float scale = 1.0f, offset = 0.0f;
    nc_get_att_float(ncid, var->varid, "scale_factor", &scale);
    nc_get_att_float(ncid, var->varid, "add_offset", &offset);

    /* Read values for each time step using a single-point hyperslab per step */
    for (size_t t = 0; t < n_times; t++) {
        size_t start[MAX_DIMS] = {0};
        size_t count[MAX_DIMS];
        float val;

        for (int d = 0; d < var->n_dims; d++) {
            if (d == var->time_dim_id) {
                start[d] = t;
                count[d] = 1;
            } else if (d == var->depth_dim_id) {
                start[d] = depth_idx;
                count[d] = 1;
            } else if (is_structured) {
                /* Find if this is the lat or lon dimension */
                if (matches_name_list(var->dim_names[d], LAT_NAMES)) {
                    start[d] = lat_idx;
                    count[d] = 1;
                } else if (matches_name_list(var->dim_names[d], LON_NAMES)) {
                    start[d] = lon_idx;
                    count[d] = 1;
                } else {
                    start[d] = node_idx;
                    count[d] = 1;
                }
            } else {
                /* Unstructured: node dimension */
                start[d] = node_idx;
                count[d] = 1;
            }
        }

        int status = nc_get_vara_float(ncid, var->varid, start, count, &val);
        if (status != NC_NOERR) {
            values[t] = var->fill_value;
            valid[t] = 0;
            continue;
        }

        /* Apply scale_factor/add_offset */
        if (fabsf(val - var->fill_value) < 1e-6f * fabsf(var->fill_value) ||
            fabsf(val) > INVALID_DATA_THRESHOLD) {
            values[t] = var->fill_value;
            valid[t] = 0;
        } else {
            if (scale != 1.0f || offset != 0.0f)
                val = val * scale + offset;
            values[t] = val;
            valid[t] = 1;
        }
    }

    /* Read time coordinate values */
    if (var->time_dim_id >= 0) {
        int coord_varid;
        if (nc_inq_varid(ncid, var->dim_names[var->time_dim_id], &coord_varid) == NC_NOERR) {
            nc_get_var_double(ncid, coord_varid, times);
        } else {
            for (size_t t = 0; t < n_times; t++)
                times[t] = (double)t;
        }
    } else {
        times[0] = 0.0;
    }

    *times_out = times;
    *values_out = values;
    *valid_out = valid;
    *n_out = n_times;
    return 0;
}

int netcdf_read_timeseries_fileset(USFileSet *fs, USVar *var,
                                   size_t node_idx, size_t depth_idx,
                                   double **times_out, float **values_out,
                                   int **valid_out, size_t *n_out) {
    if (!fs || !var || !times_out || !values_out || !valid_out || !n_out)
        return -1;

    *times_out = NULL;
    *values_out = NULL;
    *valid_out = NULL;
    *n_out = 0;

    size_t total = fs->total_times;
    if (total == 0) return -1;

    double *times = calloc(total, sizeof(double));
    float *values = calloc(total, sizeof(float));
    int *valid = calloc(total, sizeof(int));
    if (!times || !values || !valid) {
        free(times); free(values); free(valid);
        return -1;
    }

    /* Decompose node_idx for structured grids */
    size_t lat_idx = 0, lon_idx = 0;
    int is_structured = 0;
    if (var->mesh->coord_type != COORD_TYPE_1D_UNSTRUCTURED &&
        var->mesh->orig_nx > 0 && var->mesh->orig_ny > 0) {
        is_structured = 1;
        lon_idx = node_idx % var->mesh->orig_nx;
        lat_idx = node_idx / var->mesh->orig_nx;
    }

    /* Get reference time units from file 0 */
    char ref_time_units[MAX_NAME_LEN] = {0};
    if (var->time_dim_id >= 0) {
        int coord_varid;
        if (nc_inq_varid(fs->files[0]->ncid, var->dim_names[var->time_dim_id],
                         &coord_varid) == NC_NOERR) {
            get_att_text(fs->files[0]->ncid, coord_varid, "units",
                         ref_time_units, sizeof(ref_time_units));
        }
    }

    size_t out_idx = 0;
    for (int f = 0; f < fs->n_files; f++) {
        USFile *file = fs->files[f];
        int ncid = file->ncid;
        size_t file_times = fs->time_offsets[f + 1] - fs->time_offsets[f];

        /* Find variable in this file */
        int varid = var->varid;
        if (f > 0) {
            if (nc_inq_varid(ncid, var->name, &varid) != NC_NOERR) {
                /* Variable not found in this file, fill with invalid */
                for (size_t t = 0; t < file_times; t++) {
                    times[out_idx + t] = (double)(out_idx + t);
                    values[out_idx + t] = var->fill_value;
                    valid[out_idx + t] = 0;
                }
                out_idx += file_times;
                continue;
            }
        }

        /* Get scale_factor and add_offset for this file */
        float scale = 1.0f, offset = 0.0f;
        nc_get_att_float(ncid, varid, "scale_factor", &scale);
        nc_get_att_float(ncid, varid, "add_offset", &offset);

        /* Read time coordinate from this file, normalizing to file 0 units */
        if (var->time_dim_id >= 0) {
            int coord_varid;
            if (nc_inq_varid(ncid, var->dim_names[var->time_dim_id], &coord_varid) == NC_NOERR) {
                double *file_times_vals = malloc(file_times * sizeof(double));
                if (file_times_vals) {
                    if (nc_get_var_double(ncid, coord_varid, file_times_vals) == NC_NOERR) {
                        /* Read this file's time units and convert if needed */
                        char file_units[MAX_NAME_LEN] = {0};
                        get_att_text(ncid, coord_varid, "units",
                                     file_units, sizeof(file_units));
                        for (size_t t = 0; t < file_times; t++) {
                            times[out_idx + t] =
                                convert_time_units(file_times_vals[t],
                                                   file_units, ref_time_units);
                        }
                    } else {
                        for (size_t t = 0; t < file_times; t++)
                            times[out_idx + t] = (double)(out_idx + t);
                    }
                    free(file_times_vals);
                }
            } else {
                for (size_t t = 0; t < file_times; t++)
                    times[out_idx + t] = (double)(out_idx + t);
            }
        }

        /* Read value at each time step */
        for (size_t t = 0; t < file_times; t++) {
            size_t start[MAX_DIMS] = {0};
            size_t count[MAX_DIMS];
            float val;

            for (int d = 0; d < var->n_dims; d++) {
                if (d == var->time_dim_id) {
                    start[d] = t;
                    count[d] = 1;
                } else if (d == var->depth_dim_id) {
                    start[d] = depth_idx;
                    count[d] = 1;
                } else if (is_structured) {
                    if (matches_name_list(var->dim_names[d], LAT_NAMES)) {
                        start[d] = lat_idx;
                        count[d] = 1;
                    } else if (matches_name_list(var->dim_names[d], LON_NAMES)) {
                        start[d] = lon_idx;
                        count[d] = 1;
                    } else {
                        start[d] = node_idx;
                        count[d] = 1;
                    }
                } else {
                    start[d] = node_idx;
                    count[d] = 1;
                }
            }

            int status = nc_get_vara_float(ncid, varid, start, count, &val);
            if (status != NC_NOERR) {
                values[out_idx + t] = var->fill_value;
                valid[out_idx + t] = 0;
                continue;
            }

            if (fabsf(val - var->fill_value) < 1e-6f * fabsf(var->fill_value) ||
                fabsf(val) > INVALID_DATA_THRESHOLD) {
                values[out_idx + t] = var->fill_value;
                valid[out_idx + t] = 0;
            } else {
                if (scale != 1.0f || offset != 0.0f)
                    val = val * scale + offset;
                values[out_idx + t] = val;
                valid[out_idx + t] = 1;
            }
        }

        out_idx += file_times;
    }

    *times_out = times;
    *values_out = values;
    *valid_out = valid;
    *n_out = total;
    return 0;
}

void netcdf_close_fileset(USFileSet *fs) {
    if (!fs) return;

    for (int i = 0; i < fs->n_files; i++) {
        if (fs->files[i]) {
            netcdf_close(fs->files[i]);
        }
    }

    free(fs->files);
    free(fs->time_offsets);
    free(fs->base_filename);
    free(fs);
}
