/*
 * regrid.c - KDTree-based regridding engine
 */

#include "regrid.h"
#include "projection.h"
#include "kdtree.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

USRegrid *regrid_create(USMesh *mesh, double resolution, double influence_radius_m,
                        const USTargetConfig *config) {
    if (!mesh || !mesh->xyz || mesh->n_points == 0) {
        fprintf(stderr, "Invalid mesh for regridding\n");
        return NULL;
    }

    /* Use global equirect defaults if no config provided */
    USTargetConfig default_config;
    if (!config) {
        target_config_init_default(&default_config);
        config = &default_config;
    }

    USRegrid *regrid = calloc(1, sizeof(USRegrid));
    if (!regrid) return NULL;

    /* Store parameters */
    regrid->influence_radius_meters = influence_radius_m;
    regrid->influence_radius_chord = meters_to_chord(influence_radius_m);
    regrid->source_n_points = mesh->n_points;
    regrid->projection = config->projection;
    regrid->laea_R = EARTH_RADIUS_M;

    /* Create target grid based on projection */
    if (config->projection == PROJ_EQUIRECTANGULAR) {
        regrid->target_lon_min = config->lon_min;
        regrid->target_lon_max = config->lon_max;
        regrid->target_lat_min = config->lat_min;
        regrid->target_lat_max = config->lat_max;

        regrid->target_nx = (size_t)((regrid->target_lon_max - regrid->target_lon_min) / resolution);
        regrid->target_ny = (size_t)((regrid->target_lat_max - regrid->target_lat_min) / resolution);

        regrid->target_dlon = (regrid->target_lon_max - regrid->target_lon_min) / regrid->target_nx;
        regrid->target_dlat = (regrid->target_lat_max - regrid->target_lat_min) / regrid->target_ny;
    } else {
        /* LAEA polar projection */
        int pole = (config->projection == PROJ_LAEA_NORTH) ? 1 : -1;
        double extent = laea_extent_from_cutoff(config->cutoff_lat, pole, regrid->laea_R);

        /* Convert resolution from degrees to meters (~111.32 km per degree) */
        double res_m = resolution * 111320.0;
        size_t n = (size_t)(2.0 * extent / res_m);
        if (n < 2) n = 2;

        regrid->target_nx = n;
        regrid->target_ny = n;

        /* Store projected bounds (meters) in lon/lat fields */
        regrid->target_lon_min = -extent;
        regrid->target_lon_max = extent;
        regrid->target_lat_min = -extent;
        regrid->target_lat_max = extent;

        regrid->target_dlon = 2.0 * extent / n;
        regrid->target_dlat = 2.0 * extent / n;

        printf("LAEA %s pole: cutoff=%.0f° extent=%.0fkm grid=%zux%zu\n",
               (pole > 0) ? "north" : "south",
               config->cutoff_lat, extent / 1000.0, n, n);
    }

    size_t n_target = regrid->target_nx * regrid->target_ny;

    printf("Creating regrid: %zu x %zu target grid (%zu points)\n",
           regrid->target_nx, regrid->target_ny, n_target);
    printf("Influence radius: %.0f m (chord: %.6f)\n",
           influence_radius_m, regrid->influence_radius_chord);

    /* Allocate interpolation arrays */
    regrid->nn_indices = malloc(n_target * sizeof(size_t));
    regrid->nn_distances = malloc(n_target * sizeof(double));
    regrid->valid_mask = calloc(n_target, sizeof(unsigned char));

    if (!regrid->nn_indices || !regrid->nn_distances || !regrid->valid_mask) {
        regrid_free(regrid);
        return NULL;
    }

    /* Build KDTree from source mesh Cartesian coordinates */
    printf("Building KDTree from %zu source points...\n", mesh->n_points);
    regrid->kdtree = kdtree_create(mesh->xyz, mesh->n_points);
    if (!regrid->kdtree) {
        fprintf(stderr, "Failed to create KDTree\n");
        regrid_free(regrid);
        return NULL;
    }

    /* Query nearest neighbors for each target point */
    printf("Computing nearest neighbors for %zu target points...\n", n_target);
    double query[3];
    size_t valid_count = 0;

    for (size_t j = 0; j < regrid->target_ny; j++) {
        for (size_t i = 0; i < regrid->target_nx; i++) {
            size_t target_idx = j * regrid->target_nx + i;
            double lon, lat;

            if (config->projection == PROJ_EQUIRECTANGULAR) {
                lon = regrid->target_lon_min + (i + 0.5) * regrid->target_dlon;
                lat = regrid->target_lat_min + (j + 0.5) * regrid->target_dlat;
            } else {
                /* LAEA: compute projected (x,y), inverse-project to lon/lat */
                int pole = (config->projection == PROJ_LAEA_NORTH) ? 1 : -1;
                double px = regrid->target_lon_min + (i + 0.5) * regrid->target_dlon;
                double py = regrid->target_lat_min + (j + 0.5) * regrid->target_dlat;

                if (laea_inverse(px, py, pole, regrid->laea_R, &lon, &lat) != 0) {
                    /* Outside projection disk */
                    regrid->nn_indices[target_idx] = 0;
                    regrid->nn_distances[target_idx] = 1e30;
                    regrid->valid_mask[target_idx] = 0;
                    continue;
                }
            }

            /* Convert target point to Cartesian */
            lonlat_to_cartesian(lon, lat, &query[0], &query[1], &query[2]);

            /* Find nearest neighbor */
            size_t nn_idx;
            double nn_dist;
            kdtree_query_nearest(regrid->kdtree, query, &nn_idx, &nn_dist);

            regrid->nn_indices[target_idx] = nn_idx;
            regrid->nn_distances[target_idx] = nn_dist;

            /* Check if within influence radius */
            if (nn_dist <= regrid->influence_radius_chord) {
                regrid->valid_mask[target_idx] = 1;
                valid_count++;
            }
        }

        /* Progress indicator */
        if ((j + 1) % 30 == 0 || j == regrid->target_ny - 1) {
            printf("  Progress: %zu/%zu rows (%.1f%%)\n",
                   j + 1, regrid->target_ny,
                   100.0 * (j + 1) / regrid->target_ny);
        }
    }

    printf("Regrid created: %zu/%zu valid target points (%.1f%%)\n",
           valid_count, n_target, 100.0 * valid_count / n_target);

    return regrid;
}

void regrid_apply(const USRegrid *regrid, const float *source_data,
                  float fill_value, float *target_data) {
    if (!regrid || !source_data || !target_data) return;

    size_t n_target = regrid->target_nx * regrid->target_ny;

    for (size_t i = 0; i < n_target; i++) {
        if (regrid->valid_mask[i]) {
            float value = source_data[regrid->nn_indices[i]];
            /* Check for source fill value (very large values) */
            if (fabsf(value) < INVALID_DATA_THRESHOLD) {
                target_data[i] = value;
            } else {
                target_data[i] = fill_value;
            }
        } else {
            target_data[i] = fill_value;
        }
    }
}

void regrid_get_target_dims(const USRegrid *regrid, size_t *nx, size_t *ny) {
    if (regrid) {
        if (nx) *nx = regrid->target_nx;
        if (ny) *ny = regrid->target_ny;
    } else {
        if (nx) *nx = 0;
        if (ny) *ny = 0;
    }
}

void regrid_get_lonlat(const USRegrid *regrid, size_t ix, size_t iy,
                       double *lon, double *lat) {
    if (!regrid) return;

    if (regrid->projection == PROJ_EQUIRECTANGULAR) {
        if (lon) *lon = regrid->target_lon_min + (ix + 0.5) * regrid->target_dlon;
        if (lat) *lat = regrid->target_lat_min + (iy + 0.5) * regrid->target_dlat;
    } else {
        /* LAEA: compute projected coords, inverse-project */
        int pole = (regrid->projection == PROJ_LAEA_NORTH) ? 1 : -1;
        double px = regrid->target_lon_min + (ix + 0.5) * regrid->target_dlon;
        double py = regrid->target_lat_min + (iy + 0.5) * regrid->target_dlat;
        double lo, la;
        if (laea_inverse(px, py, pole, regrid->laea_R, &lo, &la) == 0) {
            if (lon) *lon = lo;
            if (lat) *lat = la;
        } else {
            if (lon) *lon = 0.0;
            if (lat) *lat = 0.0;
        }
    }
}

void regrid_free(USRegrid *regrid) {
    if (!regrid) return;
    kdtree_free(regrid->kdtree);
    free(regrid->nn_indices);
    free(regrid->nn_distances);
    free(regrid->valid_mask);
    free(regrid);
}
