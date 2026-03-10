/*
 * regrid_yac.h - YAC-based interpolation engine
 *
 * Professional-grade interpolation using YAC (Yet Another Coupler) library.
 * Supports NNN (distance/Gaussian weighted), averaging, and conservative remapping.
 * Optional: build with make WITH_YAC=1
 */

#ifndef REGRID_YAC_H
#define REGRID_YAC_H

#ifdef HAVE_YAC

#include "us_types.h"
#include "mesh.h"

typedef enum {
    YAC_METHOD_NNN_1 = 0,        /* Single nearest neighbor */
    YAC_METHOD_NNN_4_DIST,       /* 4-NN distance-weighted */
    YAC_METHOD_NNN_4_GAUSS,      /* 4-NN Gaussian-weighted */
    YAC_METHOD_AVERAGE_ARITH,    /* Cell averaging, arithmetic */
    YAC_METHOD_AVERAGE_DIST,     /* Cell averaging, distance-weighted */
    YAC_METHOD_AVERAGE_BARY,     /* Cell averaging, barycentric */
    YAC_METHOD_CONSERVATIVE_1,   /* 1st order conservative */
    YAC_METHOD_CONSERVATIVE_2,   /* 2nd order conservative */
    YAC_METHOD_COUNT
} USYacMethod;

typedef struct USYacRegrid USYacRegrid;

/*
 * Initialize YAC library (calls MPI_Init if needed).
 * Must be called before any other yac_regrid_* function.
 * Returns 0 on success, -1 on failure.
 */
int yac_regrid_init(void);

/*
 * Finalize YAC library.
 * Call after all yac_regrid objects are freed.
 */
void yac_regrid_finalize(void);

/*
 * Create YAC regridding structure.
 * Builds source/target grids, computes interpolation weights.
 *
 * mesh: source mesh with coordinates (and optionally element connectivity)
 * resolution: target grid resolution in degrees
 * method: interpolation method to use
 *
 * Returns NULL on failure.
 */
USYacRegrid *yac_regrid_create(USMesh *mesh, double resolution, USYacMethod method,
                               const USTargetConfig *config);

/*
 * Apply regridding to data using precomputed weights (sparse matrix-vector multiply).
 *
 * src: input data [mesh->n_points]
 * fill: fill value for missing/invalid data
 * dst: output data [target_ny * target_nx], must be preallocated
 */
void yac_regrid_apply(USYacRegrid *r, const float *src, float fill, float *dst);

/*
 * Get target grid dimensions.
 */
void yac_regrid_get_target_dims(const USYacRegrid *r, size_t *nx, size_t *ny);

/*
 * Get lon/lat at a target grid pixel position.
 */
void yac_regrid_get_lonlat(const USYacRegrid *r, size_t ix, size_t iy,
                            double *lon, double *lat);

/*
 * Get human-readable name for a method.
 */
const char *yac_method_name(USYacMethod m);

/*
 * Parse method name string to enum. Returns 0 on success, -1 if unknown.
 */
int yac_method_parse(const char *name, USYacMethod *out);

/*
 * Check if a method requires element connectivity.
 */
int yac_method_needs_connectivity(USYacMethod m);

/*
 * Get current interpolation method.
 */
USYacMethod yac_regrid_get_method(const USYacRegrid *r);

/*
 * Enable fractional-mask mode (--yac-3d).
 * In contrast to yac_regrid_apply, the source field is checked for fill
 * values, a dynamic fractional mask is generated accordingly, and applied.
 */
void yac_regrid_enable_frac(USYacRegrid *r);

/*
 * Check if fractional-mask mode is enabled.
 */
int yac_regrid_frac_enabled(const USYacRegrid *r);

/*
 * Apply regridding with dynamic fractional masking.
 * The source field is scanned for fill values; valid points receive a
 * fractional mask of 1.0, fill points 0.0.  The result is computed via
 * yac_interpolation_execute_frac so that partially-masked target cells
 * are weighted correctly and fully-masked cells receive the fill value.
 */
void yac_regrid_apply_frac(USYacRegrid *r,
                           const float *src, float fill, float *dst);

/*
 * Check if regrid uses a non-global region (box or polar projection).
 */
int yac_regrid_is_regional(const USYacRegrid *r);

/*
 * Free regridding structure.
 */
void yac_regrid_free(USYacRegrid *r);

#endif /* HAVE_YAC */
#endif /* REGRID_YAC_H */
