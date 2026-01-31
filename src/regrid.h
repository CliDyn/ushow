/*
 * regrid.h - KDTree-based regridding engine
 */

#ifndef REGRID_H
#define REGRID_H

#include "ushow.defines.h"
#include "mesh.h"

/*
 * Create regridding structure for a mesh.
 * Builds KDTree and precomputes nearest neighbor indices for target grid.
 *
 * mesh: source mesh with coordinates
 * resolution: target grid resolution in degrees (default 1.0)
 * influence_radius_m: maximum distance for valid interpolation in meters
 */
USRegrid *regrid_create(USMesh *mesh, double resolution, double influence_radius_m);

/*
 * Apply regridding to data.
 * source_data: input data [mesh->n_points]
 * fill_value: value to use for invalid/missing data
 * target_data: output data [target_ny * target_nx], must be preallocated
 */
void regrid_apply(const USRegrid *regrid, const float *source_data,
                  float fill_value, float *target_data);

/*
 * Get target grid dimensions.
 */
void regrid_get_target_dims(const USRegrid *regrid, size_t *nx, size_t *ny);

/*
 * Get target grid longitude/latitude at a pixel position.
 */
void regrid_get_lonlat(const USRegrid *regrid, size_t ix, size_t iy,
                       double *lon, double *lat);

/*
 * Free regridding structure and all associated memory.
 */
void regrid_free(USRegrid *regrid);

#endif /* REGRID_H */
