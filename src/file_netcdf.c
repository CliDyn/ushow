/*
 * file_netcdf.c - NetCDF file reading implementation
 */

#include "file_netcdf.h"
#include <netcdf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Dimension name patterns */
static const char *TIME_NAMES[] = {"time", "t", "Time", "TIME", NULL};
static const char *DEPTH_NAMES[] = {"depth", "z", "lev", "level", "nz", "nz1", "deptht", "depthu", "depthv", "depthw", NULL};
static const char *NODE_NAMES[] = {"nod2", "nod2d", "node", "nodes", "ncells", "npoints", "nod", "n2d", NULL};

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

        /* Skip 1D variables and coordinate variables */
        if (var_ndims < 2) continue;

        /* Get dimension info */
        size_t dim_sizes[MAX_DIMS];
        char dim_names[MAX_DIMS][MAX_NAME_LEN];
        int time_dim = -1;
        int depth_dim = -1;
        int node_dim = -1;

        for (int d = 0; d < var_ndims; d++) {
            nc_inq_dim(ncid, dimids[d], dim_names[d], &dim_sizes[d]);

            if (matches_name_list(dim_names[d], TIME_NAMES)) {
                time_dim = d;
            } else if (matches_name_list(dim_names[d], DEPTH_NAMES)) {
                depth_dim = d;
            } else if (matches_name_list(dim_names[d], NODE_NAMES)) {
                node_dim = d;
            }
        }

        /* For unstructured data, the last dimension should match mesh size */
        if (node_dim < 0) {
            /* Try to find node dimension by size match */
            for (int d = var_ndims - 1; d >= 0; d--) {
                if (dim_sizes[d] == mesh->n_points) {
                    node_dim = d;
                    break;
                }
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

    /* Sample a few time/depth slices */
    size_t n_times = (var->time_dim_id >= 0) ? var->dim_sizes[var->time_dim_id] : 1;
    size_t n_depths = (var->depth_dim_id >= 0) ? var->dim_sizes[var->depth_dim_id] : 1;

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
            if (fabsf(v) > 1e10f) continue;
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
