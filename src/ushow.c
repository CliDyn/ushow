/*
 * ushow.c - Main entry point for ushow
 *
 * Unstructured data visualization tool
 */

#include "ushow.defines.h"
#include "mesh.h"
#include "regrid.h"
#include "file_netcdf.h"
#ifdef HAVE_ZARR
#include "file_zarr.h"
#endif
#ifdef HAVE_GRIB
#include "file_grib.h"
#endif
#include "colormaps.h"
#include "view.h"
#include "interface/x_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <glob.h>

/* Global state */
static USFile *file = NULL;
static USFileSet *fileset = NULL;  /* Multi-file set (NULL for single file) */
#ifdef HAVE_ZARR
static USFileSet *zarr_fileset = NULL;  /* Multi-file zarr set */
#endif
static USMesh *mesh = NULL;
static USRegrid *regrid = NULL;
static USView *view = NULL;
static USVar *variables = NULL;
static USVar *current_var = NULL;
static int n_variables = 0;
static int animating = 0;
static USDimInfo *current_dim_info = NULL;
static int n_current_dims = 0;

/* Options */
static USOptions options = {
    .debug = 0,
    .influence_radius = DEFAULT_INFLUENCE_RADIUS_M,
    .target_resolution = DEFAULT_RESOLUTION,
    .frame_delay_ms = 200
};

/* Forward declarations */
static void update_display(void);
static void animation_tick(void);
static void update_dim_info_current(void);
static void update_dim_label(void);
static int format_time_from_units(char *out, size_t outlen, double value, const char *units);
static void on_mouse_click(int px, int py);

/* Callbacks */
static void on_var_select(int var_index) {
    /* Find variable by index */
    USVar *var = variables;
    for (int i = 0; i < var_index && var; i++) {
        var = var->next;
    }
    if (!var) return;

    USVar *prev_var = current_var;
    current_var = var;
    view_set_variable(view, var, mesh, regrid);

    /* Update UI */
    x_update_var_name(var->name);
    x_update_range_label(var->user_min, var->user_max);
    x_update_time(view->time_index, view->n_times);
    x_update_depth(view->depth_index, view->n_depths);

    /* Set up dimension info panel */
    if (current_dim_info) {
#ifdef HAVE_GRIB
        if (prev_var && prev_var->file && prev_var->file->file_type == FILE_TYPE_GRIB) {
            grib_free_dim_info(current_dim_info, n_current_dims);
        } else
#endif
        {
            netcdf_free_dim_info(current_dim_info, n_current_dims);
        }
    }
    /* Use fileset dimension info if available (includes virtual time) */
#ifdef HAVE_ZARR
    if (zarr_fileset) {
        current_dim_info = zarr_get_dim_info_fileset(zarr_fileset, var, &n_current_dims);
    } else if (var->file && var->file->file_type == FILE_TYPE_ZARR) {
        current_dim_info = zarr_get_dim_info(var, &n_current_dims);
    } else
#endif
#ifdef HAVE_GRIB
    if (var->file && var->file->file_type == FILE_TYPE_GRIB) {
        current_dim_info = grib_get_dim_info(var, &n_current_dims);
    } else
#endif
    if (fileset) {
        current_dim_info = netcdf_get_dim_info_fileset(fileset, var, &n_current_dims);
    } else {
        current_dim_info = netcdf_get_dim_info(var, &n_current_dims);
    }
    if (!current_dim_info || n_current_dims == 0) {
        int count = 0;
        if (var->time_dim_id >= 0) count++;
        if (var->depth_dim_id >= 0) count++;
        if (count > 0) {
            USDimInfo *fallback = calloc(count, sizeof(USDimInfo));
            if (fallback) {
                int idx = 0;
                if (var->time_dim_id >= 0) {
                    USDimInfo *di = &fallback[idx++];
                    strncpy(di->name, var->dim_names[var->time_dim_id], MAX_NAME_LEN - 1);
                    di->size = var->dim_sizes[var->time_dim_id];
                    di->current = 0;
                    di->min_val = 0;
                    di->max_val = (di->size > 0) ? (double)(di->size - 1) : 0.0;
                    di->values = NULL;
                    di->is_scannable = (di->size > 1);
                }
                if (var->depth_dim_id >= 0) {
                    USDimInfo *di = &fallback[idx++];
                    strncpy(di->name, var->dim_names[var->depth_dim_id], MAX_NAME_LEN - 1);
                    di->size = var->dim_sizes[var->depth_dim_id];
                    di->current = 0;
                    di->min_val = 0;
                    di->max_val = (di->size > 0) ? (double)(di->size - 1) : 0.0;
                    di->values = NULL;
                    di->is_scannable = (di->size > 1);
                }
                current_dim_info = fallback;
                n_current_dims = count;
            }
        }
    }
    x_update_dim_info(current_dim_info, n_current_dims);
    update_dim_info_current();
    update_dim_label();

    update_display();
}

static void on_time_change(size_t time_idx) {
    if (!view || !current_var) return;
    view_set_time(view, time_idx);
    x_update_time(view->time_index, view->n_times);
    update_dim_info_current();
    update_dim_label();
    update_display();
}

static void on_depth_change(size_t depth_idx) {
    if (!view || !current_var) return;
    view_set_depth(view, depth_idx);
    x_update_depth(view->depth_index, view->n_depths);
    update_dim_info_current();
    update_dim_label();
    update_display();
}

static void on_animation(int direction) {
    if (direction == 0) {
        /* Pause */
        animating = 0;
        x_clear_timer();
    } else if (direction == 1) {
        /* Single step forward */
        animating = 0;
        x_clear_timer();
        if (view) {
            view_step_time(view, 1);
            x_update_time(view->time_index, view->n_times);
            update_dim_label();
            update_display();
        }
    } else if (direction == 2) {
        /* Continuous forward animation */
        if (!animating) {
            animating = 1;
            animation_tick();
        }
    } else if (direction == -1) {
        /* Single step back */
        animating = 0;
        x_clear_timer();
        if (view) {
            view_step_time(view, -1);
            x_update_time(view->time_index, view->n_times);
            update_dim_label();
            update_display();
        }
    } else if (direction == -2) {
        /* Rewind to start */
        animating = 0;
        x_clear_timer();
        if (view) {
            view_set_time(view, 0);
            x_update_time(view->time_index, view->n_times);
            update_dim_label();
            update_display();
        }
    }
}

static void on_colormap_change(void) {
    colormap_next();
    USColormap *cmap = colormap_get_current();
    if (cmap) {
        x_update_colormap_label(cmap->name);
        update_display();
    }
}

static void on_colormap_back(void) {
    colormap_prev();
    USColormap *cmap = colormap_get_current();
    if (cmap) {
        x_update_colormap_label(cmap->name);
        update_display();
    }
}

static void on_mouse_motion(int px, int py) {
    if (!view || !view->regrid || !view->regridded_data) return;

    /* Convert pixel coordinates to data grid coordinates */
    int scale = view->scale_factor;
    size_t data_x = px / scale;
    size_t data_y = py / scale;

    /* Bounds check */
    if (data_x >= view->data_nx || data_y >= view->data_ny) return;

    /* Convert to lon/lat (remember y is flipped in display) */
    size_t src_y = view->data_ny - 1 - data_y;
    double lon, lat;
    regrid_get_lonlat(view->regrid, data_x, src_y, &lon, &lat);

    /* Get data value */
    size_t idx = src_y * view->data_nx + data_x;
    float value = view->regridded_data[idx];

    /* Update display */
    x_update_value_label(lon, lat, value);
}

static void on_mouse_click(int px, int py) {
    if (!view || !view->regrid || !view->regridded_data || !current_var) return;

    /* Polygon-only mode: no regrid → no pixel-to-node mapping */
    if (view->render_mode == RENDER_MODE_POLYGON) {
        printf("Time series not available in polygon mode\n");
        return;
    }

    /* Need at least 2 time steps for a meaningful plot */
    if (view->n_times <= 1) {
        printf("Only 1 time step, no time series to display\n");
        return;
    }

    /* Convert pixel coordinates to data grid coordinates */
    int scale = view->scale_factor;
    size_t data_x = px / scale;
    size_t data_y = py / scale;

    if (data_x >= view->data_nx || data_y >= view->data_ny) return;

    /* Flip Y for display */
    size_t src_y = view->data_ny - 1 - data_y;

    /* Check valid mask */
    size_t grid_idx = src_y * view->data_nx + data_x;
    if (!view->regrid->valid_mask[grid_idx]) return;

    /* Get source mesh node index from nn_indices */
    size_t node_idx = view->regrid->nn_indices[grid_idx];

    /* Get lon/lat for display */
    double lon, lat;
    regrid_get_lonlat(view->regrid, data_x, src_y, &lon, &lat);

    printf("Extracting time series at lon=%.2f, lat=%.2f (node %zu)...\n", lon, lat, node_idx);

    /* Read time series data */
    double *times = NULL;
    float *values = NULL;
    int *valid = NULL;
    size_t n_out = 0;
    int rc;

#ifdef HAVE_ZARR
    if (zarr_fileset) {
        rc = zarr_read_timeseries_fileset(zarr_fileset, current_var, node_idx,
                                          view->depth_index, &times, &values, &valid, &n_out);
    } else if (current_var->file && current_var->file->file_type == FILE_TYPE_ZARR) {
        rc = zarr_read_timeseries(current_var, node_idx, view->depth_index,
                                  &times, &values, &valid, &n_out);
    } else
#endif
#ifdef HAVE_GRIB
    if (current_var->file && current_var->file->file_type == FILE_TYPE_GRIB) {
        rc = grib_read_timeseries(current_var, node_idx, view->depth_index,
                                  &times, &values, &valid, &n_out);
    } else
#endif
    if (fileset) {
        rc = netcdf_read_timeseries_fileset(fileset, current_var, node_idx,
                                            view->depth_index, &times, &values, &valid, &n_out);
    } else {
        rc = netcdf_read_timeseries(current_var, node_idx, view->depth_index,
                                    &times, &values, &valid, &n_out);
    }

    if (rc != 0 || n_out == 0) {
        printf("Failed to read time series\n");
        free(times); free(values); free(valid);
        return;
    }

    /* Build TSData */
    TSData ts_data;
    memset(&ts_data, 0, sizeof(ts_data));
    ts_data.times = times;
    ts_data.values = values;
    ts_data.valid = valid;
    ts_data.n_points = n_out;

    /* Count valid points */
    ts_data.n_valid = 0;
    for (size_t i = 0; i < n_out; i++) {
        if (valid[i]) ts_data.n_valid++;
    }

    /* Build title */
    if (current_var->units[0]) {
        snprintf(ts_data.title, sizeof(ts_data.title), "%s (%s) at %.2f, %.2f",
                 current_var->name, current_var->units, lon, lat);
    } else {
        snprintf(ts_data.title, sizeof(ts_data.title), "%s at %.2f, %.2f",
                 current_var->name, lon, lat);
    }

    /* Build axis labels from dimension info */
    ts_data.x_label[0] = '\0';
    ts_data.y_label[0] = '\0';

    if (current_dim_info && current_var->time_dim_id >= 0) {
        const char *time_dim_name = current_var->dim_names[current_var->time_dim_id];
        for (int i = 0; i < n_current_dims; i++) {
            if (strcmp(current_dim_info[i].name, time_dim_name) == 0) {
                if (current_dim_info[i].units[0]) {
                    strncpy(ts_data.x_label, current_dim_info[i].units, sizeof(ts_data.x_label) - 1);
                }
                break;
            }
        }
    }
    if (!ts_data.x_label[0]) {
        strncpy(ts_data.x_label, "Time Step", sizeof(ts_data.x_label) - 1);
    }

    if (current_var->units[0]) {
        snprintf(ts_data.y_label, sizeof(ts_data.y_label), "%s (%s)",
                 current_var->name, current_var->units);
    } else {
        strncpy(ts_data.y_label, current_var->name, sizeof(ts_data.y_label) - 1);
    }

    printf("Time series: %zu points (%zu valid)\n", n_out, ts_data.n_valid);

    /* Show popup */
    x_show_timeseries(&ts_data);

    /* Free data (popup makes a deep copy) */
    free(times);
    free(values);
    free(valid);
}

static void on_range_adjust(int action) {
    if (!current_var) return;

    float range = current_var->user_max - current_var->user_min;
    float step = range * 0.1f;  /* 10% adjustment */
    if (step < 0.001f) step = 0.001f;

    switch (action) {
        case 0:  /* min down */
            current_var->user_min -= step;
            break;
        case 1:  /* min up */
            current_var->user_min += step;
            if (current_var->user_min >= current_var->user_max - step) {
                current_var->user_min = current_var->user_max - step;
            }
            break;
        case 2:  /* max down */
            current_var->user_max -= step;
            if (current_var->user_max <= current_var->user_min + step) {
                current_var->user_max = current_var->user_min + step;
            }
            break;
        case 3:  /* max up */
            current_var->user_max += step;
            break;
    }

    x_update_range_label(current_var->user_min, current_var->user_max);
    update_display();
}

static void on_zoom(int delta) {
    if (!view) return;

    int new_scale = view->scale_factor + delta;
    if (view_set_scale(view, new_scale) == 0) {
        printf("Zoom: %dx\n", view->scale_factor);
        update_display();
    }
}

static void on_save(void) {
    if (!view || !current_var) return;

    /* Generate filename from variable name and time/depth indices */
    char filename[512];
    snprintf(filename, sizeof(filename), "%s_t%zu_d%zu.ppm",
             current_var->name, view->time_index, view->depth_index);

    if (view_save_ppm(view, filename) == 0) {
        printf("Saved: %s (%zux%zu pixels)\n", filename,
               view->display_nx, view->display_ny);
    } else {
        fprintf(stderr, "Failed to save image\n");
    }
}

static void on_range_button(void) {
    if (!current_var) return;

    float new_min, new_max;
    int result = x_range_popup_show(
        current_var->user_min, current_var->user_max,
        current_var->global_min, current_var->global_max,
        &new_min, &new_max);

    if (result == 1) {  /* RANGE_POPUP_OK */
        current_var->user_min = new_min;
        current_var->user_max = new_max;
        x_update_range_label(current_var->user_min, current_var->user_max);
        update_display();
    }
}

static void on_render_mode_toggle(void) {
    if (!view) return;
    
    /* In polygon-only mode, don't allow switching */
    if (options.polygon_only) {
        printf("Polygon-only mode: cannot switch to interpolate mode\n");
        return;
    }
    
    int result = view_toggle_render_mode(view);
    if (result >= 0) {
        const char *mode_name = (result == RENDER_MODE_POLYGON) ? "Polygon" : "Interp";
        x_update_render_mode_label(mode_name);
        printf("Render mode: %s\n", mode_name);
        update_display();
    } else {
        printf("Polygon mode not available (no mesh connectivity loaded)\n");
    }
}

static void update_dim_info_current(void) {
    if (!view || !current_dim_info) return;

    /* Update current values for each dimension in the panel */
    int dim_idx = 0;
    for (int i = 0; i < n_current_dims; i++) {
        USDimInfo *di = &current_dim_info[i];
        size_t current_idx;
        double current_val;

        /* Determine which dimension this is (time or depth) */
        if (current_var->time_dim_id >= 0 &&
            strcmp(di->name, current_var->dim_names[current_var->time_dim_id]) == 0) {
            current_idx = view->time_index;
        } else if (current_var->depth_dim_id >= 0 &&
                   strcmp(di->name, current_var->dim_names[current_var->depth_dim_id]) == 0) {
            current_idx = view->depth_index;
        } else {
            continue;
        }

        di->current = current_idx;
        if (di->values && current_idx < di->size) {
            current_val = di->values[current_idx];
        } else {
            current_val = (double)current_idx;
        }

        x_update_dim_current(dim_idx, current_idx, current_val);
        dim_idx++;
    }
}

static int parse_time_units(const char *units, double *unit_seconds,
                            int *y, int *mo, int *d, int *h, int *mi, double *sec) {
    if (!units || !unit_seconds || !y || !mo || !d || !h || !mi || !sec) return 0;

    const char *since = strstr(units, "since");
    if (!since) return 0;

    char unit_buf[32] = {0};
    if (sscanf(units, "%31s", unit_buf) != 1) return 0;

    /* Normalize unit token to lower case */
    for (char *p = unit_buf; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    }

    if (strcmp(unit_buf, "seconds") == 0 || strcmp(unit_buf, "second") == 0 ||
        strcmp(unit_buf, "secs") == 0 || strcmp(unit_buf, "sec") == 0 || strcmp(unit_buf, "s") == 0) {
        *unit_seconds = 1.0;
    } else if (strcmp(unit_buf, "minutes") == 0 || strcmp(unit_buf, "minute") == 0 ||
               strcmp(unit_buf, "mins") == 0 || strcmp(unit_buf, "min") == 0) {
        *unit_seconds = 60.0;
    } else if (strcmp(unit_buf, "hours") == 0 || strcmp(unit_buf, "hour") == 0 ||
               strcmp(unit_buf, "hrs") == 0 || strcmp(unit_buf, "hr") == 0) {
        *unit_seconds = 3600.0;
    } else if (strcmp(unit_buf, "days") == 0 || strcmp(unit_buf, "day") == 0) {
        *unit_seconds = 86400.0;
    } else {
        return 0;
    }

    /* Parse origin date/time after "since" */
    const char *p = since + 5;
    while (*p == ' ') p++;
    int n = sscanf(p, "%d-%d-%d %d:%d:%lf", y, mo, d, h, mi, sec);
    if (n < 3) return 0;
    if (n == 3) { *h = 0; *mi = 0; *sec = 0.0; }
    return 1;
}

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)(era * 146097 + (int)doe - 719468);
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y_tmp = (int)(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d_tmp = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m_tmp = mp + (mp < 10 ? 3 : -9);
    y_tmp += (m_tmp <= 2);

    *y = y_tmp;
    *m = m_tmp;
    *d = d_tmp;
}

static int format_time_from_units(char *out, size_t outlen, double value, const char *units) {
    double unit_seconds = 0.0;
    int y, mo, d, h, mi;
    double sec;
    if (!parse_time_units(units, &unit_seconds, &y, &mo, &d, &h, &mi, &sec)) {
        return 0;
    }

    int64_t days = days_from_civil(y, (unsigned)mo, (unsigned)d);
    double total_sec = (double)days * 86400.0 + (double)h * 3600.0 + (double)mi * 60.0 + sec;
    total_sec += value * unit_seconds;

    int64_t out_days = (int64_t)(total_sec / 86400.0);
    double rem = total_sec - (double)out_days * 86400.0;
    if (rem < 0) {
        rem += 86400.0;
        out_days -= 1;
    }

    int out_y;
    unsigned out_m, out_d;
    civil_from_days(out_days, &out_y, &out_m, &out_d);

    int out_h = (int)(rem / 3600.0);
    rem -= out_h * 3600.0;
    int out_mi = (int)(rem / 60.0);
    rem -= out_mi * 60.0;
    int out_s = (int)(rem + 0.5);

    snprintf(out, outlen, "%04d-%02u-%02u %02d:%02d:%02d",
             out_y, out_m, out_d, out_h, out_mi, out_s);
    return 1;
}

static const USDimInfo *find_dim_info_for_dim(const char *dim_name) {
    if (!current_dim_info || !dim_name) return NULL;
    for (int i = 0; i < n_current_dims; i++) {
        if (strcmp(current_dim_info[i].name, dim_name) == 0) {
            return &current_dim_info[i];
        }
    }
    return NULL;
}

static void format_dim_label(char *buf, size_t buflen,
                             const char *label,
                             size_t idx, size_t total,
                             const USDimInfo *di,
                             int is_time) {
    if (!buf || buflen == 0) return;

    if (di && di->values && idx < di->size) {
        if (is_time && di->units[0]) {
            char time_buf[64];
            if (format_time_from_units(time_buf, sizeof(time_buf), di->values[idx], di->units)) {
                /* Prefer date-only to keep the header compact */
                char date_buf[16];
                strncpy(date_buf, time_buf, 10);
                date_buf[10] = '\0';
                snprintf(buf, buflen, "%s %zu/%zu (%s)", label, idx + 1, total, date_buf);
            } else if (di->units[0]) {
                snprintf(buf, buflen, "%s %zu/%zu (%.4g %s)",
                         label, idx + 1, total, di->values[idx], di->units);
            } else {
                snprintf(buf, buflen, "%s %zu/%zu (%.4g)",
                         label, idx + 1, total, di->values[idx]);
            }
        } else if (di->units[0]) {
            snprintf(buf, buflen, "%s %zu/%zu (%.4g %s)",
                     label, idx + 1, total, di->values[idx], di->units);
        } else {
            snprintf(buf, buflen, "%s %zu/%zu (%.4g)",
                     label, idx + 1, total, di->values[idx]);
        }
    } else {
        snprintf(buf, buflen, "%s %zu/%zu", label, idx + 1, total);
    }
}

static void update_dim_label(void) {
    if (!view || !current_var) return;

    char time_buf[300] = "Time: -/-";
    char depth_buf[300] = "Depth: -/-";

    if (current_var->time_dim_id >= 0) {
        const char *name = current_var->dim_names[current_var->time_dim_id];
        const USDimInfo *di = find_dim_info_for_dim(name);
        format_dim_label(time_buf, sizeof(time_buf), "Time:",
                         view->time_index, view->n_times, di, 1);
    }
    if (current_var->depth_dim_id >= 0) {
        const char *name = current_var->dim_names[current_var->depth_dim_id];
        const USDimInfo *di = find_dim_info_for_dim(name);
        format_dim_label(depth_buf, sizeof(depth_buf), "Depth:",
                         view->depth_index, view->n_depths, di, 0);
    }

    char combined[604];
    snprintf(combined, sizeof(combined), "%s  %s", time_buf, depth_buf);
    x_update_dim_label(combined);
}

static void on_dim_nav(int dim_index, int direction) {
    if (!view || !current_var || !current_dim_info) return;
    if (dim_index < 0 || dim_index >= n_current_dims) return;

    USDimInfo *di = &current_dim_info[dim_index];

    /* Determine which dimension this is and navigate it */
    if (current_var->time_dim_id >= 0 &&
        strcmp(di->name, current_var->dim_names[current_var->time_dim_id]) == 0) {
        /* Time dimension */
        int new_idx = (int)view->time_index + direction;
        if (new_idx < 0) new_idx = 0;
        if (new_idx >= (int)view->n_times) new_idx = (int)view->n_times - 1;
        view_set_time(view, (size_t)new_idx);
        x_update_time(view->time_index, view->n_times);
    } else if (current_var->depth_dim_id >= 0 &&
               strcmp(di->name, current_var->dim_names[current_var->depth_dim_id]) == 0) {
        /* Depth dimension */
        int new_idx = (int)view->depth_index + direction;
        if (new_idx < 0) new_idx = 0;
        if (new_idx >= (int)view->n_depths) new_idx = (int)view->n_depths - 1;
        view_set_depth(view, (size_t)new_idx);
        x_update_depth(view->depth_index, view->n_depths);
    } else {
        return;  /* Unknown dimension */
    }

    update_dim_info_current();
    update_dim_label();
    update_display();
}

static void animation_tick(void) {
    if (!animating || !view) return;

    int result = view_step_time(view, 1);
    if (result < 0) {
        /* Reached end, loop back to start */
        view_set_time(view, 0);
    }

    x_update_time(view->time_index, view->n_times);
    update_dim_label();
    update_display();

    /* Schedule next tick */
    if (animating) {
        x_set_timer(options.frame_delay_ms, animation_tick);
    }
}

static void update_display(void) {
    if (!view) return;

    view_update(view);

    size_t width, height;
    unsigned char *pixels = view_get_pixels(view, &width, &height);
    if (pixels) {
        x_update_image(pixels, width, height);

        /* Update colorbar (256 pixels wide, horizontal) */
        if (current_var) {
            x_update_colorbar(current_var->user_min, current_var->user_max, 256);
        }
    }
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <data_file.nc|data.grib|data.zarr> [file2 ...]\n\n", prog);
    fprintf(stderr, "Multi-file: Files are concatenated along time dimension.\n");
    fprintf(stderr, "Glob patterns are supported (quote them to prevent shell expansion).\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -m, --mesh <file>      Mesh file with coordinates\n");
    fprintf(stderr, "  -r, --resolution <deg> Target grid resolution (default: 1.0)\n");
    fprintf(stderr, "  -i, --influence <m>    Influence radius in meters (default: 200000)\n");
    fprintf(stderr, "  -d, --delay <ms>       Animation frame delay (default: 200)\n");
    fprintf(stderr, "  -p, --polygon-only     Skip regridding, use polygon mode only (faster)\n");
    fprintf(stderr, "  -h, --help             Show this help\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s data.nc                           # Single file\n", prog);
    fprintf(stderr, "  %s data.nc -m mesh.nc                # With separate mesh\n", prog);
    fprintf(stderr, "  %s \"data.*.nc\" -m mesh.nc           # Multi-file with glob\n", prog);
    fprintf(stderr, "  %s data.1960.nc data.1961.nc -m mesh # Multi-file explicit\n", prog);
}

int main(int argc, char *argv[]) {
    const char *mesh_filename = NULL;
    int n_data_files = 0;
    const char **data_filenames = NULL;

    /* Parse command line options */
    static struct option long_options[] = {
        {"mesh",         required_argument, 0, 'm'},
        {"resolution",   required_argument, 0, 'r'},
        {"influence",    required_argument, 0, 'i'},
        {"delay",        required_argument, 0, 'd'},
        {"polygon-only", no_argument,       0, 'p'},
        {"help",         no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:r:i:d:ph", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                mesh_filename = optarg;
                strncpy(options.mesh_file, optarg, MAX_NAME_LEN - 1);
                break;
            case 'r':
                options.target_resolution = atof(optarg);
                break;
            case 'i':
                options.influence_radius = atof(optarg);
                break;
            case 'd':
                options.frame_delay_ms = atoi(optarg);
                break;
            case 'p':
                options.polygon_only = 1;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No data file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Collect data file arguments */
    n_data_files = argc - optind;
    data_filenames = (const char **)&argv[optind];

    printf("=== ushow: Unstructured Data Viewer ===\n\n");

    /* Check if first argument looks like a glob pattern */
    int use_glob = 0;
    if (n_data_files == 1) {
        const char *fn = data_filenames[0];
        for (const char *p = fn; *p; p++) {
            if (*p == '*' || *p == '?' || *p == '[') {
                use_glob = 1;
                break;
            }
        }
    }

    if (use_glob) {
        printf("Glob pattern: %s\n", data_filenames[0]);
    } else if (n_data_files > 1) {
        printf("Data files: %d files\n", n_data_files);
        for (int i = 0; i < n_data_files && i < 3; i++) {
            printf("  %s\n", data_filenames[i]);
        }
        if (n_data_files > 3) {
            printf("  ... and %d more\n", n_data_files - 3);
        }
    } else {
        printf("Data file: %s\n", data_filenames[0]);
    }
    if (mesh_filename) printf("Mesh file: %s\n", mesh_filename);
    printf("Resolution: %.2f degrees\n", options.target_resolution);
    printf("Influence radius: %.0f m\n", options.influence_radius);
    printf("\n");

    /* Initialize colormaps */
    colormaps_init();

    /* Open data file(s) */
    printf("Opening data file(s)...\n");

    if (!use_glob && n_data_files == 1) {
#ifdef HAVE_ZARR
        /* Check if first file is a zarr store */
        if (zarr_is_zarr_store(data_filenames[0])) {
            printf("Detected zarr store: %s\n", data_filenames[0]);
            file = zarr_open(data_filenames[0]);
            if (!file) {
                fprintf(stderr, "Failed to open zarr store: %s\n", data_filenames[0]);
                return 1;
            }
        } else
#endif
#ifdef HAVE_GRIB
        if (grib_is_grib_file(data_filenames[0])) {
            printf("Detected GRIB file: %s\n", data_filenames[0]);
            file = grib_open(data_filenames[0]);
            if (!file) {
                fprintf(stderr, "Failed to open GRIB file: %s\n", data_filenames[0]);
                return 1;
            }
        } else
#endif
        {
            file = netcdf_open(data_filenames[0]);
            if (!file) {
                fprintf(stderr, "Failed to open data file: %s\n", data_filenames[0]);
                return 1;
            }
        }
    } else {
#ifdef HAVE_ZARR
        if (use_glob) {
            /* Check if glob pattern matches zarr stores by expanding and testing first match */
            glob_t test_glob;
            int is_zarr_glob = 0;
            if (glob(data_filenames[0], GLOB_TILDE | GLOB_NOSORT, NULL, &test_glob) == 0 &&
                test_glob.gl_pathc > 0) {
                is_zarr_glob = zarr_is_zarr_store(test_glob.gl_pathv[0]);
                globfree(&test_glob);
            }

            if (is_zarr_glob) {
                zarr_fileset = zarr_open_glob(data_filenames[0]);
                if (!zarr_fileset) {
                    fprintf(stderr, "Failed to open zarr stores matching: %s\n", data_filenames[0]);
                    return 1;
                }
                file = zarr_fileset->files[0];
            } else {
                fileset = netcdf_open_glob(data_filenames[0]);
                if (!fileset) {
                    fprintf(stderr, "Failed to open files matching: %s\n", data_filenames[0]);
                    return 1;
                }
                file = fileset->files[0];
            }
        } else if (n_data_files > 1 && zarr_is_zarr_store(data_filenames[0])) {
            /* Multiple explicit zarr files */
            zarr_fileset = zarr_open_fileset(data_filenames, n_data_files);
            if (!zarr_fileset) {
                fprintf(stderr, "Failed to open zarr stores\n");
                return 1;
            }
            file = zarr_fileset->files[0];
        } else
#endif
        if (use_glob) {
            /* Use glob pattern */
            fileset = netcdf_open_glob(data_filenames[0]);
            if (!fileset) {
                fprintf(stderr, "Failed to open files matching: %s\n", data_filenames[0]);
                return 1;
            }
            file = fileset->files[0];  /* Primary file for variable scanning */
        } else if (n_data_files > 1) {
            /* Multiple explicit files */
            fileset = netcdf_open_fileset(data_filenames, n_data_files);
            if (!fileset) {
                fprintf(stderr, "Failed to open data files\n");
                return 1;
            }
            file = fileset->files[0];  /* Primary file for variable scanning */
        } else {
            /* Single file fallback */
            file = netcdf_open(data_filenames[0]);
            if (!file) {
                fprintf(stderr, "Failed to open data file: %s\n", data_filenames[0]);
                return 1;
            }
        }
    }

    /* Load mesh */
    printf("Loading mesh...\n");
#ifdef HAVE_ZARR
    if (file->file_type == FILE_TYPE_ZARR) {
        /* For zarr, load coordinates from the store itself */
        mesh = mesh_create_from_zarr(file);
        if (!mesh) {
            fprintf(stderr, "Failed to load mesh from zarr store\n");
            zarr_close(file);
            return 1;
        }
    } else
#endif
#ifdef HAVE_GRIB
    if (file->file_type == FILE_TYPE_GRIB) {
        mesh = grib_create_mesh(file);
        if (!mesh) {
            fprintf(stderr, "Failed to load mesh from GRIB file\n");
            grib_close(file);
            return 1;
        }
    } else
#endif
    {
        mesh = mesh_create_from_netcdf(file->ncid, mesh_filename);
        if (!mesh) {
            fprintf(stderr, "Failed to load mesh\n");
            if (fileset) netcdf_close_fileset(fileset);
            else netcdf_close(file);
            return 1;
        }
    }

    /* Create regridding structure (skip if polygon-only mode) */
    if (!options.polygon_only) {
        printf("Creating regrid structure...\n");
        regrid = regrid_create(mesh, options.target_resolution, options.influence_radius);
        if (!regrid) {
            fprintf(stderr, "Failed to create regrid\n");
            mesh_free(mesh);
            netcdf_close(file);
            return 1;
        }
    } else {
        printf("Polygon-only mode: skipping regrid\n");
        if (mesh->n_elements == 0 || mesh->elem_nodes == NULL) {
            fprintf(stderr, "Error: --polygon-only requires mesh with element connectivity\n");
            fprintf(stderr, "Use -m <mesh.nc> to specify a mesh file with face_nodes\n");
            mesh_free(mesh);
            netcdf_close(file);
            return 1;
        }
    }

    /* Scan for variables */
    printf("Scanning for variables...\n");
#ifdef HAVE_ZARR
    if (file->file_type == FILE_TYPE_ZARR) {
        variables = zarr_scan_variables(file, mesh);
    } else
#endif
#ifdef HAVE_GRIB
    if (file->file_type == FILE_TYPE_GRIB) {
        variables = grib_scan_variables(file, mesh);
    } else
#endif
    {
        variables = netcdf_scan_variables(file, mesh);
    }
    if (!variables) {
        fprintf(stderr, "No displayable variables found\n");
        regrid_free(regrid);
        mesh_free(mesh);
#ifdef HAVE_ZARR
        if (file->file_type == FILE_TYPE_ZARR) {
            zarr_close(file);
        } else
#endif
#ifdef HAVE_GRIB
        if (file->file_type == FILE_TYPE_GRIB) {
            grib_close(file);
        } else
#endif
        if (fileset) netcdf_close_fileset(fileset);
        else netcdf_close(file);
        return 1;
    }

    /* Count variables */
    USVar *v = variables;
    while (v) {
        n_variables++;
        v = v->next;
    }

    /* Build variable name list for UI initialization */
    const char **var_names = malloc(n_variables * sizeof(char *));
    v = variables;
    for (int i = 0; i < n_variables; i++) {
        var_names[i] = v->name;
        v = v->next;
    }

    /* Build initial dimension info from variable with most scannable dims */
    USVar *max_var = variables;
    int max_scannable = -1;
    for (v = variables; v != NULL; v = v->next) {
        int count = 0;
        if (v->time_dim_id >= 0) count++;
        if (v->depth_dim_id >= 0) count++;
        if (count > max_scannable) {
            max_scannable = count;
            max_var = v;
        }
    }
    USDimInfo *init_dims = NULL;
    int n_init_dims = 0;
    if (max_var) {
        /* Use fileset dimension info if available (includes virtual time) */
#ifdef HAVE_ZARR
        if (zarr_fileset) {
            init_dims = zarr_get_dim_info_fileset(zarr_fileset, max_var, &n_init_dims);
        } else if (file->file_type == FILE_TYPE_ZARR) {
            init_dims = zarr_get_dim_info(max_var, &n_init_dims);
        } else
#endif
#ifdef HAVE_GRIB
        if (file->file_type == FILE_TYPE_GRIB) {
            init_dims = grib_get_dim_info(max_var, &n_init_dims);
        } else
#endif
        if (fileset) {
            init_dims = netcdf_get_dim_info_fileset(fileset, max_var, &n_init_dims);
        } else {
            init_dims = netcdf_get_dim_info(max_var, &n_init_dims);
        }
        if (!init_dims || n_init_dims == 0) {
            int count = 0;
            if (max_var->time_dim_id >= 0) count++;
            if (max_var->depth_dim_id >= 0) count++;
            if (count > 0) {
                init_dims = calloc(count, sizeof(USDimInfo));
                if (init_dims) {
                    int idx = 0;
                    if (max_var->time_dim_id >= 0) {
                        USDimInfo *di = &init_dims[idx++];
                        strncpy(di->name, max_var->dim_names[max_var->time_dim_id], MAX_NAME_LEN - 1);
                        di->size = max_var->dim_sizes[max_var->time_dim_id];
                        di->current = 0;
                        di->min_val = 0;
                        di->max_val = (di->size > 0) ? (double)(di->size - 1) : 0.0;
                        di->values = NULL;
                        di->is_scannable = (di->size > 1);
                    }
                    if (max_var->depth_dim_id >= 0) {
                        USDimInfo *di = &init_dims[idx++];
                        strncpy(di->name, max_var->dim_names[max_var->depth_dim_id], MAX_NAME_LEN - 1);
                        di->size = max_var->dim_sizes[max_var->depth_dim_id];
                        di->current = 0;
                        di->min_val = 0;
                        di->max_val = (di->size > 0) ? (double)(di->size - 1) : 0.0;
                        di->values = NULL;
                        di->is_scannable = (di->size > 1);
                    }
                    n_init_dims = count;
                }
            }
        }
    }

    /* Initialize X11 */
    printf("Initializing display...\n");
    if (x_init(&argc, argv, var_names, n_variables, init_dims, n_init_dims) != 0) {
        fprintf(stderr, "Failed to initialize X11 display\n");
        free(var_names);
        if (init_dims) {
#ifdef HAVE_GRIB
            if (file->file_type == FILE_TYPE_GRIB) {
                grib_free_dim_info(init_dims, n_init_dims);
            } else
#endif
            {
                netcdf_free_dim_info(init_dims, n_init_dims);
            }
        }
        regrid_free(regrid);
        mesh_free(mesh);
        if (file->file_type == FILE_TYPE_GRIB) {
            grib_close(file);
        } else {
            netcdf_close(file);
        }
        return 1;
    }
    free(var_names);
    if (init_dims) {
#ifdef HAVE_GRIB
        if (file->file_type == FILE_TYPE_GRIB) {
            grib_free_dim_info(init_dims, n_init_dims);
        } else
#endif
        {
            netcdf_free_dim_info(init_dims, n_init_dims);
        }
    }

    /* Set up callbacks */
    x_set_var_callback(on_var_select);
    x_set_time_callback(on_time_change);
    x_set_depth_callback(on_depth_change);
    x_set_animation_callback(on_animation);
    x_set_colormap_callback(on_colormap_change);
    x_set_colormap_back_callback(on_colormap_back);
    x_set_mouse_callback(on_mouse_motion);
    x_set_range_callback(on_range_adjust);
    x_set_zoom_callback(on_zoom);
    x_set_save_callback(on_save);
    x_set_dim_nav_callback(on_dim_nav);
    x_set_render_mode_callback(on_render_mode_toggle);
    x_set_range_button_callback(on_range_button);
    x_set_mouse_click_callback(on_mouse_click);

    /* Create view */
    view = view_create();

    /* Set fileset if using multiple files */
    if (fileset) {
        view_set_fileset(view, fileset);
    }
#ifdef HAVE_ZARR
    if (zarr_fileset) {
        view_set_fileset(view, zarr_fileset);
    }
#endif

    /* Set polygon-only mode if requested */
    if (options.polygon_only) {
        view->render_mode = RENDER_MODE_POLYGON;
        x_update_render_mode_label("Polygon");
    }

    /* Select first variable */
    on_var_select(0);

    /* Update colormap label */
    USColormap *cmap = colormap_get_current();
    if (cmap) {
        x_update_colormap_label(cmap->name);
    }

    printf("\nReady. Use variable buttons to select data.\n");
    printf("Controls: < Back | || Pause | Fwd >\n");
    printf("Click 'Colormap' to cycle through colormaps (right-click to go back).\n\n");

    /* Enter main loop */
    x_main_loop();

    /* Cleanup */
    x_cleanup();
    if (current_dim_info) {
#ifdef HAVE_GRIB
        if (current_var && current_var->file && current_var->file->file_type == FILE_TYPE_GRIB) {
            grib_free_dim_info(current_dim_info, n_current_dims);
        } else
#endif
        {
            netcdf_free_dim_info(current_dim_info, n_current_dims);
        }
    }
    view_free(view);
    regrid_free(regrid);
    mesh_free(mesh);
#ifdef HAVE_ZARR
    if (zarr_fileset) {
        zarr_close_fileset(zarr_fileset);
    } else if (file && file->file_type == FILE_TYPE_ZARR) {
        zarr_close(file);
    } else
#endif
    if (file && file->file_type == FILE_TYPE_GRIB) {
        grib_close(file);
    } else if (fileset) {
        netcdf_close_fileset(fileset);
    } else {
        netcdf_close(file);
    }
    colormaps_cleanup();

    return 0;
}
