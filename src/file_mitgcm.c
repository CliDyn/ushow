/*
 * file_mitgcm.c - MITgcm MDS binary data reading
 *
 * Reads MITgcm .data/.meta file pairs. The .meta files are ASCII with
 * MATLAB-style syntax; .data files are raw big-endian IEEE float arrays.
 */

#include "file_mitgcm.h"
#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

/* Internal structures */

typedef struct {
    int n_dims;
    int dims[MAX_DIMS];          /* C-order dimensions (reversed from Fortran) */
    char dataprec[32];           /* "float32" or "float64" */
    int n_records;
    int n_fields;
    char fields[MAX_VARS][16];   /* field names (stripped) */
    float missing_value;
    int has_missing;
    int timestep_number;
    int has_timestep;
} MetaInfo;

typedef struct {
    char directory[MAX_NAME_LEN];
    int nx, ny, nz;
    float *depth_values;         /* RC[nz] */
    float *hfac;                 /* hFacC[nz*ny*nx], NULL if unavailable */
    float missing_value;
} MitgcmStore;

typedef struct {
    char prefix[MAX_NAME_LEN];   /* e.g. "diags3D" */
    int field_index;             /* index within multi-field file */
    int *iterations;             /* sorted iteration numbers */
    int n_iterations;
    int is_3d;
    int nx, ny, nz;
    float missing_value;
    int has_missing;
} MitgcmVarData;

/* ---- Byte swapping ---- */

static int is_little_endian(void) {
    uint32_t x = 1;
    return *(unsigned char *)&x;
}

static void swap_endian_float(float *data, size_t n) {
    uint32_t *p = (uint32_t *)data;
    for (size_t i = 0; i < n; i++) {
        uint32_t v = p[i];
        p[i] = ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
               ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    }
}

/* ---- Meta file parsing ---- */

/* Skip whitespace */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Find value after "key = " in buffer. Returns pointer after '=' or NULL. */
static const char *find_key(const char *buf, const char *key) {
    const char *p = buf;
    size_t klen = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        const char *after = p + klen;
        after = skip_ws(after);
        if (*after == '=') {
            return skip_ws(after + 1);
        }
        p = after;
    }
    return NULL;
}

/* Parse integer from "[ NNN ]" or "[ NNN ;" */
static int parse_bracket_int(const char *p, int *val) {
    p = skip_ws(p);
    if (*p != '[') return 0;
    p = skip_ws(p + 1);
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *val = (int)v;
    return 1;
}

/* Parse float from "[ NNN ]" */
static int parse_bracket_float(const char *p, float *val) {
    p = skip_ws(p);
    if (*p != '[') return 0;
    p = skip_ws(p + 1);
    char *end;
    double v = strtod(p, &end);
    if (end == p) return 0;
    *val = (float)v;
    return 1;
}

/* Parse a string from "[ 'value' ]" */
static int parse_bracket_string(const char *p, char *out, size_t outlen) {
    p = skip_ws(p);
    if (*p != '[') return 0;
    p = skip_ws(p + 1);
    if (*p != '\'') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '\'' && i < outlen - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

static int parse_meta(const char *meta_path, MetaInfo *info) {
    memset(info, 0, sizeof(*info));
    info->missing_value = -999.0f;

    FILE *fp = fopen(meta_path, "r");
    if (!fp) return -1;

    /* Read entire file */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 100000) { fclose(fp); return -1; }

    char *buf = malloc(fsize + 1);
    if (!buf) { fclose(fp); return -1; }
    size_t nread = fread(buf, 1, fsize, fp);
    fclose(fp);
    buf[nread] = '\0';

    /* nDims */
    const char *p = find_key(buf, "nDims");
    if (p) parse_bracket_int(p, &info->n_dims);

    /* dimList - parse triplets, take first value of each triplet */
    p = find_key(buf, "dimList");
    if (p) {
        p = skip_ws(p);
        if (*p == '[') p++;
        int fortran_dims[MAX_DIMS];
        int nd = 0;
        while (nd < info->n_dims && nd < MAX_DIMS) {
            p = skip_ws(p);
            char *end;
            long v1 = strtol(p, &end, 10);
            if (end == p) break;
            fortran_dims[nd] = (int)v1;
            /* Skip the other two values in the triplet */
            p = end;
            for (int skip = 0; skip < 2; skip++) {
                while (*p && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                strtol(p, &end, 10);
                p = end;
            }
            while (*p && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            nd++;
        }
        /* Reverse Fortran order to C order */
        for (int i = 0; i < nd; i++) {
            info->dims[i] = fortran_dims[nd - 1 - i];
        }
    }

    /* dataprec */
    p = find_key(buf, "dataprec");
    if (p) parse_bracket_string(p, info->dataprec, sizeof(info->dataprec));

    /* nrecords */
    p = find_key(buf, "nrecords");
    if (p) parse_bracket_int(p, &info->n_records);

    /* timeStepNumber */
    p = find_key(buf, "timeStepNumber");
    if (p && parse_bracket_int(p, &info->timestep_number)) {
        info->has_timestep = 1;
    }

    /* missingValue */
    p = find_key(buf, "missingValue");
    if (p && parse_bracket_float(p, &info->missing_value)) {
        info->has_missing = 1;
    }

    /* nFlds */
    p = find_key(buf, "nFlds");
    if (p) parse_bracket_int(p, &info->n_fields);

    /* fldList */
    p = find_key(buf, "fldList");
    if (p) {
        /* Skip to opening brace */
        while (*p && *p != '{') p++;
        if (*p == '{') p++;

        int fi = 0;
        while (fi < info->n_fields && fi < MAX_VARS) {
            p = skip_ws(p);
            if (*p == '}' || *p == '\0') break;
            if (*p == '\'') {
                p++;
                size_t j = 0;
                while (*p && *p != '\'' && j < 15) {
                    info->fields[fi][j++] = *p++;
                }
                info->fields[fi][j] = '\0';
                if (*p == '\'') p++;
                /* Trim trailing spaces */
                while (j > 0 && info->fields[fi][j - 1] == ' ') {
                    info->fields[fi][--j] = '\0';
                }
                fi++;
            } else {
                p++;
            }
        }
    }

    free(buf);
    return 0;
}

/* ---- Binary reading helpers ---- */

static float *read_binary_floats(const char *path, size_t offset, size_t count) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    float *data = malloc(count * sizeof(float));
    if (!data) { fclose(fp); return NULL; }

    if (offset > 0) fseek(fp, (long)offset, SEEK_SET);

    size_t nread = fread(data, sizeof(float), count, fp);
    fclose(fp);

    if (nread != count) {
        free(data);
        return NULL;
    }

    if (is_little_endian()) {
        swap_endian_float(data, count);
    }

    return data;
}

/* ---- Path helpers ---- */

static int is_directory(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

/* Build companion .meta path from .data path */
static void data_to_meta_path(const char *data_path, char *meta_path, size_t len) {
    strncpy(meta_path, data_path, len - 1);
    meta_path[len - 1] = '\0';
    size_t slen = strlen(meta_path);
    if (slen > 5 && strcmp(meta_path + slen - 5, ".data") == 0) {
        strcpy(meta_path + slen - 5, ".meta");
    }
}

/* ---- Public API ---- */

int mitgcm_is_mitgcm(const char *path) {
    if (!path) return 0;

    /* If it's a .data file, check for companion .meta */
    size_t plen = strlen(path);
    if (plen > 5 && strcmp(path + plen - 5, ".data") == 0) {
        char meta[MAX_NAME_LEN];
        data_to_meta_path(path, meta, sizeof(meta));
        return file_exists(meta);
    }

    /* If it's a directory, check for grid meta files or any .meta */
    if (is_directory(path)) {
        char test[MAX_NAME_LEN];
        snprintf(test, sizeof(test), "%s/XC.meta", path);
        if (file_exists(test)) return 1;

        /* Check for any .meta file with iteration number pattern */
        DIR *dir = opendir(path);
        if (!dir) return 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t nlen = strlen(entry->d_name);
            if (nlen > 5 && strcmp(entry->d_name + nlen - 5, ".meta") == 0) {
                closedir(dir);
                return 1;
            }
        }
        closedir(dir);
    }

    return 0;
}

USFile *mitgcm_open(const char *path) {
    if (!path) return NULL;

    char directory[MAX_NAME_LEN];

    /* Resolve directory */
    if (is_directory(path)) {
        strncpy(directory, path, MAX_NAME_LEN - 1);
        directory[MAX_NAME_LEN - 1] = '\0';
    } else {
        /* Extract parent directory from .data file path */
        strncpy(directory, path, MAX_NAME_LEN - 1);
        directory[MAX_NAME_LEN - 1] = '\0';
        char *slash = strrchr(directory, '/');
        if (slash) {
            *slash = '\0';
        } else {
            strcpy(directory, ".");
        }
    }

    /* Remove trailing slash */
    size_t dlen = strlen(directory);
    if (dlen > 1 && directory[dlen - 1] == '/') directory[dlen - 1] = '\0';

    /* Determine grid dimensions from meta files */
    int nx = 0, ny = 0, nz = 0;

    /* Try XC.meta first (2D: nx, ny) */
    char meta_path[MAX_NAME_LEN];
    snprintf(meta_path, sizeof(meta_path), "%s/XC.meta", directory);
    MetaInfo minfo;
    if (parse_meta(meta_path, &minfo) == 0 && minfo.n_dims >= 2) {
        /* C-order: dims are [ny, nx] for 2D */
        ny = minfo.dims[0];
        nx = minfo.dims[1];
    }

    /* Try hFacC.meta for nz (3D: nz, ny, nx) */
    snprintf(meta_path, sizeof(meta_path), "%s/hFacC.meta", directory);
    if (parse_meta(meta_path, &minfo) == 0 && minfo.n_dims >= 3) {
        nz = minfo.dims[0];
        if (nx == 0) { ny = minfo.dims[1]; nx = minfo.dims[2]; }
    }

    /* If still no grid dims, scan for any diagnostic meta with dimList */
    if (nx == 0 || ny == 0) {
        DIR *dir = opendir(directory);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                size_t nlen = strlen(entry->d_name);
                if (nlen > 5 && strcmp(entry->d_name + nlen - 5, ".meta") == 0 &&
                    strchr(entry->d_name, '.') != entry->d_name + nlen - 5) {
                    /* Has a dot before .meta → likely a diagnostic file */
                    snprintf(meta_path, sizeof(meta_path), "%s/%s", directory, entry->d_name);
                    if (parse_meta(meta_path, &minfo) == 0 && minfo.n_dims >= 2) {
                        if (minfo.n_dims == 2) {
                            ny = minfo.dims[0]; nx = minfo.dims[1];
                        } else if (minfo.n_dims == 3) {
                            nz = minfo.dims[0]; ny = minfo.dims[1]; nx = minfo.dims[2];
                        }
                        break;
                    }
                }
            }
            closedir(dir);
        }
    }

    if (nx == 0 || ny == 0) {
        fprintf(stderr, "MITgcm: Cannot determine grid dimensions from meta files in %s\n", directory);
        return NULL;
    }

    printf("MITgcm grid: %d x %d", nx, ny);
    if (nz > 0) printf(" x %d", nz);
    printf("\n");

    /* Allocate store */
    MitgcmStore *store = calloc(1, sizeof(MitgcmStore));
    if (!store) return NULL;
    strncpy(store->directory, directory, MAX_NAME_LEN - 1);
    store->nx = nx;
    store->ny = ny;
    store->nz = nz;
    store->missing_value = -999.0f;

    /* Read RC.data for depth values */
    if (nz > 0) {
        char rc_path[MAX_NAME_LEN];
        snprintf(rc_path, sizeof(rc_path), "%s/RC.data", directory);
        if (file_exists(rc_path)) {
            store->depth_values = read_binary_floats(rc_path, 0, nz);
            if (store->depth_values) {
                printf("MITgcm: loaded %d depth levels (%.1f to %.1f m)\n",
                       nz, store->depth_values[0], store->depth_values[nz - 1]);
            }
        }
    }

    /* Read hFacC.data for land masking */
    if (nz > 0) {
        char hfac_path[MAX_NAME_LEN];
        snprintf(hfac_path, sizeof(hfac_path), "%s/hFacC.data", directory);
        if (file_exists(hfac_path)) {
            size_t total = (size_t)nz * ny * nx;
            store->hfac = read_binary_floats(hfac_path, 0, total);
            if (store->hfac) {
                printf("MITgcm: loaded hFacC mask (%zu cells)\n", total);
            }
        }
    }

    /* Create USFile */
    USFile *f = calloc(1, sizeof(USFile));
    if (!f) { free(store->depth_values); free(store->hfac); free(store); return NULL; }

    f->file_type = FILE_TYPE_MITGCM;
    strncpy(f->filename, directory, MAX_NAME_LEN - 1);
    f->mitgcm_data = store;

    return f;
}

USMesh *mitgcm_create_mesh(USFile *file) {
    if (!file || !file->mitgcm_data) return NULL;
    MitgcmStore *store = (MitgcmStore *)file->mitgcm_data;

    int nx = store->nx, ny = store->ny;
    size_t n_points = (size_t)ny * nx;

    /* Read XC.data and YC.data */
    char xc_path[MAX_NAME_LEN], yc_path[MAX_NAME_LEN];
    snprintf(xc_path, sizeof(xc_path), "%s/XC.data", store->directory);
    snprintf(yc_path, sizeof(yc_path), "%s/YC.data", store->directory);

    float *xc = read_binary_floats(xc_path, 0, n_points);
    float *yc = read_binary_floats(yc_path, 0, n_points);
    if (!xc || !yc) {
        fprintf(stderr, "MITgcm: Failed to read XC/YC grid files\n");
        free(xc); free(yc);
        return NULL;
    }

    /* Convert float to double for mesh_create */
    double *lon = malloc(n_points * sizeof(double));
    double *lat = malloc(n_points * sizeof(double));
    if (!lon || !lat) {
        free(xc); free(yc); free(lon); free(lat);
        return NULL;
    }

    for (size_t i = 0; i < n_points; i++) {
        lon[i] = (double)xc[i];
        lat[i] = (double)yc[i];
    }
    free(xc);
    free(yc);

    USMesh *mesh = mesh_create(lon, lat, n_points, COORD_TYPE_2D_CURVILINEAR);
    if (mesh) {
        mesh->orig_nx = nx;
        mesh->orig_ny = ny;
        printf("MITgcm mesh: %zu points (%dx%d curvilinear)\n", n_points, nx, ny);
    }
    return mesh;
}

/* ---- Iteration discovery ---- */

/* Compare ints for qsort */
static int cmp_int(const void *a, const void *b) {
    return (*(const int *)a) - (*(const int *)b);
}

/*
 * Find all iteration numbers for a given prefix.
 * Scans for files matching <prefix>.NNNNNNNNNN.meta
 */
static int *find_iterations(const char *directory, const char *prefix,
                            int *n_out) {
    *n_out = 0;
    DIR *dir = opendir(directory);
    if (!dir) return NULL;

    size_t prefix_len = strlen(prefix);
    int capacity = 32;
    int *iters = malloc(capacity * sizeof(int));
    if (!iters) { closedir(dir); return NULL; }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t nlen = strlen(entry->d_name);
        /* Match: prefix.DIGITS.meta */
        if (nlen < prefix_len + 7) continue;  /* prefix + . + at least 1 digit + .meta */
        if (strncmp(entry->d_name, prefix, prefix_len) != 0) continue;
        if (entry->d_name[prefix_len] != '.') continue;
        if (nlen < 6 || strcmp(entry->d_name + nlen - 5, ".meta") != 0) continue;

        /* Extract digits between prefix. and .meta */
        const char *digit_start = entry->d_name + prefix_len + 1;
        const char *digit_end = entry->d_name + nlen - 5;
        if (digit_end <= digit_start) continue;

        /* Check all digits */
        int all_digits = 1;
        for (const char *c = digit_start; c < digit_end; c++) {
            if (*c < '0' || *c > '9') { all_digits = 0; break; }
        }
        if (!all_digits) continue;

        char num_buf[32];
        size_t num_len = digit_end - digit_start;
        if (num_len >= sizeof(num_buf)) continue;
        memcpy(num_buf, digit_start, num_len);
        num_buf[num_len] = '\0';

        int iter = atoi(num_buf);

        if (*n_out >= capacity) {
            capacity *= 2;
            int *tmp = realloc(iters, capacity * sizeof(int));
            if (!tmp) break;
            iters = tmp;
        }
        iters[(*n_out)++] = iter;
    }
    closedir(dir);

    if (*n_out > 1) {
        qsort(iters, *n_out, sizeof(int), cmp_int);
    }

    return iters;
}

USVar *mitgcm_scan_variables(USFile *f, USMesh *m) {
    if (!f || !f->mitgcm_data || !m) return NULL;
    MitgcmStore *store = (MitgcmStore *)f->mitgcm_data;

    /* Discover unique prefixes by scanning for *.DIGITS.meta */
    DIR *dir = opendir(store->directory);
    if (!dir) return NULL;

    /* Collect unique prefixes */
    char prefixes[64][MAX_NAME_LEN];
    int n_prefixes = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t nlen = strlen(entry->d_name);
        if (nlen < 7 || strcmp(entry->d_name + nlen - 5, ".meta") != 0) continue;

        /* Find first dot that precedes a digit sequence before .meta */
        const char *meta_ext = entry->d_name + nlen - 5;
        const char *last_dot = NULL;
        for (const char *c = meta_ext - 1; c > entry->d_name; c--) {
            if (*c == '.') { last_dot = c; break; }
        }
        if (!last_dot || last_dot == entry->d_name) continue;

        /* Check that between last_dot+1 and meta_ext are all digits */
        int all_digits = 1;
        for (const char *c = last_dot + 1; c < meta_ext; c++) {
            if (*c < '0' || *c > '9') { all_digits = 0; break; }
        }
        if (!all_digits) continue;

        /* Extract prefix */
        size_t plen = last_dot - entry->d_name;
        if (plen >= MAX_NAME_LEN) continue;

        char prefix[MAX_NAME_LEN];
        memcpy(prefix, entry->d_name, plen);
        prefix[plen] = '\0';

        /* Check uniqueness */
        int found = 0;
        for (int i = 0; i < n_prefixes; i++) {
            if (strcmp(prefixes[i], prefix) == 0) { found = 1; break; }
        }
        if (!found && n_prefixes < 64) {
            strncpy(prefixes[n_prefixes++], prefix, MAX_NAME_LEN - 1);
        }
    }
    closedir(dir);

    if (n_prefixes == 0) {
        fprintf(stderr, "MITgcm: No diagnostic files found in %s\n", store->directory);
        return NULL;
    }

    /* For each prefix, parse meta and create variables */
    USVar *head = NULL;
    USVar *tail = NULL;
    int n_vars = 0;

    for (int pi = 0; pi < n_prefixes; pi++) {
        /* Find iterations for this prefix */
        int n_iters = 0;
        int *iterations = find_iterations(store->directory, prefixes[pi], &n_iters);
        if (!iterations || n_iters == 0) { free(iterations); continue; }

        /* Parse meta from first iteration to get field info */
        char meta_path[MAX_NAME_LEN];
        snprintf(meta_path, sizeof(meta_path), "%s/%s.%010d.meta",
                 store->directory, prefixes[pi], iterations[0]);

        MetaInfo minfo;
        if (parse_meta(meta_path, &minfo) != 0) {
            free(iterations);
            continue;
        }

        int is_3d = (minfo.n_dims >= 3 && minfo.dims[0] > 1);
        int var_nx = store->nx;
        int var_ny = store->ny;
        int var_nz = is_3d ? minfo.dims[0] : 1;

        /* If no fldList, treat as single anonymous field from prefix */
        int actual_fields = minfo.n_fields > 0 ? minfo.n_fields : 1;

        for (int fi = 0; fi < actual_fields; fi++) {
            USVar *var = calloc(1, sizeof(USVar));
            if (!var) continue;

            /* Name */
            if (minfo.n_fields > 0 && minfo.fields[fi][0]) {
                strncpy(var->name, minfo.fields[fi], MAX_NAME_LEN - 1);
            } else {
                strncpy(var->name, prefixes[pi], MAX_NAME_LEN - 1);
            }

            var->mesh = m;
            var->file = f;
            var->fill_value = minfo.has_missing ? minfo.missing_value : DEFAULT_FILL_VALUE;

            /* Set up dimensions */
            int dim_idx = 0;

            /* Time dimension (iterations) */
            if (n_iters > 0) {
                var->time_dim_id = dim_idx;
                strncpy(var->dim_names[dim_idx], "time", MAX_NAME_LEN - 1);
                var->dim_sizes[dim_idx] = n_iters;
                dim_idx++;
            } else {
                var->time_dim_id = -1;
            }

            /* Depth dimension */
            if (is_3d && var_nz > 1) {
                var->depth_dim_id = dim_idx;
                strncpy(var->dim_names[dim_idx], "depth", MAX_NAME_LEN - 1);
                var->dim_sizes[dim_idx] = var_nz;
                dim_idx++;
            } else {
                var->depth_dim_id = -1;
            }

            /* Spatial dimension */
            var->node_dim_id = dim_idx;
            strncpy(var->dim_names[dim_idx], "node", MAX_NAME_LEN - 1);
            var->dim_sizes[dim_idx] = (size_t)var_ny * var_nx;
            dim_idx++;

            var->n_dims = dim_idx;

            /* Private data */
            MitgcmVarData *vd = calloc(1, sizeof(MitgcmVarData));
            if (!vd) { free(var); continue; }
            strncpy(vd->prefix, prefixes[pi], MAX_NAME_LEN - 1);
            vd->field_index = fi;
            vd->iterations = malloc(n_iters * sizeof(int));
            if (vd->iterations) {
                memcpy(vd->iterations, iterations, n_iters * sizeof(int));
            }
            vd->n_iterations = n_iters;
            vd->is_3d = is_3d;
            vd->nx = var_nx;
            vd->ny = var_ny;
            vd->nz = var_nz;
            vd->missing_value = minfo.has_missing ? minfo.missing_value : DEFAULT_FILL_VALUE;
            vd->has_missing = minfo.has_missing;
            var->mitgcm_data = vd;

            /* Append to list */
            var->next = NULL;
            if (!head) {
                head = var;
                tail = var;
            } else {
                tail->next = var;
                tail = var;
            }
            n_vars++;
        }

        free(iterations);
    }

    f->vars = head;
    f->n_vars = n_vars;
    printf("MITgcm: found %d variables from %d diagnostic groups\n", n_vars, n_prefixes);

    return head;
}

int mitgcm_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data) {
    if (!var || !var->mitgcm_data || !var->file || !var->file->mitgcm_data || !data)
        return -1;

    MitgcmVarData *vd = (MitgcmVarData *)var->mitgcm_data;
    MitgcmStore *store = (MitgcmStore *)var->file->mitgcm_data;

    if ((int)time_idx >= vd->n_iterations) return -1;

    /* Build data file path */
    char data_path[MAX_NAME_LEN];
    snprintf(data_path, sizeof(data_path), "%s/%s.%010d.data",
             store->directory, vd->prefix, vd->iterations[time_idx]);

    size_t slab_size = (size_t)vd->ny * vd->nx;
    size_t field_size;

    if (vd->is_3d) {
        field_size = (size_t)vd->nz * slab_size;
    } else {
        field_size = slab_size;
    }

    /* Compute offset: field_index * field_size + depth_offset */
    size_t offset = (size_t)vd->field_index * field_size;
    if (vd->is_3d) {
        offset += depth_idx * slab_size;
    }
    offset *= sizeof(float);

    /* Read the 2D slab */
    FILE *fp = fopen(data_path, "rb");
    if (!fp) {
        fprintf(stderr, "MITgcm: Cannot open %s\n", data_path);
        return -1;
    }

    fseek(fp, (long)offset, SEEK_SET);
    size_t nread = fread(data, sizeof(float), slab_size, fp);
    fclose(fp);

    if (nread != slab_size) {
        fprintf(stderr, "MITgcm: Short read from %s (got %zu, expected %zu)\n",
                data_path, nread, slab_size);
        return -1;
    }

    /* Byte swap */
    if (is_little_endian()) {
        swap_endian_float(data, slab_size);
    }

    /* Apply hFacC land masking */
    if (store->hfac && vd->is_3d) {
        float *mask = store->hfac + depth_idx * slab_size;
        for (size_t i = 0; i < slab_size; i++) {
            if (mask[i] == 0.0f) data[i] = var->fill_value;
        }
    } else if (store->hfac && !vd->is_3d) {
        /* Use surface level for 2D vars */
        for (size_t i = 0; i < slab_size; i++) {
            if (store->hfac[i] == 0.0f) data[i] = var->fill_value;
        }
    }

    /* Apply missing value masking */
    if (vd->has_missing) {
        for (size_t i = 0; i < slab_size; i++) {
            if (data[i] == vd->missing_value || fabsf(data[i]) > INVALID_DATA_THRESHOLD) {
                data[i] = var->fill_value;
            }
        }
    }

    return 0;
}

int mitgcm_estimate_range(USVar *var, float *min_val, float *max_val) {
    if (!var || !var->mitgcm_data) return -1;

    MitgcmVarData *vd = (MitgcmVarData *)var->mitgcm_data;
    size_t slab_size = (size_t)vd->ny * vd->nx;

    float *data = malloc(slab_size * sizeof(float));
    if (!data) return -1;

    float gmin = 1e30f, gmax = -1e30f;
    int found = 0;

    /* Sample first, mid, last timestep at surface (depth 0) */
    int samples[] = {0, vd->n_iterations / 2, vd->n_iterations - 1};
    int n_samples = (vd->n_iterations >= 3) ? 3 :
                    (vd->n_iterations >= 2) ? 2 : 1;

    for (int s = 0; s < n_samples; s++) {
        if (mitgcm_read_slice(var, samples[s], 0, data) != 0) continue;

        for (size_t i = 0; i < slab_size; i++) {
            float v = data[i];
            if (v == var->fill_value || fabsf(v) > INVALID_DATA_THRESHOLD) continue;
            if (v < gmin) gmin = v;
            if (v > gmax) gmax = v;
            found = 1;
        }
    }

    free(data);

    if (!found) {
        *min_val = 0.0f;
        *max_val = 1.0f;
        return -1;
    }

    *min_val = gmin;
    *max_val = gmax;
    return 0;
}

USDimInfo *mitgcm_get_dim_info(USVar *var, int *n_dims_out) {
    if (!var || !var->mitgcm_data || !var->file || !var->file->mitgcm_data) {
        *n_dims_out = 0;
        return NULL;
    }

    MitgcmVarData *vd = (MitgcmVarData *)var->mitgcm_data;
    MitgcmStore *store = (MitgcmStore *)var->file->mitgcm_data;

    int n_dims = 0;
    if (var->time_dim_id >= 0) n_dims++;
    if (var->depth_dim_id >= 0) n_dims++;

    if (n_dims == 0) {
        *n_dims_out = 0;
        return NULL;
    }

    USDimInfo *dims = calloc(n_dims, sizeof(USDimInfo));
    if (!dims) { *n_dims_out = 0; return NULL; }

    int idx = 0;

    /* Time dimension (iteration numbers) */
    if (var->time_dim_id >= 0) {
        USDimInfo *di = &dims[idx++];
        strncpy(di->name, "time", MAX_NAME_LEN - 1);
        strncpy(di->units, "iteration", MAX_NAME_LEN - 1);
        di->size = vd->n_iterations;
        di->is_scannable = (vd->n_iterations > 1);
        di->values = malloc(vd->n_iterations * sizeof(double));
        if (di->values) {
            for (int i = 0; i < vd->n_iterations; i++) {
                di->values[i] = (double)vd->iterations[i];
            }
            di->min_val = di->values[0];
            di->max_val = di->values[vd->n_iterations - 1];
        }
    }

    /* Depth dimension (RC values) */
    if (var->depth_dim_id >= 0) {
        USDimInfo *di = &dims[idx++];
        strncpy(di->name, "depth", MAX_NAME_LEN - 1);
        strncpy(di->units, "m", MAX_NAME_LEN - 1);
        di->size = vd->nz;
        di->is_scannable = (vd->nz > 1);
        if (store->depth_values) {
            di->values = malloc(vd->nz * sizeof(double));
            if (di->values) {
                for (int i = 0; i < vd->nz; i++) {
                    di->values[i] = (double)store->depth_values[i];
                }
                di->min_val = di->values[0];
                di->max_val = di->values[vd->nz - 1];
            }
        }
    }

    *n_dims_out = n_dims;
    return dims;
}

void mitgcm_free_dim_info(USDimInfo *dims, int n_dims) {
    if (!dims) return;
    for (int i = 0; i < n_dims; i++) {
        free(dims[i].values);
    }
    free(dims);
}

int mitgcm_read_timeseries(USVar *var, size_t node_idx, size_t depth_idx,
                           double **times_out, float **values_out,
                           int **valid_out, size_t *n_out) {
    if (!var || !var->mitgcm_data || !var->file || !var->file->mitgcm_data)
        return -1;

    MitgcmVarData *vd = (MitgcmVarData *)var->mitgcm_data;
    MitgcmStore *store = (MitgcmStore *)var->file->mitgcm_data;

    size_t n_times = vd->n_iterations;
    size_t slab_size = (size_t)vd->ny * vd->nx;

    if (node_idx >= slab_size) return -1;

    double *times = malloc(n_times * sizeof(double));
    float *values = malloc(n_times * sizeof(float));
    int *valid = malloc(n_times * sizeof(int));
    if (!times || !values || !valid) {
        free(times); free(values); free(valid);
        return -1;
    }

    size_t field_size;
    if (vd->is_3d) {
        field_size = (size_t)vd->nz * slab_size;
    } else {
        field_size = slab_size;
    }

    for (size_t t = 0; t < n_times; t++) {
        times[t] = (double)vd->iterations[t];

        /* Build path and read single value */
        char data_path[MAX_NAME_LEN];
        snprintf(data_path, sizeof(data_path), "%s/%s.%010d.data",
                 store->directory, vd->prefix, vd->iterations[t]);

        size_t offset = (size_t)vd->field_index * field_size;
        if (vd->is_3d) {
            offset += depth_idx * slab_size;
        }
        offset += node_idx;
        offset *= sizeof(float);

        FILE *fp = fopen(data_path, "rb");
        if (!fp) {
            values[t] = var->fill_value;
            valid[t] = 0;
            continue;
        }

        float val;
        fseek(fp, (long)offset, SEEK_SET);
        if (fread(&val, sizeof(float), 1, fp) == 1) {
            if (is_little_endian()) swap_endian_float(&val, 1);

            /* Check hFacC mask */
            int masked = 0;
            if (store->hfac && vd->is_3d) {
                if (store->hfac[depth_idx * slab_size + node_idx] == 0.0f)
                    masked = 1;
            } else if (store->hfac && !vd->is_3d) {
                if (store->hfac[node_idx] == 0.0f)
                    masked = 1;
            }

            if (masked || val == vd->missing_value || fabsf(val) > INVALID_DATA_THRESHOLD) {
                values[t] = var->fill_value;
                valid[t] = 0;
            } else {
                values[t] = val;
                valid[t] = 1;
            }
        } else {
            values[t] = var->fill_value;
            valid[t] = 0;
        }
        fclose(fp);
    }

    *times_out = times;
    *values_out = values;
    *valid_out = valid;
    *n_out = n_times;
    return 0;
}

void mitgcm_close(USFile *file) {
    if (!file) return;

    if (file->mitgcm_data) {
        MitgcmStore *store = (MitgcmStore *)file->mitgcm_data;
        free(store->depth_values);
        free(store->hfac);
        free(store);
    }

    /* Free variables */
    USVar *var = file->vars;
    while (var) {
        USVar *next = var->next;
        if (var->mitgcm_data) {
            MitgcmVarData *vd = (MitgcmVarData *)var->mitgcm_data;
            free(vd->iterations);
            free(vd);
        }
        free(var);
        var = next;
    }

    free(file);
}
