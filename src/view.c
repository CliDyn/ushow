/*
 * view.c - View state management
 */

#include "view.h"
#include "file_netcdf.h"
#ifdef HAVE_ZARR
#include "file_zarr.h"
#endif
#include "regrid.h"
#include "colormaps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Default scale factor for display */
#define DEFAULT_SCALE_FACTOR 2

USView *view_create(void) {
    USView *view = calloc(1, sizeof(USView));
    if (!view) return NULL;
    view->frame_delay_ms = 200;  /* Default animation speed */
    view->scale_factor = DEFAULT_SCALE_FACTOR;
    return view;
}

void view_set_fileset(USView *view, USFileSet *fileset) {
    if (view) {
        view->fileset = fileset;
    }
}

int view_set_variable(USView *view, USVar *var, USMesh *mesh, USRegrid *regrid) {
    if (!view || !var || !mesh) return -1;
    /* regrid can be NULL in polygon-only mode */
    if (!regrid && view->render_mode != RENDER_MODE_POLYGON) return -1;

    view->variable = var;
    view->mesh = mesh;
    view->regrid = regrid;

    /* Get dimension info - use fileset total if available */
    if (view->fileset) {
        view->n_times = netcdf_fileset_total_times(view->fileset);
    } else {
        view->n_times = (var->time_dim_id >= 0) ? var->dim_sizes[var->time_dim_id] : 1;
    }
    view->n_depths = (var->depth_dim_id >= 0) ? var->dim_sizes[var->depth_dim_id] : 1;
    view->time_index = 0;
    view->depth_index = 0;

    /* Get target grid dimensions */
    size_t nx, ny;
    if (regrid) {
        regrid_get_target_dims(regrid, &nx, &ny);
    } else {
        /* Polygon-only mode: use fixed display size */
        nx = 720;  /* 0.5 degree equivalent */
        ny = 360;
    }
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
    if (regrid) {
        view->regridded_data = malloc(n_data * sizeof(float));
    } else {
        view->regridded_data = NULL;  /* Not needed in polygon-only mode */
    }

    free(view->pixels);
    view->pixels = malloc(n_display * 3);  /* RGB */

    if (!view->raw_data || !view->pixels) {
        fprintf(stderr, "Failed to allocate view buffers\n");
        return -1;
    }
    if (regrid && !view->regridded_data) {
        fprintf(stderr, "Failed to allocate regridded data buffer\n");
        return -1;
    }

    /* Estimate data range if not set */
    if (!var->range_set) {
#ifdef HAVE_ZARR
        if (var->file && var->file->file_type == FILE_TYPE_ZARR) {
            zarr_estimate_range(var, &var->global_min, &var->global_max);
        } else
#endif
        {
            netcdf_estimate_range(var, &var->global_min, &var->global_max);
        }
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

int view_set_scale(USView *view, int scale) {
    if (!view) return -1;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;  /* Cap at 8x zoom */

    if (view->scale_factor == scale) return 0;  /* No change */

    view->scale_factor = scale;

    /* Recalculate display dimensions */
    view->display_nx = view->data_nx * scale;
    view->display_ny = view->data_ny * scale;

    /* Reallocate pixel buffer */
    size_t n_display = view->display_nx * view->display_ny;
    free(view->pixels);
    view->pixels = malloc(n_display * 3);
    if (!view->pixels) {
        fprintf(stderr, "Failed to reallocate pixel buffer\n");
        return -1;
    }

    view->data_valid = 0;
    return 0;
}

int view_polygon_available(USView *view) {
    if (!view || !view->mesh) return 0;
    return (view->mesh->n_elements > 0 && view->mesh->elem_nodes != NULL);
}

int view_set_render_mode(USView *view, RenderMode mode) {
    if (!view) return -1;
    
    if (mode == RENDER_MODE_POLYGON && !view_polygon_available(view)) {
        fprintf(stderr, "Polygon mode unavailable: no element connectivity loaded\n");
        return -1;
    }
    
    view->render_mode = mode;
    view->data_valid = 0;
    return 0;
}

int view_toggle_render_mode(USView *view) {
    if (!view) return -1;
    
    if (view->render_mode == RENDER_MODE_INTERPOLATE) {
        if (view_polygon_available(view)) {
            view->render_mode = RENDER_MODE_POLYGON;
            view->data_valid = 0;
            return (int)RENDER_MODE_POLYGON;
        }
        return -1;  /* Polygon mode not available */
    } else {
        view->render_mode = RENDER_MODE_INTERPOLATE;
        view->data_valid = 0;
        return (int)RENDER_MODE_INTERPOLATE;
    }
}

/* Helper: convert lon/lat to pixel coordinates */
static void lonlat_to_pixel(double lon, double lat, size_t width, size_t height,
                            int *px, int *py) {
    /* Simple equirectangular projection: lon [-180,180] -> [0,width], lat [-90,90] -> [height,0] */
    *px = (int)((lon + 180.0) / 360.0 * (double)width);
    *py = (int)((90.0 - lat) / 180.0 * (double)height);
}

/* Helper: clamp value to range */
static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Fill a triangle using scanline algorithm */
static void fill_triangle(unsigned char *pixels, size_t width, size_t height,
                          int x0, int y0, int x1, int y1, int x2, int y2,
                          unsigned char r, unsigned char g, unsigned char b) {
    /* Sort vertices by y coordinate */
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 > y2) { int t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    
    /* Skip degenerate triangles */
    if (y2 == y0) return;
    
    /* Clamp to image bounds */
    int y_start = clamp_int(y0, 0, (int)height - 1);
    int y_end = clamp_int(y2, 0, (int)height - 1);
    
    for (int y = y_start; y <= y_end; y++) {
        int x_left, x_right;
        
        /* Calculate x intersections for this scanline */
        if (y < y1) {
            /* Upper part of triangle */
            if (y1 != y0) {
                x_left = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
            } else {
                x_left = x0;
            }
        } else {
            /* Lower part of triangle */
            if (y2 != y1) {
                x_left = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
            } else {
                x_left = x1;
            }
        }
        
        if (y2 != y0) {
            x_right = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        } else {
            x_right = x0;
        }
        
        if (x_left > x_right) { int t = x_left; x_left = x_right; x_right = t; }
        
        x_left = clamp_int(x_left, 0, (int)width - 1);
        x_right = clamp_int(x_right, 0, (int)width - 1);
        
        /* Fill scanline */
        for (int x = x_left; x <= x_right; x++) {
            size_t idx = ((size_t)y * width + (size_t)x) * 3;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
        }
    }
}

/* Render polygons directly to pixel buffer */
static int view_render_polygons(USView *view) {
    if (!view || !view->mesh || !view->variable) return -1;
    
    USMesh *mesh = view->mesh;
    if (!mesh->elem_nodes || mesh->n_elements == 0) return -1;
    
    size_t width = view->display_nx;
    size_t height = view->display_ny;
    
    /* Clear to black */
    memset(view->pixels, 0, width * height * 3);
    
    /* Get colormap */
    USColormap *cmap = colormap_get_current();
    if (!cmap) return -1;
    
    float data_min = view->variable->user_min;
    float data_max = view->variable->user_max;
    float data_range = data_max - data_min;
    if (data_range <= 0.0f) data_range = 1.0f;
    
    /* For each element, compute average value and render triangle */
    for (size_t e = 0; e < mesh->n_elements; e++) {
        int *nodes = &mesh->elem_nodes[e * mesh->n_vertices];
        
        /* Get vertex coordinates */
        double lons[4], lats[4];
        float values[4];
        int valid = 1;
        float sum_val = 0.0f;
        int n_valid_vals = 0;
        
        for (int v = 0; v < mesh->n_vertices; v++) {
            int node_idx = nodes[v];
            if (node_idx < 0 || (size_t)node_idx >= mesh->n_points) {
                valid = 0;
                break;
            }
            lons[v] = mesh->lon[node_idx];
            lats[v] = mesh->lat[node_idx];
            values[v] = view->raw_data[node_idx];
            
            /* Check for fill value */
            if (values[v] != view->variable->fill_value && 
                fabsf(values[v]) < 1e30f) {
                sum_val += values[v];
                n_valid_vals++;
            }
        }
        
        if (!valid || n_valid_vals == 0) continue;
        
        /* Skip elements that span the dateline (lon difference > 180) */
        double max_lon_diff = 0.0;
        for (int v = 0; v < mesh->n_vertices; v++) {
            for (int w = v + 1; w < mesh->n_vertices; w++) {
                double diff = fabs(lons[v] - lons[w]);
                if (diff > max_lon_diff) max_lon_diff = diff;
            }
        }
        if (max_lon_diff > 180.0) continue;  /* Skip dateline-crossing elements */
        
        /* Compute average value and map to color */
        float avg_val = sum_val / (float)n_valid_vals;
        float t = (avg_val - data_min) / data_range;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        
        unsigned char r, g, b;
        colormap_map_value(cmap, t, &r, &g, &b);
        
        /* Convert vertices to pixel coordinates */
        int px[4], py[4];
        for (int v = 0; v < mesh->n_vertices; v++) {
            lonlat_to_pixel(lons[v], lats[v], width, height, &px[v], &py[v]);
        }
        
        /* Render triangle(s) */
        fill_triangle(view->pixels, width, height,
                      px[0], py[0], px[1], py[1], px[2], py[2], r, g, b);
        
        /* If quad (4 vertices), render second triangle */
        if (mesh->n_vertices == 4) {
            fill_triangle(view->pixels, width, height,
                          px[0], py[0], px[2], py[2], px[3], py[3], r, g, b);
        }
    }
    
    return 0;
}

int view_update(USView *view) {
    if (!view || !view->variable || !view->mesh) return -1;
    
    /* Polygon mode doesn't need regrid */
    if (view->render_mode != RENDER_MODE_POLYGON && !view->regrid) return -1;

    /* Read data slice - dispatch based on file type */
    int read_result;
#ifdef HAVE_ZARR
    if (view->fileset && view->fileset->files[0]->file_type == FILE_TYPE_ZARR) {
        /* Zarr multi-file */
        read_result = zarr_read_slice_fileset(view->fileset, view->variable,
                                              view->time_index, view->depth_index,
                                              view->raw_data);
    } else if (view->variable->file && view->variable->file->file_type == FILE_TYPE_ZARR) {
        /* Single zarr file */
        read_result = zarr_read_slice(view->variable, view->time_index,
                                      view->depth_index, view->raw_data);
    } else
#endif
    if (view->fileset) {
        read_result = netcdf_read_slice_fileset(view->fileset, view->variable,
                                                 view->time_index, view->depth_index,
                                                 view->raw_data);
    } else {
        read_result = netcdf_read_slice(view->variable, view->time_index,
                                        view->depth_index, view->raw_data);
    }

    if (read_result != 0) {
        fprintf(stderr, "Failed to read data slice\n");
        return -1;
    }

    /* Render based on mode */
    if (view->render_mode == RENDER_MODE_POLYGON) {
        /* Direct polygon rendering */
        if (view_render_polygons(view) != 0) {
            fprintf(stderr, "Polygon rendering failed, falling back to interpolate\n");
            view->render_mode = RENDER_MODE_INTERPOLATE;
            /* Fall through to interpolate mode */
        } else {
            view->data_valid = 1;
            return 0;
        }
    }
    
    /* Interpolate mode: regrid and colormap */
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

int view_save_ppm(USView *view, const char *filename) {
    if (!view || !view->pixels || !filename) return -1;

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open file for writing: %s\n", filename);
        return -1;
    }

    /* Write PPM header */
    fprintf(fp, "P6\n%zu %zu\n255\n", view->display_nx, view->display_ny);

    /* Write pixel data (already in RGB format) */
    size_t n_bytes = view->display_nx * view->display_ny * 3;
    if (fwrite(view->pixels, 1, n_bytes, fp) != n_bytes) {
        fprintf(stderr, "Failed to write pixel data\n");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}
