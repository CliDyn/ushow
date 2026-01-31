/*
 * view.c - View state management
 */

#include "view.h"
#include "file_netcdf.h"
#include "regrid.h"
#include "colormaps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Default scale factor for display */
#define DEFAULT_SCALE_FACTOR 2

USView *view_create(void) {
    USView *view = calloc(1, sizeof(USView));
    if (!view) return NULL;
    view->frame_delay_ms = 200;  /* Default animation speed */
    view->scale_factor = DEFAULT_SCALE_FACTOR;
    return view;
}

int view_set_variable(USView *view, USVar *var, USMesh *mesh, USRegrid *regrid) {
    if (!view || !var || !mesh || !regrid) return -1;

    view->variable = var;
    view->mesh = mesh;
    view->regrid = regrid;

    /* Get dimension info */
    view->n_times = (var->time_dim_id >= 0) ? var->dim_sizes[var->time_dim_id] : 1;
    view->n_depths = (var->depth_dim_id >= 0) ? var->dim_sizes[var->depth_dim_id] : 1;
    view->time_index = 0;
    view->depth_index = 0;

    /* Get target grid dimensions */
    size_t nx, ny;
    regrid_get_target_dims(regrid, &nx, &ny);
    view->data_nx = nx;
    view->data_ny = ny;

    /* Apply scale factor for display */
    view->display_nx = nx * view->scale_factor;
    view->display_ny = ny * view->scale_factor;

    /* Allocate buffers */
    size_t n_points = mesh->n_points;
    size_t n_data = nx * ny;
    size_t n_display = view->display_nx * view->display_ny;

    free(view->raw_data);
    view->raw_data = malloc(n_points * sizeof(float));
    view->raw_data_size = n_points;

    free(view->regridded_data);
    view->regridded_data = malloc(n_data * sizeof(float));

    free(view->pixels);
    view->pixels = malloc(n_display * 3);  /* RGB */

    if (!view->raw_data || !view->regridded_data || !view->pixels) {
        fprintf(stderr, "Failed to allocate view buffers\n");
        return -1;
    }

    /* Estimate data range if not set */
    if (!var->range_set) {
        netcdf_estimate_range(var, &var->global_min, &var->global_max);
        var->user_min = var->global_min;
        var->user_max = var->global_max;
        var->range_set = 1;
    }

    view->data_valid = 0;
    return 0;
}

int view_set_time(USView *view, size_t time_idx) {
    if (!view || !view->variable) return -1;
    if (time_idx >= view->n_times) time_idx = view->n_times - 1;

    view->time_index = time_idx;
    view->data_valid = 0;
    return 0;
}

int view_set_depth(USView *view, size_t depth_idx) {
    if (!view || !view->variable) return -1;
    if (depth_idx >= view->n_depths) depth_idx = view->n_depths - 1;

    view->depth_index = depth_idx;
    view->data_valid = 0;
    return 0;
}

int view_step_time(USView *view, int delta) {
    if (!view) return -1;

    int new_idx = (int)view->time_index + delta;
    if (new_idx < 0) {
        new_idx = 0;
        return -1;  /* At beginning */
    }
    if (new_idx >= (int)view->n_times) {
        new_idx = view->n_times - 1;
        return -1;  /* At end */
    }

    view->time_index = new_idx;
    view->data_valid = 0;
    return new_idx;
}

int view_update(USView *view) {
    if (!view || !view->variable || !view->mesh || !view->regrid) return -1;

    /* Read data slice */
    if (netcdf_read_slice(view->variable, view->time_index, view->depth_index,
                          view->raw_data) != 0) {
        fprintf(stderr, "Failed to read data slice\n");
        return -1;
    }

    /* Apply regridding */
    regrid_apply(view->regrid, view->raw_data,
                 view->variable->fill_value, view->regridded_data);

    /* Convert to pixels with scaling */
    USColormap *cmap = colormap_get_current();
    if (cmap) {
        colormap_apply_scaled(cmap, view->regridded_data,
                              view->data_nx, view->data_ny,
                              view->variable->user_min, view->variable->user_max,
                              view->variable->fill_value,
                              view->pixels,
                              view->scale_factor);
    }

    view->data_valid = 1;
    return 0;
}

unsigned char *view_get_pixels(USView *view, size_t *width, size_t *height) {
    if (!view) return NULL;
    if (width) *width = view->display_nx;
    if (height) *height = view->display_ny;
    return view->pixels;
}

void view_free(USView *view) {
    if (!view) return;
    free(view->raw_data);
    free(view->regridded_data);
    free(view->pixels);
    free(view);
}
