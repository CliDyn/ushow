/*
 * regrid_yac.c - YAC-based interpolation engine
 *
 * Uses YAC library for professional-grade interpolation:
 * NNN (distance/Gaussian weighted), averaging, and conservative remapping.
 * Uses yac_interpolation_execute() which handles data redistribution internally.
 */

#ifdef HAVE_YAC

#include "regrid_yac.h"
#include "projection.h"
#include "healpix.h"
#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

/* YAC public header (single mega-header installed by YAC) */
#include "yac_core.h"
/* YAXT must be initialized before YAC grid operations */
#include <xt/xt_core.h>

/* Grid name constants used when registering source/target grids with YAC */
#define YAC_SOURCE_GRID_NAME "source"
#define YAC_TARGET_GRID_NAME "target"

/* Store YAC interpolation object for execute-based apply */
struct USYacRegrid {
    size_t target_nx, target_ny;
    double target_dlon, target_dlat;
    double target_lon_min, target_lat_min;

    size_t n_src_points;   /* source mesh point count */
    size_t n_tgt_points;   /* target grid cell count */

    /* YAC interpolation object (handles redistribution + weight apply) */
    struct yac_interpolation *interp;

    /* YAC interpolation weights (retained for reuse / inspection) */
    struct yac_interp_weights *weights;

    /* Temporary double buffers for float<->double conversion */
    double *src_buf;   /* [n_src_points] */
    double *tgt_buf;   /* [n_tgt_points] */

    USYacMethod method;
    double resolution;  /* target grid resolution (for rebuilding) */

    /* Projection info */
    ProjectionType projection;
    double laea_R;
    USTargetConfig config;  /* stored copy for 3D rebuild path */

    /* Per-depth cache (--yac-3d) */
    USMesh *mesh_ref;                        /* non-owned, for lazy creation */
    struct yac_interpolation **depth_cache;  /* [depth_cache_n], NULL = not yet built */
    size_t depth_cache_n;                    /* 0 = 3d mode not enabled */
};

static int mpi_initialized_by_us = 0;
static int yaxt_initialized_by_us = 0;

static double get_time_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

int yac_regrid_init(void) {
    int already_initialized = 0;
    MPI_Initialized(&already_initialized);
    if (!already_initialized) {
        if (MPI_Init(NULL, NULL) != MPI_SUCCESS) {
            fprintf(stderr, "YAC: MPI_Init failed\n");
            return -1;
        }
        mpi_initialized_by_us = 1;
    }
    /* YAXT must be initialized before any YAC grid operations */
    if (!xt_initialized()) {
        xt_initialize(MPI_COMM_WORLD);
        yaxt_initialized_by_us = 1;
    }
    return 0;
}

void yac_regrid_finalize(void) {
    if (yaxt_initialized_by_us && xt_initialized()) {
        xt_finalize();
        yaxt_initialized_by_us = 0;
    }
    if (mpi_initialized_by_us) {
        int finalized = 0;
        MPI_Finalized(&finalized);
        if (!finalized) {
            MPI_Finalize();
        }
        mpi_initialized_by_us = 0;
    }
}

const char *yac_method_name(USYacMethod m) {
    switch (m) {
        case YAC_METHOD_NNN_1:           return "nnn1";
        case YAC_METHOD_NNN_4_DIST:      return "nnn4dist";
        case YAC_METHOD_NNN_4_GAUSS:     return "nnn4gauss";
        case YAC_METHOD_AVERAGE_ARITH:   return "avg_arith";
        case YAC_METHOD_AVERAGE_DIST:    return "avg_dist";
        case YAC_METHOD_AVERAGE_BARY:    return "avg_bary";
        case YAC_METHOD_CONSERVATIVE_1:  return "conserv1";
        case YAC_METHOD_CONSERVATIVE_2:  return "conserv2";
        default:                         return "unknown";
    }
}

int yac_method_parse(const char *name, USYacMethod *out) {
    if (!name || !out) return -1;
    struct { const char *str; USYacMethod m; } table[] = {
        {"nnn1",       YAC_METHOD_NNN_1},
        {"nnn4dist",   YAC_METHOD_NNN_4_DIST},
        {"nnn4gauss",  YAC_METHOD_NNN_4_GAUSS},
        {"avg_arith",  YAC_METHOD_AVERAGE_ARITH},
        {"avg_dist",   YAC_METHOD_AVERAGE_DIST},
        {"avg_bary",   YAC_METHOD_AVERAGE_BARY},
        {"conserv1",   YAC_METHOD_CONSERVATIVE_1},
        {"conserv2",   YAC_METHOD_CONSERVATIVE_2},
    };
    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (strcmp(name, table[i].str) == 0) {
            *out = table[i].m;
            return 0;
        }
    }
    return -1;
}

int yac_method_needs_connectivity(USYacMethod m) {
    switch (m) {
        case YAC_METHOD_AVERAGE_ARITH:
        case YAC_METHOD_AVERAGE_DIST:
        case YAC_METHOD_AVERAGE_BARY:
        case YAC_METHOD_CONSERVATIVE_1:
        case YAC_METHOD_CONSERVATIVE_2:
            return 1;
        default:
            return 0;
    }
}

/* Build YAC source grid from USMesh */
static struct yac_basic_grid *build_source_grid(USMesh *mesh, USYacMethod method) {
    /* For 2D curvilinear or 1D structured grids with known dimensions,
     * use YAC's native curve_2d support. This avoids the need for
     * auto-generated triangulation which fails on grids with folds
     * (e.g., ORCA north fold) or irregular latitude distributions. */
    if ((mesh->coord_type == COORD_TYPE_2D_CURVILINEAR ||
         mesh->coord_type == COORD_TYPE_1D_STRUCTURED) &&
        mesh->orig_nx >= 2 && mesh->orig_ny >= 2) {
        size_t nbr_vertices[2] = {mesh->orig_nx, mesh->orig_ny};
        int cyclic[2] = {0, 0};
        return yac_basic_grid_curve_2d_deg_new(
            YAC_SOURCE_GRID_NAME, nbr_vertices, cyclic, mesh->lon, mesh->lat);
    }

    /* If connectivity information is available */
    if (mesh->n_elements > 0 && mesh->elem_nodes != NULL) {
        /* Unstructured grid with cell connectivity (e.g. FESOM triangles) */
        int *num_verts_per_cell = malloc(mesh->n_elements * sizeof(int));
        if (!num_verts_per_cell) return NULL;
        for (size_t i = 0; i < mesh->n_elements; i++) {
            num_verts_per_cell[i] = mesh->n_vertices;
        }

        struct yac_basic_grid *grid = yac_basic_grid_unstruct_deg_new(
            YAC_SOURCE_GRID_NAME,
            mesh->n_points,
            mesh->n_elements,
            num_verts_per_cell,
            mesh->lon,
            mesh->lat,
            mesh->elem_nodes);

        free(num_verts_per_cell);
        return grid;
    } else {
        /* Point cloud (e.g. HEALPix, or unstructured without connectivity) */
        return yac_basic_grid_cloud_deg_new(
            YAC_SOURCE_GRID_NAME,
            mesh->n_points,
            mesh->lon,
            mesh->lat);
    }
}

/* Build target grid (equirectangular or LAEA polar) */
static struct yac_basic_grid *build_target_grid(
    double resolution, const USTargetConfig *config,
    size_t *out_nx, size_t *out_ny) {

    /* Default to global equirectangular if no config */
    USTargetConfig default_cfg;
    if (!config) {
        target_config_init_default(&default_cfg);
        config = &default_cfg;
    }

    if (config->projection == PROJ_EQUIRECTANGULAR) {
        /* Equirectangular (global or regional --box) */
        double lon_min = config->lon_min, lon_max = config->lon_max;
        double lat_min = config->lat_min, lat_max = config->lat_max;

        size_t nx = (size_t)((lon_max - lon_min) / resolution);
        size_t ny = (size_t)((lat_max - lat_min) / resolution);
        if (nx < 1) nx = 1;
        if (ny < 1) ny = 1;

        size_t n_lon_verts = nx + 1;
        size_t n_lat_verts = ny + 1;

        double *lon_verts = malloc(n_lon_verts * sizeof(double));
        double *lat_verts = malloc(n_lat_verts * sizeof(double));
        if (!lon_verts || !lat_verts) {
            free(lon_verts);
            free(lat_verts);
            return NULL;
        }

        double dlon = (lon_max - lon_min) / (double)nx;
        double dlat = (lat_max - lat_min) / (double)ny;

        for (size_t i = 0; i <= nx; i++)
            lon_verts[i] = lon_min + i * dlon;
        for (size_t j = 0; j <= ny; j++)
            lat_verts[j] = lat_min + j * dlat;

        size_t nbr_vertices[2] = {n_lon_verts, n_lat_verts};
        int cyclic[2] = {0, 0};

        struct yac_basic_grid *grid = yac_basic_grid_reg_2d_deg_new(
            YAC_TARGET_GRID_NAME, nbr_vertices, cyclic, lon_verts, lat_verts);

        free(lon_verts);
        free(lat_verts);

        /* Add cell center coordinates (required for YAC_LOC_CELL interpolation) */
        size_t n_cells = nx * ny;
        yac_coordinate_pointer cell_coords = malloc(n_cells * sizeof(*cell_coords));
        if (cell_coords) {
            for (size_t j = 0; j < ny; j++) {
                double lat_center = lat_min + (j + 0.5) * dlat;
                for (size_t i = 0; i < nx; i++) {
                    double lon_center = lon_min + (i + 0.5) * dlon;
                    lonlat_to_cartesian(lon_center, lat_center,
                                       &cell_coords[j * nx + i][0],
                                       &cell_coords[j * nx + i][1],
                                       &cell_coords[j * nx + i][2]);
                }
            }
            yac_basic_grid_add_coordinates_nocpy(grid, YAC_LOC_CELL, cell_coords);
        }

        *out_nx = nx;
        *out_ny = ny;
        return grid;
    }

    /* LAEA polar projection */
    double R = EARTH_RADIUS_M;
    int pole = (config->projection == PROJ_LAEA_NORTH) ? 1 : -1;
    double extent = laea_extent_from_cutoff(config->cutoff_lat, pole, R);

    /* Convert resolution from degrees to meters (~111.32 km per degree) */
    double res_m = resolution * 111320.0;
    size_t n = (size_t)(2.0 * extent / res_m);
    if (n < 2) n = 2;

    double dx = 2.0 * extent / (double)n;
    double dy = dx;

    /* Build curvilinear grid: (n+1) x (n+1) vertices in lon/lat */
    size_t nvx = n + 1;
    size_t nvy = n + 1;
    size_t n_verts = nvx * nvy;
    double *vert_lon = malloc(n_verts * sizeof(double));
    double *vert_lat = malloc(n_verts * sizeof(double));
    if (!vert_lon || !vert_lat) {
        free(vert_lon);
        free(vert_lat);
        return NULL;
    }

    for (size_t j = 0; j < nvy; j++) {
        double py = -extent + j * dy;
        for (size_t i = 0; i < nvx; i++) {
            double px = -extent + i * dx;
            size_t idx = j * nvx + i;
            double lo, la;
            if (laea_inverse(px, py, pole, R, &lo, &la) == 0) {
                vert_lon[idx] = lo;
                vert_lat[idx] = la;
            } else {
                /* Outside projection disk — place at pole with offset */
                vert_lon[idx] = 0.0;
                vert_lat[idx] = (pole > 0) ? 90.0 : -90.0;
            }
        }
    }

    size_t nbr_vertices[2] = {nvx, nvy};
    int cyclic[2] = {0, 0};
    struct yac_basic_grid *grid = yac_basic_grid_curve_2d_deg_new(
        YAC_TARGET_GRID_NAME, nbr_vertices, cyclic, vert_lon, vert_lat);

    free(vert_lon);
    free(vert_lat);

    /* Add cell center coordinates */
    size_t n_cells = n * n;
    yac_coordinate_pointer cell_coords = malloc(n_cells * sizeof(*cell_coords));
    if (cell_coords) {
        for (size_t j = 0; j < n; j++) {
            double py = -extent + (j + 0.5) * dy;
            for (size_t i = 0; i < n; i++) {
                double px = -extent + (i + 0.5) * dx;
                double lo, la;
                if (laea_inverse(px, py, pole, R, &lo, &la) == 0) {
                    lonlat_to_cartesian(lo, la,
                                       &cell_coords[j * n + i][0],
                                       &cell_coords[j * n + i][1],
                                       &cell_coords[j * n + i][2]);
                } else {
                    double pole_lat = (pole > 0) ? 90.0 : -90.0;
                    lonlat_to_cartesian(0.0, pole_lat,
                                       &cell_coords[j * n + i][0],
                                       &cell_coords[j * n + i][1],
                                       &cell_coords[j * n + i][2]);
                }
            }
        }
        yac_basic_grid_add_coordinates_nocpy(grid, YAC_LOC_CELL, cell_coords);
    }

    printf("YAC LAEA %s pole: cutoff=%.0f° extent=%.0fkm grid=%zux%zu\n",
           (pole > 0) ? "north" : "south",
           config->cutoff_lat, extent / 1000.0, n, n);

    *out_nx = n;
    *out_ny = n;
    return grid;
}

/* Configure interpolation stack based on method */
static struct yac_interp_stack_config *build_interp_stack(USYacMethod method) {
    struct yac_interp_stack_config *config = yac_interp_stack_config_new();
    if (!config) return NULL;

    switch (method) {
        case YAC_METHOD_NNN_1:
            yac_interp_stack_config_add_nnn(
                config, YAC_INTERP_NNN_DIST, 1, 0.0, 0.0);
            break;
        case YAC_METHOD_NNN_4_DIST:
            yac_interp_stack_config_add_nnn(
                config, YAC_INTERP_NNN_DIST, 4, 0.0, 0.0);
            break;
        case YAC_METHOD_NNN_4_GAUSS:
            yac_interp_stack_config_add_nnn(
                config, YAC_INTERP_NNN_GAUSS, 4, 0.0,
                YAC_INTERP_NNN_GAUSS_SCALE_DEFAULT);
            break;
        case YAC_METHOD_AVERAGE_ARITH:
            yac_interp_stack_config_add_average(
                config, YAC_INTERP_AVG_ARITHMETIC, 1);
            break;
        case YAC_METHOD_AVERAGE_DIST:
            yac_interp_stack_config_add_average(
                config, YAC_INTERP_AVG_DIST, 1);
            break;
        case YAC_METHOD_AVERAGE_BARY:
            yac_interp_stack_config_add_average(
                config, YAC_INTERP_AVG_BARY, 1);
            break;
        case YAC_METHOD_CONSERVATIVE_1:
            yac_interp_stack_config_add_conservative(
                config, 1, 0, 1, YAC_INTERP_CONSERV_DESTAREA);
            break;
        case YAC_METHOD_CONSERVATIVE_2:
            yac_interp_stack_config_add_conservative(
                config, 2, 0, 1, YAC_INTERP_CONSERV_DESTAREA);
            break;
        default:
            yac_interp_stack_config_delete(config);
            return NULL;
    }

    return config;
}

/* Determine source field location */
static enum yac_location get_src_location(USYacMethod method) {
    (void)method;
    /* FESOM data lives on vertices/nodes (corners) */
    return YAC_LOC_CORNER;
}

/* Determine target field location */
static enum yac_location get_tgt_location(USYacMethod method) {
    (void)method;
    /* Target buffer is nx*ny cells */
    return YAC_LOC_CELL;
}

/*
 * Build a single YAC interpolation object.
 * If src_mask is non-NULL, applies it to the source grid corners.
 * Returns NULL on failure.
 */
static struct yac_interpolation *
build_interpolation(USMesh *mesh, USYacMethod method,
                    double resolution,
                    const int *src_mask,
                    const USTargetConfig *config) {
    /* 1. Build grids */
    struct yac_basic_grid *src_grid = build_source_grid(mesh, method);
    if (!src_grid) return NULL;

    /* Apply source mask if provided */
    size_t mask_idx = SIZE_MAX;
    if (src_mask) {
        mask_idx = yac_basic_grid_add_mask(
            src_grid, YAC_LOC_CORNER, src_mask, mesh->n_points, "depth_mask");
    }

    size_t nx, ny;
    struct yac_basic_grid *tgt_grid = build_target_grid(resolution, config, &nx, &ny);
    if (!tgt_grid) {
        yac_basic_grid_delete(src_grid);
        return NULL;
    }

    /* 2. Create distributed grid pair */
    struct yac_dist_grid_pair *grid_pair =
        yac_dist_grid_pair_new(src_grid, tgt_grid, MPI_COMM_SELF);
    if (!grid_pair) {
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 3. Set up interpolation fields */
    struct yac_interp_field src_field = {
        .location = get_src_location(method),
        .coordinates_idx = SIZE_MAX,
        .masks_idx = mask_idx
    };
    struct yac_interp_field tgt_field = {
        .location = get_tgt_location(method),
        .coordinates_idx = 0,
        .masks_idx = SIZE_MAX
    };

    struct yac_interp_grid *interp_grid = yac_interp_grid_new(
        grid_pair, YAC_SOURCE_GRID_NAME, YAC_TARGET_GRID_NAME, 1,
        &src_field, tgt_field);
    if (!interp_grid) {
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 4. Configure interpolation method */
    struct yac_interp_stack_config *stack_config = build_interp_stack(method);
    if (!stack_config) {
        yac_interp_grid_delete(interp_grid);
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    struct interp_method **methods = yac_interp_stack_config_generate(stack_config);
    yac_interp_stack_config_delete(stack_config);
    if (!methods) {
        yac_interp_grid_delete(interp_grid);
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 5. Compute weights */
    struct yac_interp_weights *weights =
        yac_interp_method_do_search(methods, interp_grid);
    yac_interp_method_delete(methods);
    if (!weights) {
        yac_interp_grid_delete(interp_grid);
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 6. Create interpolation object */
    struct yac_interpolation *interp = yac_interp_weights_get_interpolation(
        weights,
        YAC_MAPPING_ON_TGT, 1, YAC_FRAC_MASK_NO_VALUE,
        1.0, 0.0, NULL, 1, 1);

    /* 7. Cleanup intermediates */
    yac_interp_weights_delete(weights);
    yac_interp_grid_delete(interp_grid);
    yac_dist_grid_pair_delete(grid_pair);
    yac_basic_grid_delete(src_grid);
    yac_basic_grid_delete(tgt_grid);

    return interp;
}

USYacRegrid *yac_regrid_create(USMesh *mesh, double resolution, USYacMethod method,
                               const USTargetConfig *config) {
    if (!mesh || mesh->n_points == 0) {
        fprintf(stderr, "YAC regrid: invalid mesh\n");
        return NULL;
    }

    if (yac_method_needs_connectivity(method) &&
        (mesh->n_elements == 0 || mesh->elem_nodes == NULL)) {
        /* For curvilinear/structured grids, YAC handles connectivity natively
         * via curve_2d grid type — skip auto-generated triangulation */
        if ((mesh->coord_type == COORD_TYPE_2D_CURVILINEAR ||
             mesh->coord_type == COORD_TYPE_1D_STRUCTURED) &&
            mesh->orig_nx >= 2 && mesh->orig_ny >= 2) {
            printf("YAC: Using native curvilinear grid support (%zu x %zu)\n",
                   mesh->orig_nx, mesh->orig_ny);
        } else {
            /* Try to auto-generate connectivity from latitude bands */
            printf("YAC: Auto-generating connectivity for method '%s'...\n",
                   yac_method_name(method));
            if (latband_generate_connectivity(mesh) != 0) {
                fprintf(stderr, "YAC regrid: method '%s' requires element connectivity "
                        "and auto-generation failed\n", yac_method_name(method));
                return NULL;
            }
        }
    }

    /* Conservative methods require cell-based source data (YAC_LOC_CELL).
     * Node-based data (e.g. FESOM temperature on vertices) uses YAC_LOC_CORNER,
     * which is incompatible. Conservative would need Voronoi dual-mesh cells. */
    if (method == YAC_METHOD_CONSERVATIVE_1 || method == YAC_METHOD_CONSERVATIVE_2) {
        fprintf(stderr, "YAC regrid: conservative methods not supported for node-based data.\n"
                "  Conservative remapping requires cell-centered source fields.\n"
                "  Use nnn1, nnn4dist, nnn4gauss, avg_arith, avg_dist, or avg_bary instead.\n");
        return NULL;
    }

    double t0 = get_time_seconds();

    printf("YAC: Creating regrid with method '%s'...\n", yac_method_name(method));

    /* 1. Build grids */
    struct yac_basic_grid *src_grid = build_source_grid(mesh, method);
    if (!src_grid) {
        fprintf(stderr, "YAC: Failed to create source grid\n");
        return NULL;
    }

    /* Use global equirect defaults if no config provided */
    USTargetConfig default_cfg;
    if (!config) {
        target_config_init_default(&default_cfg);
        config = &default_cfg;
    }

    size_t nx, ny;
    struct yac_basic_grid *tgt_grid = build_target_grid(resolution, config, &nx, &ny);
    if (!tgt_grid) {
        fprintf(stderr, "YAC: Failed to create target grid\n");
        yac_basic_grid_delete(src_grid);
        return NULL;
    }

    printf("YAC: Source grid: %zu points", mesh->n_points);
    if (mesh->n_elements > 0)
        printf(", %zu elements", mesh->n_elements);
    else if ((mesh->coord_type == COORD_TYPE_2D_CURVILINEAR ||
              mesh->coord_type == COORD_TYPE_1D_STRUCTURED) &&
             mesh->orig_nx >= 2 && mesh->orig_ny >= 2)
        printf(" (%zu x %zu curvilinear)", mesh->orig_nx, mesh->orig_ny);
    printf("\n");
    printf("YAC: Target grid: %zu x %zu (%zu cells)\n", nx, ny, nx * ny);

    /* 2. Create distributed grid pair with MPI_COMM_SELF */
    struct yac_dist_grid_pair *grid_pair =
        yac_dist_grid_pair_new(src_grid, tgt_grid, MPI_COMM_SELF);
    if (!grid_pair) {
        fprintf(stderr, "YAC: Failed to create grid pair\n");
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 3. Set up interpolation fields */
    enum yac_location src_loc = get_src_location(method);
    enum yac_location tgt_loc = get_tgt_location(method);

    struct yac_interp_field src_field = {
        .location = src_loc,
        .coordinates_idx = SIZE_MAX,
        .masks_idx = SIZE_MAX
    };
    struct yac_interp_field tgt_field = {
        .location = tgt_loc,
        .coordinates_idx = 0,  /* cell center coords added in build_target_grid */
        .masks_idx = SIZE_MAX
    };

    struct yac_interp_grid *interp_grid = yac_interp_grid_new(
        grid_pair, YAC_SOURCE_GRID_NAME, YAC_TARGET_GRID_NAME, 1,
        &src_field, tgt_field);
    if (!interp_grid) {
        fprintf(stderr, "YAC: Failed to create interp grid\n");
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 4. Configure interpolation method */
    struct yac_interp_stack_config *stack_config = build_interp_stack(method);
    if (!stack_config) {
        fprintf(stderr, "YAC: Failed to build interp stack\n");
        yac_interp_grid_delete(interp_grid);
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    struct interp_method **methods = yac_interp_stack_config_generate(stack_config);
    yac_interp_stack_config_delete(stack_config);

    if (!methods) {
        fprintf(stderr, "YAC: Failed to generate interp methods\n");
        yac_interp_grid_delete(interp_grid);
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 5. Compute weights */
    printf("YAC: Computing interpolation weights...\n");
    struct yac_interp_weights *weights =
        yac_interp_method_do_search(methods, interp_grid);

    yac_interp_method_delete(methods);

    if (!weights) {
        fprintf(stderr, "YAC: Weight computation failed\n");
        yac_interp_grid_delete(interp_grid);
        yac_dist_grid_pair_delete(grid_pair);
        yac_basic_grid_delete(src_grid);
        yac_basic_grid_delete(tgt_grid);
        return NULL;
    }

    /* 6. Create interpolation object (handles data redistribution + weight apply) */
    struct yac_interpolation *interp = yac_interp_weights_get_interpolation(
        weights,
        YAC_MAPPING_ON_TGT,       /* apply weights on target side */
        1,                         /* collection_size */
        YAC_FRAC_MASK_NO_VALUE,   /* disable fractional masking */
        1.0,                       /* scaling_factor */
        0.0,                       /* scaling_summand */
        NULL,                      /* yaxt_exchanger_name */
        1,                         /* is_source */
        1                          /* is_target */
    );

    /* 7. Cleanup intermediates (weights kept in struct for reuse) */
    yac_interp_grid_delete(interp_grid);
    yac_dist_grid_pair_delete(grid_pair);
    yac_basic_grid_delete(src_grid);
    yac_basic_grid_delete(tgt_grid);

    if (!interp) {
        fprintf(stderr, "YAC: Failed to create interpolation object\n");
        return NULL;
    }

    /* 8. Allocate result struct */
    USYacRegrid *r = calloc(1, sizeof(USYacRegrid));
    if (!r) {
        yac_interpolation_delete(interp);
        return NULL;
    }

    r->target_nx = nx;
    r->target_ny = ny;
    r->method = method;
    r->resolution = resolution;
    r->n_src_points = mesh->n_points;
    r->n_tgt_points = nx * ny;
    r->interp = interp;
    r->weights = weights;
    r->projection = config->projection;
    r->laea_R = EARTH_RADIUS_M;
    r->config = *config;

    if (config->projection == PROJ_EQUIRECTANGULAR) {
        r->target_lon_min = config->lon_min;
        r->target_lat_min = config->lat_min;
        r->target_dlon = (config->lon_max - config->lon_min) / (double)nx;
        r->target_dlat = (config->lat_max - config->lat_min) / (double)ny;
    } else {
        /* LAEA: store projected extent/spacing in lon/lat fields (meters) */
        int pole = (config->projection == PROJ_LAEA_NORTH) ? 1 : -1;
        double extent = laea_extent_from_cutoff(config->cutoff_lat, pole, r->laea_R);
        r->target_lon_min = -extent;
        r->target_lat_min = -extent;
        r->target_dlon = 2.0 * extent / (double)nx;
        r->target_dlat = 2.0 * extent / (double)ny;
    }

    /* Pre-allocate double buffers for float<->double conversion */
    r->src_buf = malloc(mesh->n_points * sizeof(double));
    r->tgt_buf = malloc(r->n_tgt_points * sizeof(double));
    if (!r->src_buf || !r->tgt_buf) {
        fprintf(stderr, "YAC: Failed to allocate conversion buffers\n");
        yac_regrid_free(r);
        return NULL;
    }

    double t1 = get_time_seconds();
    printf("YAC: Interpolation ready in %.2fs\n", t1 - t0);

    return r;
}

void yac_regrid_apply(const USYacRegrid *r, const float *src, float fill, float *dst) {
    if (!r || !src || !dst || !r->interp) return;

    size_t n_src = r->n_src_points;
    size_t n_tgt = r->n_tgt_points;

    /* Convert float source data to double (YAC uses double) */
    double *src_d = r->src_buf;
    for (size_t i = 0; i < n_src; i++) {
        src_d[i] = (double)src[i];
    }

    /* Initialize target to fill */
    double *tgt_d = r->tgt_buf;
    for (size_t i = 0; i < n_tgt; i++) {
        tgt_d[i] = (double)fill;
    }

    /* Execute YAC interpolation:
     * src_fields[collection_idx][field_idx][local_idx]
     * tgt_field[collection_idx][local_idx]
     */
    double *src_field_ptr = src_d;
    double **src_fields_ptr = &src_field_ptr;  /* 1 field */
    double ***src_collection = &src_fields_ptr; /* 1 collection */
    double **tgt_collection = &tgt_d;           /* 1 collection */

    yac_interpolation_execute(r->interp, src_collection, tgt_collection);

    /* Convert double target data back to float */
    for (size_t i = 0; i < n_tgt; i++) {
        dst[i] = (float)tgt_d[i];
    }
}

void yac_regrid_get_target_dims(const USYacRegrid *r, size_t *nx, size_t *ny) {
    if (r) {
        if (nx) *nx = r->target_nx;
        if (ny) *ny = r->target_ny;
    } else {
        if (nx) *nx = 0;
        if (ny) *ny = 0;
    }
}

void yac_regrid_get_lonlat(const USYacRegrid *r, size_t ix, size_t iy,
                            double *lon, double *lat) {
    if (!r) return;

    if (r->projection == PROJ_EQUIRECTANGULAR) {
        if (lon) *lon = r->target_lon_min + (ix + 0.5) * r->target_dlon;
        if (lat) *lat = r->target_lat_min + (iy + 0.5) * r->target_dlat;
    } else {
        /* LAEA: compute projected coords, inverse-project */
        int pole = (r->projection == PROJ_LAEA_NORTH) ? 1 : -1;
        double px = r->target_lon_min + (ix + 0.5) * r->target_dlon;
        double py = r->target_lat_min + (iy + 0.5) * r->target_dlat;
        double lo, la;
        if (laea_inverse(px, py, pole, r->laea_R, &lo, &la) == 0) {
            if (lon) *lon = lo;
            if (lat) *lat = la;
        } else {
            if (lon) *lon = 0.0;
            if (lat) *lat = 0.0;
        }
    }
}

USYacMethod yac_regrid_get_method(const USYacRegrid *r) {
    return r ? r->method : YAC_METHOD_NNN_1;
}

void yac_regrid_enable_3d(USYacRegrid *r, USMesh *mesh) {
    if (r) r->mesh_ref = mesh;
}

void yac_regrid_clear_depth_cache(USYacRegrid *r) {
    if (!r || !r->depth_cache) return;
    for (size_t i = 0; i < r->depth_cache_n; i++) {
        if (r->depth_cache[i] && r->depth_cache[i] != r->interp) {
            yac_interpolation_delete(r->depth_cache[i]);
        }
    }
    free(r->depth_cache);
    r->depth_cache = NULL;
    r->depth_cache_n = 0;
}

int yac_regrid_is_3d(const USYacRegrid *r) {
    return r && r->mesh_ref != NULL;
}

void yac_regrid_apply_3d(USYacRegrid *r, size_t depth_idx, size_t n_depths,
                          const float *src, float fill, float *dst) {
    if (!r || !src || !dst) return;

    /* Lazy-allocate depth cache array */
    if (!r->depth_cache) {
        r->depth_cache = calloc(n_depths, sizeof(struct yac_interpolation *));
        if (!r->depth_cache) return;
        r->depth_cache_n = n_depths;
    }

    /* Build masked interpolation for this depth if not cached */
    if (!r->depth_cache[depth_idx]) {
        /* Scan source data for fill values to build mask */
        size_t n = r->n_src_points;
        int all_valid = 1;
        int *mask = malloc(n * sizeof(int));
        if (!mask) {
            /* Fallback to unmasked */
            yac_regrid_apply(r, src, fill, dst);
            return;
        }
        for (size_t i = 0; i < n; i++) {
            mask[i] = (src[i] != fill) ? 1 : 0;
            if (!mask[i]) all_valid = 0;
        }

        if (all_valid) {
            /* No masking needed — reuse base interpolation (sentinel) */
            r->depth_cache[depth_idx] = r->interp;
            free(mask);
        } else {
            double t0 = get_time_seconds();
            struct yac_interpolation *masked =
                build_interpolation(r->mesh_ref, r->method,
                                    r->resolution, mask, &r->config);
            free(mask);

            if (masked) {
                r->depth_cache[depth_idx] = masked;
            } else {
                /* Fallback to unmasked */
                r->depth_cache[depth_idx] = r->interp;
            }
            double t1 = get_time_seconds();
            printf("YAC 3D: Built masked interpolation for depth %zu (%.2fs)\n",
                   depth_idx, t1 - t0);
        }
    }

    /* Execute interpolation using the cached object */
    struct yac_interpolation *interp = r->depth_cache[depth_idx];
    size_t n_src = r->n_src_points;
    size_t n_tgt = r->n_tgt_points;

    double *src_d = r->src_buf;
    for (size_t i = 0; i < n_src; i++)
        src_d[i] = (double)src[i];

    double *tgt_d = r->tgt_buf;
    for (size_t i = 0; i < n_tgt; i++)
        tgt_d[i] = (double)fill;

    double *src_field_ptr = src_d;
    double **src_fields_ptr = &src_field_ptr;
    double ***src_collection = &src_fields_ptr;
    double **tgt_collection = &tgt_d;

    yac_interpolation_execute(interp, src_collection, tgt_collection);

    for (size_t i = 0; i < n_tgt; i++)
        dst[i] = (float)tgt_d[i];
}

int yac_regrid_is_regional(const USYacRegrid *r) {
    if (!r) return 0;
    if (r->projection != PROJ_EQUIRECTANGULAR) return 1;
    if (r->config.lon_min > -180.0 || r->config.lon_max < 180.0 ||
        r->config.lat_min > -90.0  || r->config.lat_max < 90.0)
        return 1;
    return 0;
}

void yac_regrid_free(USYacRegrid *r) {
    if (!r) return;
    yac_regrid_clear_depth_cache(r);
    if (r->interp) yac_interpolation_delete(r->interp);
    if (r->weights) yac_interp_weights_delete(r->weights);
    free(r->src_buf);
    free(r->tgt_buf);
    free(r);
}

#endif /* HAVE_YAC */
