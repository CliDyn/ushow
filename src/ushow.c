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

    update_display();
}

static void on_time_change(size_t time_idx) {
    if (!view || !current_var) return;
    view_set_time(view, time_idx);
    x_update_time(view->time_index, view->n_times);
    update_display();
}

static void on_depth_change(size_t depth_idx) {
    if (!view || !current_var) return;
    view_set_depth(view, depth_idx);
    x_update_depth(view->depth_index, view->n_depths);
    update_display();
}

static void on_animation(int direction) {
    if (direction == 0) {
        /* Pause */
        animating = 0;
        x_clear_timer();
    } else if (direction == 1) {
        /* Forward */
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

    /* Initialize X11 */
    printf("Initializing display...\n");
    if (x_init(&argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize X11 display\n");
        regrid_free(regrid);
        mesh_free(mesh);
        netcdf_close(file);
        return 1;
    }

    /* Set up callbacks */
    x_set_var_callback(on_var_select);
    x_set_time_callback(on_time_change);
    x_set_depth_callback(on_depth_change);
    x_set_animation_callback(on_animation);
    x_set_colormap_callback(on_colormap_change);

    /* Set up variable selector */
    const char **var_names = malloc(n_variables * sizeof(char *));
    v = variables;
    for (int i = 0; i < n_variables; i++) {
        var_names[i] = v->name;
        v = v->next;
    }
    x_setup_var_selector(var_names, n_variables);
    free(var_names);

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
    view_free(view);
    regrid_free(regrid);
    mesh_free(mesh);
    netcdf_close(file);
    colormaps_cleanup();

    return 0;
}
