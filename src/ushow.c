/*
 * ushow.c - Main entry point for ushow
 *
 * Unstructured data visualization tool
 */

#include "ushow.defines.h"
#include "mesh.h"
#include "regrid.h"
#include "file_netcdf.h"
#include "colormaps.h"
#include "view.h"
#include "interface/x_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/* Global state */
static USFile *file = NULL;
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

/* Callbacks */
static void on_var_select(int var_index) {
    /* Find variable by index */
    USVar *var = variables;
    for (int i = 0; i < var_index && var; i++) {
        var = var->next;
    }
    if (!var) return;

    current_var = var;
    view_set_variable(view, var, mesh, regrid);

    /* Update UI */
    x_update_var_name(var->name);
    x_update_range_label(var->user_min, var->user_max);
    x_update_time(view->time_index, view->n_times);
    x_update_depth(view->depth_index, view->n_depths);

    /* Set up dimension info panel */
    if (current_dim_info) {
        netcdf_free_dim_info(current_dim_info, n_current_dims);
    }
    current_dim_info = netcdf_get_dim_info(var, &n_current_dims);
    x_setup_dim_info(current_dim_info, n_current_dims);
    update_dim_info_current();

    update_display();
}

static void on_time_change(size_t time_idx) {
    if (!view || !current_var) return;
    view_set_time(view, time_idx);
    x_update_time(view->time_index, view->n_times);
    update_dim_info_current();
    update_display();
}

static void on_depth_change(size_t depth_idx) {
    if (!view || !current_var) return;
    view_set_depth(view, depth_idx);
    x_update_depth(view->depth_index, view->n_depths);
    update_dim_info_current();
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
            update_display();
        }
    } else if (direction == -2) {
        /* Rewind to start */
        animating = 0;
        x_clear_timer();
        if (view) {
            view_set_time(view, 0);
            x_update_time(view->time_index, view->n_times);
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
    fprintf(stderr, "Usage: %s [options] <data_file.nc>\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -m, --mesh <file>      Mesh file with coordinates\n");
    fprintf(stderr, "  -r, --resolution <deg> Target grid resolution (default: 1.0)\n");
    fprintf(stderr, "  -i, --influence <m>    Influence radius in meters (default: 200000)\n");
    fprintf(stderr, "  -d, --delay <ms>       Animation frame delay (default: 200)\n");
    fprintf(stderr, "  -h, --help             Show this help\n");
}

int main(int argc, char *argv[]) {
    const char *data_filename = NULL;
    const char *mesh_filename = NULL;

    /* Parse command line options */
    static struct option long_options[] = {
        {"mesh",       required_argument, 0, 'm'},
        {"resolution", required_argument, 0, 'r'},
        {"influence",  required_argument, 0, 'i'},
        {"delay",      required_argument, 0, 'd'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:r:i:d:h", long_options, NULL)) != -1) {
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
    data_filename = argv[optind];

    printf("=== ushow: Unstructured Data Viewer ===\n\n");
    printf("Data file: %s\n", data_filename);
    if (mesh_filename) printf("Mesh file: %s\n", mesh_filename);
    printf("Resolution: %.2f degrees\n", options.target_resolution);
    printf("Influence radius: %.0f m\n", options.influence_radius);
    printf("\n");

    /* Initialize colormaps */
    colormaps_init();

    /* Open data file */
    printf("Opening data file...\n");
    file = netcdf_open(data_filename);
    if (!file) {
        fprintf(stderr, "Failed to open data file: %s\n", data_filename);
        return 1;
    }

    /* Load mesh */
    printf("Loading mesh...\n");
    mesh = mesh_create_from_netcdf(file->ncid, mesh_filename);
    if (!mesh) {
        fprintf(stderr, "Failed to load mesh\n");
        netcdf_close(file);
        return 1;
    }

    /* Create regridding structure */
    printf("Creating regrid structure...\n");
    regrid = regrid_create(mesh, options.target_resolution, options.influence_radius);
    if (!regrid) {
        fprintf(stderr, "Failed to create regrid\n");
        mesh_free(mesh);
        netcdf_close(file);
        return 1;
    }

    /* Scan for variables */
    printf("Scanning for variables...\n");
    variables = netcdf_scan_variables(file, mesh);
    if (!variables) {
        fprintf(stderr, "No displayable variables found\n");
        regrid_free(regrid);
        mesh_free(mesh);
        netcdf_close(file);
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

    /* Initialize X11 */
    printf("Initializing display...\n");
    if (x_init(&argc, argv, var_names, n_variables) != 0) {
        fprintf(stderr, "Failed to initialize X11 display\n");
        free(var_names);
        regrid_free(regrid);
        mesh_free(mesh);
        netcdf_close(file);
        return 1;
    }
    free(var_names);

    /* Set up callbacks */
    x_set_var_callback(on_var_select);
    x_set_time_callback(on_time_change);
    x_set_depth_callback(on_depth_change);
    x_set_animation_callback(on_animation);
    x_set_colormap_callback(on_colormap_change);
    x_set_mouse_callback(on_mouse_motion);
    x_set_range_callback(on_range_adjust);
    x_set_zoom_callback(on_zoom);
    x_set_save_callback(on_save);
    x_set_dim_nav_callback(on_dim_nav);

    /* Create view */
    view = view_create();

    /* Select first variable */
    on_var_select(0);

    /* Update colormap label */
    USColormap *cmap = colormap_get_current();
    if (cmap) {
        x_update_colormap_label(cmap->name);
    }

    printf("\nReady. Use variable buttons to select data.\n");
    printf("Controls: < Back | || Pause | Fwd >\n");
    printf("Click 'Colormap' to cycle through colormaps.\n\n");

    /* Enter main loop */
    x_main_loop();

    /* Cleanup */
    x_cleanup();
    if (current_dim_info) {
        netcdf_free_dim_info(current_dim_info, n_current_dims);
    }
    view_free(view);
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    colormaps_cleanup();

    return 0;
}
