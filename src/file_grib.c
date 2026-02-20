/*
 * file_grib.c - GRIB file reading implementation
 */

#ifdef HAVE_GRIB

#include "file_grib.h"
#include "mesh.h"

#include <eccodes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/types.h>

#define GRIB_TIME_DIM_NAME "time"
#define GRIB_DEPTH_DIM_NAME "depth"

typedef struct {
    char short_name[MAX_NAME_LEN];
    char type_of_level[MAX_NAME_LEN];
    char units[MAX_NAME_LEN];
    char long_name[MAX_NAME_LEN];
    size_t n_levels;
    size_t n_times;
    double *times;
    double *levels;
    off_t **message_offsets;
    size_t *message_counts;
    size_t total_messages;
    int is_multi_level;
} GribVarData;

typedef struct {
    FILE *fp;
    char *path;
    off_t *offsets;
    size_t *sizes;
    int n_messages;
} GribFileData;

typedef struct {
    char short_name[MAX_NAME_LEN];
    char type_of_level[MAX_NAME_LEN];
    char units[MAX_NAME_LEN];
    char long_name[MAX_NAME_LEN];
    size_t n_levels;
    size_t n_times;
    double *times;
    double *levels;
    off_t **message_offsets;
    size_t *message_counts;
    size_t total_messages;
} GribVarGroup;

typedef struct {
    char short_name[MAX_NAME_LEN];
    char type_of_level[MAX_NAME_LEN];
    long level;
    double time;
    int has_time;
    int has_level;
    off_t offset;
} GribMessageInfo;

typedef struct {
    double time;
    off_t offset;
} GribLevelMessage;

static codes_handle *grib_handle_from_offset(GribFileData *gfile, off_t offset);

static int grib_util_get_string(codes_handle *h, const char *key, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    buf[0] = '\0';
    size_t len = bufsize;
    if (codes_get_string(h, key, buf, &len) != CODES_SUCCESS) return 0;
    buf[bufsize - 1] = '\0';
    return 1;
}

static int grib_util_get_long(codes_handle *h, const char *key, long *value) {
    if (!value) return 0;
    return codes_get_long(h, key, value) == CODES_SUCCESS;
}

static int grib_util_is_missing(codes_handle *h, const char *key) {
    int err = 0;
    int missing = codes_is_missing(h, key, &err);
    return (err == CODES_SUCCESS && missing);
}

static int grib_get_time_value(codes_handle *h, double *time_out) {
    long date = 0;
    long time = 0;
    if (grib_util_get_long(h, "validityDate", &date) && grib_util_get_long(h, "validityTime", &time)) {
        /* Use validity time when available */
    } else if (!grib_util_get_long(h, "dataDate", &date) || !grib_util_get_long(h, "dataTime", &time)) {
        return 0;
    }

    int y = (int)(date / 10000);
    int m = (int)((date / 100) % 100);
    int d = (int)(date % 100);
    int hour = (int)(time / 100);
    int minute = (int)(time % 100);

    int y_adj = y - (m <= 2);
    int64_t era = (y_adj >= 0 ? y_adj : y_adj - 399) / 400;
    unsigned yoe = (unsigned)(y_adj - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)(era * 146097 + (int)doe - 719468);

    double seconds = (double)days * 86400.0 + (double)hour * 3600.0 + (double)minute * 60.0;
    *time_out = seconds / 86400.0;  /* days since 1970-01-01 */
    return 1;
}

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static int compare_level_message(const void *a, const void *b) {
    const GribLevelMessage *ma = (const GribLevelMessage *)a;
    const GribLevelMessage *mb = (const GribLevelMessage *)b;
    if (ma->time < mb->time) return -1;
    if (ma->time > mb->time) return 1;
    if (ma->offset < mb->offset) return -1;
    if (ma->offset > mb->offset) return 1;
    return 0;
}

static int grib_time_list_contains(const double *times, size_t n_times, double value) {
    for (size_t i = 0; i < n_times; i++) {
        if (times[i] == value) return 1;
    }
    return 0;
}

static void grib_fileset_collect_times(USFileSet *fs) {
    if (!fs || fs->n_files <= 0) return;

    double *times = NULL;
    size_t n_times = 0;
    size_t cap = 0;

    for (int f = 0; f < fs->n_files; f++) {
        USFile *file = fs->files[f];
        if (!file || !file->grib_data) continue;
        GribFileData *gfile = (GribFileData *)file->grib_data;
        for (int i = 0; i < gfile->n_messages; i++) {
            codes_handle *h = grib_handle_from_offset(gfile, gfile->offsets[i]);
            if (!h) continue;
            double time_val = 0.0;
            if (grib_get_time_value(h, &time_val)) {
                if (!grib_time_list_contains(times, n_times, time_val)) {
                    if (n_times == cap) {
                        size_t new_cap = cap == 0 ? 16 : cap * 2;
                        double *new_times = realloc(times, new_cap * sizeof(double));
                        if (!new_times) {
                            codes_handle_delete(h);
                            free(times);
                            return;
                        }
                        times = new_times;
                        cap = new_cap;
                    }
                    times[n_times++] = time_val;
                }
            }
            codes_handle_delete(h);
        }
    }

    if (n_times == 0) {
        free(times);
        return;
    }

    qsort(times, n_times, sizeof(double), compare_double);
    fs->grib_times = times;
    fs->grib_n_times = n_times;
}

static int grib_var_list_contains(USVar *list, const char *name) {
    for (USVar *v = list; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return 1;
    }
    return 0;
}

static void grib_free_var_list_shallow(USVar *list) {
    while (list) {
        USVar *next = list->next;
        free(list);
        list = next;
    }
}

static void grib_apply_fileset_time_dim(USVar *var, const USFileSet *fs) {
    if (!var || !fs) return;
    if (fs->grib_n_times <= 1) return;

    if (var->time_dim_id >= 0) {
        var->dim_sizes[var->time_dim_id] = fs->grib_n_times;
        strncpy(var->dim_names[var->time_dim_id], GRIB_TIME_DIM_NAME, MAX_NAME_LEN - 1);
        return;
    }

    if (var->n_dims >= MAX_DIMS) return;

    for (int i = var->n_dims; i > 0; i--) {
        var->dim_sizes[i] = var->dim_sizes[i - 1];
        strncpy(var->dim_names[i], var->dim_names[i - 1], MAX_NAME_LEN - 1);
        var->dim_names[i][MAX_NAME_LEN - 1] = '\0';
    }

    var->n_dims++;
    var->time_dim_id = 0;
    if (var->depth_dim_id >= 0) var->depth_dim_id++;
    if (var->node_dim_id >= 0) var->node_dim_id++;

    strncpy(var->dim_names[0], GRIB_TIME_DIM_NAME, MAX_NAME_LEN - 1);
    var->dim_names[0][MAX_NAME_LEN - 1] = '\0';
    var->dim_sizes[0] = fs->grib_n_times;
}

static int is_grib_message(FILE *fp) {
    if (!fp) return 0;
    long pos = ftell(fp);
    unsigned char header[4] = {0};
    size_t n = fread(header, 1, sizeof(header), fp);
    fseek(fp, pos, SEEK_SET);
    if (n < sizeof(header)) return 0;
    return (header[0] == 'G' && header[1] == 'R' && header[2] == 'I' && header[3] == 'B');
}

int grib_is_grib_file(const char *path) {
    if (!path) return 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    int result = is_grib_message(fp);
    fclose(fp);
    return result;
}

static codes_handle *grib_handle_from_offset(GribFileData *gfile, off_t offset) {
    if (!gfile || !gfile->fp) return NULL;
    if (fseeko(gfile->fp, offset, SEEK_SET) != 0) return NULL;
    int err = 0;
    codes_handle *h = codes_handle_new_from_file(NULL, gfile->fp, PRODUCT_GRIB, &err);
    if (err != CODES_SUCCESS) {
        if (h) codes_handle_delete(h);
        return NULL;
    }
    return h;
}

USFile *grib_open(const char *filename) {
    if (!filename) return NULL;

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error opening %s\n", filename);
        return NULL;
    }

    if (!is_grib_message(fp)) {
        fprintf(stderr, "Not a GRIB file: %s\n", filename);
        fclose(fp);
        return NULL;
    }

    GribFileData *gfile = calloc(1, sizeof(GribFileData));
    if (!gfile) {
        fclose(fp);
        return NULL;
    }
    gfile->fp = fp;
    gfile->path = strdup(filename);

    int num = 0;
    off_t *offsets = NULL;
    size_t *sizes = NULL;
    int rc = codes_extract_offsets_sizes_malloc(NULL, filename, PRODUCT_GRIB, &offsets, &sizes, &num, 0);
    if (rc != CODES_SUCCESS || num <= 0) {
        fprintf(stderr, "Failed to scan GRIB messages in %s\n", filename);
        free(offsets);
        free(sizes);
        free(gfile->path);
        free(gfile);
        fclose(fp);
        return NULL;
    }

    gfile->offsets = offsets;
    gfile->sizes = sizes;
    gfile->n_messages = num;

    USFile *file = calloc(1, sizeof(USFile));
    if (!file) {
        free(offsets);
        free(sizes);
        free(gfile->path);
        free(gfile);
        fclose(fp);
        return NULL;
    }

    file->file_type = FILE_TYPE_GRIB;
    file->ncid = -1;
    file->grib_data = gfile;
    strncpy(file->filename, filename, MAX_NAME_LEN - 1);
    return file;
}

USMesh *grib_create_mesh(USFile *file) {
    if (!file || !file->grib_data) return NULL;
    GribFileData *gfile = (GribFileData *)file->grib_data;
    if (gfile->n_messages <= 0) return NULL;

    codes_handle *h = grib_handle_from_offset(gfile, gfile->offsets[0]);
    if (!h) return NULL;

    long n_points = 0;
    if (!grib_util_get_long(h, "numberOfPoints", &n_points) || n_points <= 0) {
        fprintf(stderr, "GRIB: missing numberOfPoints\n");
        codes_handle_delete(h);
        return NULL;
    }

    double *lats = malloc((size_t)n_points * sizeof(double));
    double *lons = malloc((size_t)n_points * sizeof(double));
    double *vals = malloc((size_t)n_points * sizeof(double));
    if (!lats || !lons || !vals) {
        free(lats);
        free(lons);
        free(vals);
        codes_handle_delete(h);
        return NULL;
    }

    int rc = codes_grib_get_data(h, lats, lons, vals);
    free(vals);
    codes_handle_delete(h);
    if (rc != CODES_SUCCESS) {
        fprintf(stderr, "GRIB: failed to read lat/lon arrays (rc=%d)\n", rc);
        free(lats);
        free(lons);
        return NULL;
    }

    for (size_t i = 0; i < (size_t)n_points; i++) {
        while (lons[i] > 180.0) lons[i] -= 360.0;
        while (lons[i] < -180.0) lons[i] += 360.0;
    }

    USMesh *mesh = mesh_create(lons, lats, (size_t)n_points, COORD_TYPE_1D_UNSTRUCTURED);
    if (!mesh) {
        free(lats);
        free(lons);
        return NULL;
    }

    return mesh;
}

static void free_grib_var_data(GribVarData *data) {
    if (!data) return;
    if (data->message_offsets) {
        for (size_t i = 0; i < data->n_levels; i++) {
            free(data->message_offsets[i]);
        }
        free(data->message_offsets);
    }
    free(data->message_counts);
    free(data->times);
    free(data->levels);
    free(data);
}

static void free_grib_groups(GribVarGroup *groups, size_t n_groups) {
    if (!groups) return;
    for (size_t i = 0; i < n_groups; i++) {
        for (size_t j = 0; j < groups[i].n_levels; j++) {
            free(groups[i].message_offsets[j]);
        }
        free(groups[i].message_offsets);
        free(groups[i].message_counts);
        free(groups[i].times);
        free(groups[i].levels);
    }
    free(groups);
}

static int group_matches(const GribVarGroup *group, const char *short_name, const char *type_of_level) {
    if (strcmp(group->short_name, short_name) != 0) return 0;
    if (strcmp(group->type_of_level, type_of_level) != 0) return 0;
    return 1;
}

static int add_group(GribVarGroup **groups, size_t *n_groups, const char *short_name,
                     const char *type_of_level, const char *units, const char *long_name) {
    size_t new_count = *n_groups + 1;
    GribVarGroup *new_groups = realloc(*groups, new_count * sizeof(GribVarGroup));
    if (!new_groups) return -1;
    *groups = new_groups;
    GribVarGroup *g = &new_groups[*n_groups];
    memset(g, 0, sizeof(*g));
    strncpy(g->short_name, short_name, MAX_NAME_LEN - 1);
    strncpy(g->type_of_level, type_of_level, MAX_NAME_LEN - 1);
    if (units) strncpy(g->units, units, MAX_NAME_LEN - 1);
    if (long_name) strncpy(g->long_name, long_name, MAX_NAME_LEN - 1);
    *n_groups = new_count;
    return (int)(new_count - 1);
}

static int add_message_info(GribMessageInfo **infos, size_t *n_infos, size_t *cap,
                            const GribMessageInfo *info) {
    if (*n_infos == *cap) {
        size_t new_cap = (*cap == 0) ? 16 : (*cap * 2);
        GribMessageInfo *new_infos = realloc(*infos, new_cap * sizeof(GribMessageInfo));
        if (!new_infos) return -1;
        *infos = new_infos;
        *cap = new_cap;
    }
    (*infos)[*n_infos] = *info;
    (*n_infos)++;
    return 0;
}

static int collect_message_info(GribFileData *gfile, GribMessageInfo **infos_out, size_t *n_infos,
                                const char *short_name, const char *type_of_level) {
    if (!gfile || !infos_out || !n_infos) return -1;

    *infos_out = NULL;
    *n_infos = 0;
    size_t cap = 0;

    for (int i = 0; i < gfile->n_messages; i++) {
        codes_handle *h = grib_handle_from_offset(gfile, gfile->offsets[i]);
        if (!h) continue;

        char msg_short[MAX_NAME_LEN] = "";
        char msg_type[MAX_NAME_LEN] = "";
        long msg_level = 0;
        if (!grib_util_get_string(h, "shortName", msg_short, sizeof(msg_short))) {
            codes_handle_delete(h);
            continue;
        }
        if (!grib_util_get_string(h, "typeOfLevel", msg_type, sizeof(msg_type))) {
            strncpy(msg_type, "unknown", sizeof(msg_type) - 1);
        }
        if (!grib_util_get_long(h, "level", &msg_level)) {
            msg_level = 0;
        }

        if (strcmp(msg_short, short_name) != 0 || strcmp(msg_type, type_of_level) != 0) {
            codes_handle_delete(h);
            continue;
        }

        GribMessageInfo info;
        memset(&info, 0, sizeof(info));
        strncpy(info.short_name, msg_short, MAX_NAME_LEN - 1);
        strncpy(info.type_of_level, msg_type, MAX_NAME_LEN - 1);
        info.level = msg_level;
        info.offset = gfile->offsets[i];

        if (grib_get_time_value(h, &info.time)) {
            info.has_time = 1;
        }
        if (grib_util_is_missing(h, "level")) {
            info.level = 0;
            info.has_level = 1;
        } else {
            info.has_level = 1;
        }

        if (add_message_info(infos_out, n_infos, &cap, &info) != 0) {
            codes_handle_delete(h);
            return -1;
        }

        codes_handle_delete(h);
    }

    return (*n_infos > 0) ? 0 : -1;
}

static int build_group_data(GribVarGroup *group, const GribMessageInfo *infos, size_t n_infos) {
    if (!group || !infos || n_infos == 0) return -1;

    size_t times_cap = 8;
    size_t levels_cap = 8;
    double *times = malloc(times_cap * sizeof(double));
    double *levels = malloc(levels_cap * sizeof(double));
    if (!times || !levels) {
        free(times);
        free(levels);
        return -1;
    }

    size_t n_times = 0;
    size_t n_levels = 0;

    for (size_t i = 0; i < n_infos; i++) {
        if (infos[i].has_time) {
            int found = 0;
            for (size_t t = 0; t < n_times; t++) {
                if (times[t] == infos[i].time) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (n_times == times_cap) {
                    times_cap *= 2;
                    double *new_times = realloc(times, times_cap * sizeof(double));
                    if (!new_times) {
                        free(times);
                        free(levels);
                        return -1;
                    }
                    times = new_times;
                }
                times[n_times++] = infos[i].time;
            }
        }

        if (infos[i].has_level) {
            int found = 0;
            for (size_t l = 0; l < n_levels; l++) {
                if (levels[l] == (double)infos[i].level) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (n_levels == levels_cap) {
                    levels_cap *= 2;
                    double *new_levels = realloc(levels, levels_cap * sizeof(double));
                    if (!new_levels) {
                        free(times);
                        free(levels);
                        return -1;
                    }
                    levels = new_levels;
                }
                levels[n_levels++] = (double)infos[i].level;
            }
        }
    }

    if (n_times == 0) {
        times[n_times++] = 0.0;
    }
    if (n_levels == 0) {
        levels[n_levels++] = 0.0;
    }

    qsort(times, n_times, sizeof(double), compare_double);
    qsort(levels, n_levels, sizeof(double), compare_double);

    group->times = times;
    group->levels = levels;
    group->n_times = n_times;
    group->n_levels = n_levels;

    group->message_offsets = calloc(n_levels, sizeof(off_t *));
    group->message_counts = calloc(n_levels, sizeof(size_t));
    if (!group->message_offsets || !group->message_counts) {
        free(times);
        free(levels);
        free(group->message_offsets);
        free(group->message_counts);
        group->message_offsets = NULL;
        group->message_counts = NULL;
        return -1;
    }

    size_t *level_caps = calloc(n_levels, sizeof(size_t));
    GribLevelMessage **level_messages = calloc(n_levels, sizeof(GribLevelMessage *));
    if (!level_caps || !level_messages) {
        free(level_caps);
        free(level_messages);
        return -1;
    }

    for (size_t i = 0; i < n_infos; i++) {
        if (!infos[i].has_level) continue;
        size_t idx = 0;
        for (; idx < n_levels; idx++) {
            if (levels[idx] == (double)infos[i].level) break;
        }
        if (idx >= n_levels) continue;
        if (group->message_counts[idx] == level_caps[idx]) {
            size_t new_cap = level_caps[idx] == 0 ? 8 : level_caps[idx] * 2;
            GribLevelMessage *new_msgs = realloc(level_messages[idx], new_cap * sizeof(GribLevelMessage));
            if (!new_msgs) {
                free(level_caps);
                for (size_t l = 0; l < n_levels; l++) {
                    free(level_messages[l]);
                }
                free(level_messages);
                return -1;
            }
            level_messages[idx] = new_msgs;
            level_caps[idx] = new_cap;
        }
        level_messages[idx][group->message_counts[idx]].time = infos[i].has_time ? infos[i].time : 0.0;
        level_messages[idx][group->message_counts[idx]].offset = infos[i].offset;
        group->message_counts[idx]++;
        group->total_messages++;
    }

    for (size_t l = 0; l < n_levels; l++) {
        size_t count = group->message_counts[l];
        if (count == 0) continue;
        qsort(level_messages[l], count, sizeof(GribLevelMessage), compare_level_message);
        group->message_offsets[l] = malloc(count * sizeof(off_t));
        if (!group->message_offsets[l]) {
            free(level_caps);
            for (size_t x = 0; x < n_levels; x++) {
                free(level_messages[x]);
            }
            free(level_messages);
            return -1;
        }
        for (size_t i = 0; i < count; i++) {
            group->message_offsets[l][i] = level_messages[l][i].offset;
        }
    }

    free(level_caps);
    for (size_t l = 0; l < n_levels; l++) {
        free(level_messages[l]);
    }
    free(level_messages);
    return 0;
}

static USVar *build_var_from_group(USFile *file, USMesh *mesh, const GribVarGroup *group,
                                   const char *name_override, int is_multi_level) {
    USVar *var = calloc(1, sizeof(USVar));
    if (!var) return NULL;

    const char *name = name_override ? name_override : group->short_name;
    strncpy(var->name, name, MAX_NAME_LEN - 1);
    if (group->long_name[0]) {
        strncpy(var->long_name, group->long_name, MAX_NAME_LEN - 1);
    } else {
        strncpy(var->long_name, group->short_name, MAX_NAME_LEN - 1);
    }
    if (group->units[0]) {
        strncpy(var->units, group->units, MAX_NAME_LEN - 1);
    }

    var->n_dims = 0;
    var->time_dim_id = -1;
    var->depth_dim_id = -1;
    var->node_dim_id = -1;

    if (group->n_times > 1) {
        var->time_dim_id = var->n_dims;
        strncpy(var->dim_names[var->n_dims], GRIB_TIME_DIM_NAME, MAX_NAME_LEN - 1);
        var->dim_sizes[var->n_dims] = group->n_times;
        var->n_dims++;
    }

    if (is_multi_level) {
        var->depth_dim_id = var->n_dims;
        strncpy(var->dim_names[var->n_dims], GRIB_DEPTH_DIM_NAME, MAX_NAME_LEN - 1);
        var->dim_sizes[var->n_dims] = group->n_levels;
        var->n_dims++;
    }

    var->node_dim_id = var->n_dims;
    strncpy(var->dim_names[var->n_dims], "node", MAX_NAME_LEN - 1);
    var->dim_sizes[var->n_dims] = mesh->n_points;
    var->n_dims++;

    var->mesh = mesh;
    var->file = file;
    var->fill_value = DEFAULT_FILL_VALUE;
    var->range_set = 0;

    GribVarData *data = calloc(1, sizeof(GribVarData));
    if (!data) {
        free(var);
        return NULL;
    }
    strncpy(data->short_name, group->short_name, MAX_NAME_LEN - 1);
    strncpy(data->type_of_level, group->type_of_level, MAX_NAME_LEN - 1);
    data->n_levels = group->n_levels;
    data->n_times = group->n_times;
    data->is_multi_level = is_multi_level;

    data->times = malloc(group->n_times * sizeof(double));
    data->levels = malloc(group->n_levels * sizeof(double));
    data->message_offsets = calloc(group->n_levels, sizeof(off_t *));
    data->message_counts = calloc(group->n_levels, sizeof(size_t));
    if (!data->times || !data->levels || !data->message_offsets || !data->message_counts) {
        free_grib_var_data(data);
        free(var);
        return NULL;
    }

    memcpy(data->times, group->times, group->n_times * sizeof(double));
    memcpy(data->levels, group->levels, group->n_levels * sizeof(double));
    for (size_t i = 0; i < group->n_levels; i++) {
        data->message_counts[i] = group->message_counts[i];
        if (group->message_counts[i] > 0) {
            data->message_offsets[i] = malloc(group->message_counts[i] * sizeof(off_t));
            if (!data->message_offsets[i]) {
                free_grib_var_data(data);
                free(var);
                return NULL;
            }
            memcpy(data->message_offsets[i], group->message_offsets[i],
                   group->message_counts[i] * sizeof(off_t));
        }
    }
    data->total_messages = group->total_messages;
    var->grib_data = data;

    return var;
}

USVar *grib_scan_variables(USFile *file, USMesh *mesh) {
    if (!file || !mesh || !file->grib_data) return NULL;
    GribFileData *gfile = (GribFileData *)file->grib_data;

    GribVarGroup *groups = NULL;
    size_t n_groups = 0;

    for (int i = 0; i < gfile->n_messages; i++) {
        codes_handle *h = grib_handle_from_offset(gfile, gfile->offsets[i]);
        if (!h) continue;

        char short_name[MAX_NAME_LEN] = "";
        char type_of_level[MAX_NAME_LEN] = "";
        long level = 0;
        if (!grib_util_get_string(h, "shortName", short_name, sizeof(short_name))) {
            codes_handle_delete(h);
            continue;
        }
        if (!grib_util_get_string(h, "typeOfLevel", type_of_level, sizeof(type_of_level))) {
            strncpy(type_of_level, "unknown", sizeof(type_of_level) - 1);
        }
        if (!grib_util_get_long(h, "level", &level)) {
            level = 0;
        }

        char units[MAX_NAME_LEN] = "";
        char long_name[MAX_NAME_LEN] = "";
        if (!grib_util_get_string(h, "units", units, sizeof(units))) {
            units[0] = '\0';
        }
        if (!grib_util_get_string(h, "name", long_name, sizeof(long_name))) {
            long_name[0] = '\0';
        }

        int found = -1;
        for (size_t g = 0; g < n_groups; g++) {
            if (group_matches(&groups[g], short_name, type_of_level)) {
                found = (int)g;
                break;
            }
        }
        if (found < 0) {
            found = add_group(&groups, &n_groups, short_name, type_of_level, units, long_name);
        }
        codes_handle_delete(h);
        if (found < 0) break;
    }

    if (n_groups == 0) return NULL;

    USVar *var_list = NULL;
    USVar *var_tail = NULL;
    int var_count = 0;

    for (size_t g = 0; g < n_groups; g++) {
        GribVarGroup *group = &groups[g];

        GribMessageInfo *infos = NULL;
        size_t n_infos = 0;
        if (collect_message_info(gfile, &infos, &n_infos,
                                 group->short_name, group->type_of_level) != 0) {
            free(infos);
            continue;
        }

        if (build_group_data(group, infos, n_infos) != 0) {
            free(infos);
            continue;
        }
        free(infos);

        int is_multi_level = (group->n_levels > 1);
        char name_buf[MAX_NAME_LEN] = {0};
        if (!is_multi_level) {
            long level = (group->n_levels > 0) ? (long)group->levels[0] : 0;
            if (group->type_of_level[0]) {
                snprintf(name_buf, sizeof(name_buf), "%s@%s=%ld",
                         group->short_name, group->type_of_level, level);
            } else {
                snprintf(name_buf, sizeof(name_buf), "%s@level=%ld",
                         group->short_name, level);
            }
        }

        USVar *var = build_var_from_group(file, mesh, group,
                                          is_multi_level ? NULL : name_buf,
                                          is_multi_level);
        if (!var) continue;

        var->next = NULL;
        if (!var_list) var_list = var;
        else var_tail->next = var;
        var_tail = var;
        var_count++;

        printf("Found GRIB variable: %s (levels=%zu, times=%zu)\n",
               var->name, group->n_levels, group->n_times);
    }

    free_grib_groups(groups, n_groups);

    file->vars = var_list;
    file->n_vars = var_count;
    printf("Found %d GRIB variables\n", var_count);
    return var_list;
}

USVar *grib_scan_variables_fileset(USFileSet *fs, USMesh *mesh) {
    if (!fs || !mesh || fs->n_files <= 0) return NULL;

    USVar *var_list = NULL;
    USVar *var_tail = NULL;
    int var_count = 0;

    for (int f = 0; f < fs->n_files; f++) {
        USFile *file = fs->files[f];
        if (!file) continue;
        if (!file->vars) {
            grib_scan_variables(file, mesh);
        }

        for (USVar *v = file->vars; v; v = v->next) {
            if (grib_var_list_contains(var_list, v->name)) continue;

            USVar *copy = calloc(1, sizeof(USVar));
            if (!copy) {
                grib_free_var_list_shallow(var_list);
                return NULL;
            }
            memcpy(copy, v, sizeof(USVar));
            copy->next = NULL;
            grib_apply_fileset_time_dim(copy, fs);

            if (!var_list) var_list = copy;
            else var_tail->next = copy;
            var_tail = copy;
            var_count++;
        }
    }

    if (var_count == 0) {
        grib_free_var_list_shallow(var_list);
        return NULL;
    }

    printf("Found %d GRIB variables across %d files\n", var_count, fs->n_files);
    return var_list;
}

static int grib_select_offset(const GribVarData *data, size_t time_idx, size_t depth_idx, off_t *offset_out) {
    if (!data || !offset_out) return -1;
    if (data->n_levels == 0 || data->n_times == 0) return -1;

    size_t level_idx = data->is_multi_level ? depth_idx : 0;
    if (level_idx >= data->n_levels) level_idx = data->n_levels - 1;
    if (time_idx >= data->n_times) time_idx = data->n_times - 1;

    if (data->message_counts[level_idx] == 0) return -1;
    off_t *offsets = data->message_offsets[level_idx];
    size_t count = data->message_counts[level_idx];

    if (count == 1) {
        *offset_out = offsets[0];
        return 0;
    }

    size_t idx = time_idx < count ? time_idx : count - 1;
    *offset_out = offsets[idx];
    return 0;
}

int grib_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data) {
    if (!var || !var->file || !var->grib_data || !data) return -1;
    GribVarData *data_info = (GribVarData *)var->grib_data;
    GribFileData *gfile = (GribFileData *)var->file->grib_data;
    if (!gfile) return -1;

    off_t offset = 0;
    if (grib_select_offset(data_info, time_idx, depth_idx, &offset) != 0) return -1;

    codes_handle *h = grib_handle_from_offset(gfile, offset);
    if (!h) return -1;

    size_t n_values = 0;
    if (codes_get_size(h, "values", &n_values) != CODES_SUCCESS) {
        codes_handle_delete(h);
        return -1;
    }

    double *values = malloc(n_values * sizeof(double));
    if (!values) {
        codes_handle_delete(h);
        return -1;
    }

    size_t n = n_values;
    if (codes_get_double_array(h, "values", values, &n) != CODES_SUCCESS || n != n_values) {
        free(values);
        codes_handle_delete(h);
        return -1;
    }

    double missing_value = GRIB_MISSING_DOUBLE;
    if (codes_is_defined(h, "missingValue")) {
        codes_get_double(h, "missingValue", &missing_value);
    }

    size_t n_points = var->mesh->n_points;
    size_t copy_count = n_values < n_points ? n_values : n_points;
    for (size_t i = 0; i < copy_count; i++) {
        if (values[i] == missing_value) {
            data[i] = var->fill_value;
        } else {
            data[i] = (float)values[i];
        }
    }
    for (size_t i = copy_count; i < n_points; i++) {
        data[i] = var->fill_value;
    }

    free(values);
    codes_handle_delete(h);
    return 0;
}

int grib_estimate_range(USVar *var, float *min_val, float *max_val) {
    if (!var || !var->mesh || !min_val || !max_val) return -1;

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
        if (grib_read_slice(var, time_idx, 0, data) != 0) continue;

        for (size_t i = 0; i < n_points; i++) {
            float v = data[i];
            if (fabsf(v) > INVALID_DATA_THRESHOLD) continue;
            if (fabsf(v - var->fill_value) < 1e-6f * fabsf(var->fill_value)) continue;
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
    printf("Estimated range for %s: [%.4f, %.4f]\n", var->name, global_min, global_max);
    return 0;
}

USDimInfo *grib_get_dim_info(USVar *var, int *n_dims_out) {
    if (!var || !var->grib_data || !n_dims_out) return NULL;
    GribVarData *data = (GribVarData *)var->grib_data;

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
    if (var->time_dim_id >= 0) {
        USDimInfo *di = &dims[idx++];
        strncpy(di->name, GRIB_TIME_DIM_NAME, MAX_NAME_LEN - 1);
        strncpy(di->units, "days since 1970-01-01", MAX_NAME_LEN - 1);
        di->size = data->n_times;
        di->current = 0;
        di->values = malloc(data->n_times * sizeof(double));
        if (di->values) {
            memcpy(di->values, data->times, data->n_times * sizeof(double));
            di->min_val = di->values[0];
            di->max_val = di->values[data->n_times - 1];
        }
        di->is_scannable = (di->size > 1);
    }

    if (var->depth_dim_id >= 0) {
        USDimInfo *di = &dims[idx++];
        strncpy(di->name, GRIB_DEPTH_DIM_NAME, MAX_NAME_LEN - 1);
        strncpy(di->units, data->type_of_level, MAX_NAME_LEN - 1);
        di->size = data->n_levels;
        di->current = 0;
        di->values = malloc(data->n_levels * sizeof(double));
        if (di->values) {
            memcpy(di->values, data->levels, data->n_levels * sizeof(double));
            di->min_val = di->values[0];
            di->max_val = di->values[data->n_levels - 1];
        }
        di->is_scannable = (di->size > 1);
    }

    *n_dims_out = n_scannable;
    return dims;
}

void grib_free_dim_info(USDimInfo *dims, int n_dims) {
    if (!dims) return;
    for (int i = 0; i < n_dims; i++) {
        free(dims[i].values);
    }
    free(dims);
}

int grib_read_timeseries(USVar *var, size_t node_idx, size_t depth_idx,
                         double **times_out, float **values_out,
                         int **valid_out, size_t *n_out) {
    if (!var || !var->grib_data || !times_out || !values_out || !valid_out || !n_out) return -1;

    *times_out = NULL;
    *values_out = NULL;
    *valid_out = NULL;
    *n_out = 0;

    GribVarData *data = (GribVarData *)var->grib_data;
    size_t n_times = data->n_times;
    if (n_times == 0) return -1;

    double *times = calloc(n_times, sizeof(double));
    float *values = calloc(n_times, sizeof(float));
    int *valid = calloc(n_times, sizeof(int));
    if (!times || !values || !valid) {
        free(times); free(values); free(valid);
        return -1;
    }

    float *slice = malloc(var->mesh->n_points * sizeof(float));
    if (!slice) {
        free(times); free(values); free(valid);
        return -1;
    }

    for (size_t t = 0; t < n_times; t++) {
        if (grib_read_slice(var, t, depth_idx, slice) != 0) {
            free(slice);
            free(times); free(values); free(valid);
            return -1;
        }
        times[t] = data->times[t];
        if (node_idx < var->mesh->n_points) {
            values[t] = slice[node_idx];
            valid[t] = (fabsf(values[t] - var->fill_value) > 1e-6f * fabsf(var->fill_value));
        } else {
            values[t] = var->fill_value;
            valid[t] = 0;
        }
    }

    free(slice);

    *times_out = times;
    *values_out = values;
    *valid_out = valid;
    *n_out = n_times;
    return 0;
}

void grib_close(USFile *file) {
    if (!file) return;
    USVar *var = file->vars;
    while (var) {
        USVar *next = var->next;
        if (var->grib_data) {
            free_grib_var_data((GribVarData *)var->grib_data);
        }
        free(var);
        var = next;
    }

    GribFileData *gfile = (GribFileData *)file->grib_data;
    if (gfile) {
        if (gfile->fp) fclose(gfile->fp);
        free(gfile->offsets);
        free(gfile->sizes);
        free(gfile->path);
        free(gfile);
    }
    free(file);
}

/* ---- Multi-file GRIB support ---- */

#include <glob.h>

static int compare_strings_grib(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* Count unique time values across all messages in a GRIB file */
static size_t grib_count_unique_times(GribFileData *gfile) {
    if (!gfile || gfile->n_messages <= 0) return 1;

    double *times = NULL;
    size_t n_times = 0;
    size_t cap = 0;

    for (int i = 0; i < gfile->n_messages; i++) {
        codes_handle *h = grib_handle_from_offset(gfile, gfile->offsets[i]);
        if (!h) continue;

        double time_val;
        if (grib_get_time_value(h, &time_val)) {
            int found = 0;
            for (size_t t = 0; t < n_times; t++) {
                if (times[t] == time_val) { found = 1; break; }
            }
            if (!found) {
                if (n_times == cap) {
                    size_t new_cap = cap == 0 ? 16 : cap * 2;
                    double *new_times = realloc(times, new_cap * sizeof(double));
                    if (!new_times) {
                        codes_handle_delete(h);
                        break;
                    }
                    times = new_times;
                    cap = new_cap;
                }
                times[n_times++] = time_val;
            }
        }
        codes_handle_delete(h);
    }

    free(times);
    return n_times > 0 ? n_times : 1;
}

/* Find a variable by name in a file's variable list */
static USVar *grib_find_var(USFile *file, const char *name) {
    if (!file || !name) return NULL;
    for (USVar *v = file->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

USFileSet *grib_open_fileset(const char **filenames, int n_files) {
    if (!filenames || n_files <= 0) return NULL;

    USFileSet *fs = calloc(1, sizeof(USFileSet));
    if (!fs) return NULL;

    fs->files = calloc(n_files, sizeof(USFile *));
    fs->time_offsets = calloc(n_files + 1, sizeof(size_t));
    if (!fs->files || !fs->time_offsets) {
        free(fs->files);
        free(fs->time_offsets);
        free(fs);
        return NULL;
    }

    /* Create sorted copy of filenames */
    char **sorted = malloc(n_files * sizeof(char *));
    if (!sorted) {
        free(fs->files);
        free(fs->time_offsets);
        free(fs);
        return NULL;
    }
    for (int i = 0; i < n_files; i++) {
        sorted[i] = strdup(filenames[i]);
    }
    qsort(sorted, n_files, sizeof(char *), compare_strings_grib);

    /* Open each file and count time steps */
    fs->time_offsets[0] = 0;
    for (int i = 0; i < n_files; i++) {
        printf("Opening GRIB file %d/%d: %s\n", i + 1, n_files, sorted[i]);
        fs->files[i] = grib_open(sorted[i]);
        if (!fs->files[i]) {
            fprintf(stderr, "Failed to open GRIB file: %s\n", sorted[i]);
            for (int j = 0; j < i; j++) {
                grib_close(fs->files[j]);
            }
            for (int j = 0; j < n_files; j++) free(sorted[j]);
            free(sorted);
            free(fs->files);
            free(fs->time_offsets);
            free(fs);
            return NULL;
        }

        size_t time_size = grib_count_unique_times(
            (GribFileData *)fs->files[i]->grib_data);
        fs->time_offsets[i + 1] = fs->time_offsets[i] + time_size;
        printf("  GRIB file %d: %zu time steps (offset %zu)\n",
               i, time_size, fs->time_offsets[i]);
    }

    fs->n_files = n_files;
    fs->total_times = fs->time_offsets[n_files];
    fs->base_filename = strdup(sorted[0]);

    grib_fileset_collect_times(fs);

    printf("Total virtual time steps: %zu across %d GRIB files\n",
           fs->total_times, n_files);
    if (fs->grib_times && fs->grib_n_times > 0) {
        printf("GRIB unique time steps: %zu\n", fs->grib_n_times);
    }

    for (int i = 0; i < n_files; i++) free(sorted[i]);
    free(sorted);

    return fs;
}

USFileSet *grib_open_glob(const char *pattern) {
    if (!pattern) return NULL;

    glob_t glob_result;
    int ret = glob(pattern, GLOB_TILDE | GLOB_NOSORT, NULL, &glob_result);
    if (ret != 0) {
        if (ret == GLOB_NOMATCH)
            fprintf(stderr, "No GRIB files match pattern: %s\n", pattern);
        else
            fprintf(stderr, "Glob error for pattern: %s\n", pattern);
        return NULL;
    }
    if (glob_result.gl_pathc == 0) {
        fprintf(stderr, "No GRIB files match pattern: %s\n", pattern);
        globfree(&glob_result);
        return NULL;
    }

    printf("GRIB pattern '%s' matched %zu files\n", pattern, glob_result.gl_pathc);

    USFileSet *fs = grib_open_fileset((const char **)glob_result.gl_pathv,
                                       (int)glob_result.gl_pathc);
    globfree(&glob_result);
    return fs;
}

int grib_fileset_map_time(USFileSet *fs, size_t virtual_time,
                          int *file_idx_out, size_t *local_time_out) {
    if (!fs || !file_idx_out || !local_time_out) return -1;
    if (virtual_time >= fs->total_times) return -1;

    int lo = 0, hi = fs->n_files - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (fs->time_offsets[mid] <= virtual_time)
            lo = mid;
        else
            hi = mid - 1;
    }

    *file_idx_out = lo;
    *local_time_out = virtual_time - fs->time_offsets[lo];
    return 0;
}

size_t grib_fileset_total_times(USFileSet *fs) {
    if (!fs) return 0;
    if (fs->grib_times && fs->grib_n_times > 0) {
        return fs->grib_n_times;
    }
    return fs->total_times;
}

int grib_read_slice_fileset(USFileSet *fs, USVar *var,
                            size_t virtual_time, size_t depth_idx, float *data) {
    if (!fs || !var || !data) return -1;

    size_t mapped_time = virtual_time;
    if (fs->grib_times && fs->grib_n_times > 0) {
        if (virtual_time >= fs->grib_n_times) {
            fprintf(stderr, "Invalid GRIB virtual time index: %zu\n", virtual_time);
            return -1;
        }

        mapped_time = fs->total_times;
        for (size_t t = 0; t < fs->total_times; t++) {
            int file_idx = 0;
            size_t local_time = 0;
            if (grib_fileset_map_time(fs, t, &file_idx, &local_time) != 0) continue;
            USFile *file = fs->files[file_idx];
            if (!file || !file->vars) {
                if (file && var->mesh) grib_scan_variables(file, var->mesh);
            }
            USVar *file_var = file ? grib_find_var(file, var->name) : NULL;
            if (!file_var || !file_var->grib_data) continue;
            GribVarData *fdata = (GribVarData *)file_var->grib_data;
            if (local_time < fdata->n_times &&
                fdata->times[local_time] == fs->grib_times[virtual_time]) {
                mapped_time = t;
                break;
            }
        }

        if (mapped_time == fs->total_times) {
            size_t n_points = var->mesh ? var->mesh->n_points : 0;
            for (size_t i = 0; i < n_points; i++) {
                data[i] = var->fill_value;
            }
            return 0;
        }
    }

    int file_idx;
    size_t local_time;
    if (grib_fileset_map_time(fs, mapped_time, &file_idx, &local_time) != 0) {
        fprintf(stderr, "Invalid virtual time index: %zu\n", mapped_time);
        return -1;
    }

    USFile *file = fs->files[file_idx];

    /* Lazily scan variables for this file if not done yet */
    if (!file->vars && var->mesh) {
        grib_scan_variables(file, var->mesh);
    }

    /* Find matching variable by name */
    USVar *file_var = grib_find_var(file, var->name);
    if (!file_var) {
        size_t n_points = var->mesh ? var->mesh->n_points : 0;
        for (size_t i = 0; i < n_points; i++) {
            data[i] = var->fill_value;
        }
        return 0;
    }

    return grib_read_slice(file_var, local_time, depth_idx, data);
}

USDimInfo *grib_get_dim_info_fileset(USFileSet *fs, USVar *var, int *n_dims_out) {
    if (!fs || !var || !n_dims_out || fs->n_files == 0) return NULL;

    if (fs->grib_times && fs->grib_n_times > 0 && var->time_dim_id >= 0) {
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
        USDimInfo *time_dim = &dims[idx++];
        strncpy(time_dim->name, GRIB_TIME_DIM_NAME, MAX_NAME_LEN - 1);
        strncpy(time_dim->units, "days since 1970-01-01", MAX_NAME_LEN - 1);
        time_dim->size = fs->grib_n_times;
        time_dim->current = 0;
        time_dim->values = malloc(fs->grib_n_times * sizeof(double));
        if (time_dim->values) {
            memcpy(time_dim->values, fs->grib_times, fs->grib_n_times * sizeof(double));
            time_dim->min_val = time_dim->values[0];
            time_dim->max_val = time_dim->values[fs->grib_n_times - 1];
        }
        time_dim->is_scannable = (time_dim->size > 1);

        if (var->depth_dim_id >= 0) {
            GribVarData *data = (GribVarData *)var->grib_data;
            USDimInfo *depth_dim = &dims[idx++];
            strncpy(depth_dim->name, GRIB_DEPTH_DIM_NAME, MAX_NAME_LEN - 1);
            strncpy(depth_dim->units, data->type_of_level, MAX_NAME_LEN - 1);
            depth_dim->size = data->n_levels;
            depth_dim->current = 0;
            depth_dim->values = malloc(data->n_levels * sizeof(double));
            if (depth_dim->values) {
                memcpy(depth_dim->values, data->levels, data->n_levels * sizeof(double));
                depth_dim->min_val = depth_dim->values[0];
                depth_dim->max_val = depth_dim->values[data->n_levels - 1];
            }
            depth_dim->is_scannable = (depth_dim->size > 1);
        }

        *n_dims_out = n_scannable;
        return dims;
    }

    /* Get base dim info from the variable (file 0) */
    USDimInfo *dims = grib_get_dim_info(var, n_dims_out);
    if (!dims) return NULL;

    /* Update time dimension with virtual total and concatenated values */
    for (int i = 0; i < *n_dims_out; i++) {
        if (var->time_dim_id >= 0 &&
            strcmp(dims[i].name, GRIB_TIME_DIM_NAME) == 0) {
            double *old_values = dims[i].values;

            dims[i].size = fs->total_times;
            dims[i].is_scannable = (fs->total_times > 1);

            /* Concatenate time values from all files */
            dims[i].values = malloc(fs->total_times * sizeof(double));
            if (dims[i].values) {
                size_t offset = 0;
                for (int f = 0; f < fs->n_files; f++) {
                    USFile *file = fs->files[f];
                    size_t file_times = fs->time_offsets[f + 1] - fs->time_offsets[f];

                    /* Lazily scan variables if needed */
                    if (!file->vars && var->mesh) {
                        grib_scan_variables(file, var->mesh);
                    }

                    USVar *file_var = grib_find_var(file, var->name);
                    if (file_var && file_var->grib_data) {
                        GribVarData *fdata = (GribVarData *)file_var->grib_data;
                        size_t copy_count = fdata->n_times < file_times ?
                                            fdata->n_times : file_times;
                        for (size_t t = 0; t < copy_count; t++) {
                            dims[i].values[offset + t] = fdata->times[t];
                        }
                        for (size_t t = copy_count; t < file_times; t++) {
                            dims[i].values[offset + t] = (double)(offset + t);
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

int grib_read_timeseries_fileset(USFileSet *fs, USVar *var,
                                 size_t node_idx, size_t depth_idx,
                                 double **times_out, float **values_out,
                                 int **valid_out, size_t *n_out) {
    if (!fs || !var || !times_out || !values_out || !valid_out || !n_out)
        return -1;

    *times_out = NULL;
    *values_out = NULL;
    *valid_out = NULL;
    *n_out = 0;

    size_t total = fs->grib_times && fs->grib_n_times > 0 ?
                   fs->grib_n_times : fs->total_times;
    if (total == 0) return -1;

    double *times = calloc(total, sizeof(double));
    float *values = calloc(total, sizeof(float));
    int *valid = calloc(total, sizeof(int));
    if (!times || !values || !valid) {
        free(times); free(values); free(valid);
        return -1;
    }

    if (fs->grib_times && fs->grib_n_times > 0) {
        for (size_t t = 0; t < fs->grib_n_times; t++) {
            times[t] = fs->grib_times[t];
            values[t] = var->fill_value;
            valid[t] = 0;

            for (size_t vt = 0; vt < fs->total_times; vt++) {
                int file_idx = 0;
                size_t local_time = 0;
                if (grib_fileset_map_time(fs, vt, &file_idx, &local_time) != 0) continue;
                USFile *file = fs->files[file_idx];
                if (!file) continue;
                if (!file->vars && var->mesh) {
                    grib_scan_variables(file, var->mesh);
                }

                USVar *file_var = grib_find_var(file, var->name);
                if (!file_var || !file_var->grib_data) continue;
                GribVarData *fdata = (GribVarData *)file_var->grib_data;
                if (local_time >= fdata->n_times) continue;
                if (fdata->times[local_time] != fs->grib_times[t]) continue;

                double *file_ts_times = NULL;
                float *file_ts_values = NULL;
                int *file_ts_valid = NULL;
                size_t file_ts_n = 0;

                int rc = grib_read_timeseries(file_var, node_idx, depth_idx,
                                               &file_ts_times, &file_ts_values,
                                               &file_ts_valid, &file_ts_n);
                if (rc == 0 && local_time < file_ts_n) {
                    values[t] = file_ts_values[local_time];
                    valid[t] = file_ts_valid[local_time];
                }

                free(file_ts_times);
                free(file_ts_values);
                free(file_ts_valid);
                break;
            }
        }

        *times_out = times;
        *values_out = values;
        *valid_out = valid;
        *n_out = total;
        return 0;
    }

    size_t out_idx = 0;
    for (int f = 0; f < fs->n_files; f++) {
        USFile *file = fs->files[f];
        size_t file_times = fs->time_offsets[f + 1] - fs->time_offsets[f];

        /* Lazily scan variables if needed */
        if (!file->vars && var->mesh) {
            grib_scan_variables(file, var->mesh);
        }

        USVar *file_var = grib_find_var(file, var->name);
        if (!file_var) {
            for (size_t t = 0; t < file_times; t++) {
                times[out_idx + t] = (double)(out_idx + t);
                values[out_idx + t] = var->fill_value;
                valid[out_idx + t] = 0;
            }
            out_idx += file_times;
            continue;
        }

        /* Read timeseries from this file */
        double *file_ts_times = NULL;
        float *file_ts_values = NULL;
        int *file_ts_valid = NULL;
        size_t file_ts_n = 0;

        int rc = grib_read_timeseries(file_var, node_idx, depth_idx,
                                       &file_ts_times, &file_ts_values,
                                       &file_ts_valid, &file_ts_n);
        if (rc != 0 || file_ts_n == 0) {
            for (size_t t = 0; t < file_times; t++) {
                times[out_idx + t] = (double)(out_idx + t);
                values[out_idx + t] = var->fill_value;
                valid[out_idx + t] = 0;
            }
        } else {
            size_t copy_count = file_ts_n < file_times ? file_ts_n : file_times;
            memcpy(&times[out_idx], file_ts_times, copy_count * sizeof(double));
            memcpy(&values[out_idx], file_ts_values, copy_count * sizeof(float));
            memcpy(&valid[out_idx], file_ts_valid, copy_count * sizeof(int));
            for (size_t t = copy_count; t < file_times; t++) {
                times[out_idx + t] = (double)(out_idx + t);
                values[out_idx + t] = var->fill_value;
                valid[out_idx + t] = 0;
            }
        }

        free(file_ts_times);
        free(file_ts_values);
        free(file_ts_valid);
        out_idx += file_times;
    }

    *times_out = times;
    *values_out = values;
    *valid_out = valid;
    *n_out = total;
    return 0;
}

void grib_close_fileset(USFileSet *fs) {
    if (!fs) return;
    for (int i = 0; i < fs->n_files; i++) {
        if (fs->files[i]) grib_close(fs->files[i]);
    }
    free(fs->files);
    free(fs->time_offsets);
    free(fs->base_filename);
    free(fs->grib_times);
    free(fs);
}

#endif /* HAVE_GRIB */
