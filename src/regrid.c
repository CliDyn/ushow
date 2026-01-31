/*
 * regrid.c - KDTree-based regridding engine
 */

#include "regrid.h"
#include "kdtree.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

USRegrid *regrid_create(USMesh *mesh, double resolution, double influence_radius_m) {
    if (!mesh || !mesh->xyz || mesh->n_points == 0) {
        fprintf(stderr, "Invalid mesh for regridding\n");
        return NULL;
    }

    USRegrid *regrid = calloc(1, sizeof(USRegrid));
    if (!regrid) return NULL;

    /* Store parameters */
    regrid->influence_radius_meters = influence_radius_m;
    regrid->influence_radius_chord = meters_to_chord(influence_radius_m);
    regrid->source_n_points = mesh->n_points;

    /* Create target grid (global) */
    regrid->target_lon_min = -180.0;
    regrid->target_lon_max = 180.0;
    regrid->target_lat_min = -90.0;
    regrid->target_lat_max = 90.0;

    regrid->target_nx = (size_t)((regrid->target_lon_max - regrid->target_lon_min) / resolution);
    regrid->target_ny = (size_t)((regrid->target_lat_max - regrid->target_lat_min) / resolution);

    regrid->target_dlon = (regrid->target_lon_max - regrid->target_lon_min) / regrid->target_nx;
    regrid->target_dlat = (regrid->target_lat_max - regrid->target_lat_min) / regrid->target_ny;

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
        double lat = regrid->target_lat_min + (j + 0.5) * regrid->target_dlat;

        for (size_t i = 0; i < regrid->target_nx; i++) {
            double lon = regrid->target_lon_min + (i + 0.5) * regrid->target_dlon;
            size_t target_idx = j * regrid->target_nx + i;

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
            if (fabsf(value) < 1e10f) {
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
    if (lon) *lon = regrid->target_lon_min + (ix + 0.5) * regrid->target_dlon;
    if (lat) *lat = regrid->target_lat_min + (iy + 0.5) * regrid->target_dlat;
}

void regrid_free(USRegrid *regrid) {
    if (!regrid) return;
    kdtree_free(regrid->kdtree);
    free(regrid->nn_indices);
    free(regrid->nn_distances);
    free(regrid->valid_mask);
    free(regrid);
}
