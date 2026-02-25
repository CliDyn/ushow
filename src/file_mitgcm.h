/*
 * file_mitgcm.h - MITgcm MDS binary data reading
 *
 * Reads MITgcm .data/.meta file pairs (MDS format).
 * Always compiled (no external library dependencies).
 */

#ifndef FILE_MITGCM_H
#define FILE_MITGCM_H

#include "ushow.defines.h"

/*
 * Check if a path looks like MITgcm data (directory with .meta files,
 * or a .data file with companion .meta).
 */
int mitgcm_is_mitgcm(const char *path);

/*
 * Open MITgcm data. Path can be a directory or a .data file.
 * Resolves grid dimensions from meta files, loads RC and hFacC if present.
 */
USFile *mitgcm_open(const char *path);

/*
 * Create mesh from XC.data/YC.data grid files.
 */
USMesh *mitgcm_create_mesh(USFile *file);

/*
 * Scan directory for diagnostic variables.
 * Returns linked list of USVar.
 */
USVar *mitgcm_scan_variables(USFile *f, USMesh *m);

/*
 * Read a 2D slice of data.
 */
int mitgcm_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data);

/*
 * Estimate min/max range by sampling.
 */
int mitgcm_estimate_range(USVar *var, float *min_val, float *max_val);

/*
 * Get dimension info (time iterations, depth levels).
 */
USDimInfo *mitgcm_get_dim_info(USVar *var, int *n_dims_out);

/*
 * Free dimension info.
 */
void mitgcm_free_dim_info(USDimInfo *dims, int n_dims);

/*
 * Read time series at a single spatial node.
 */
int mitgcm_read_timeseries(USVar *var, size_t node_idx, size_t depth_idx,
                           double **times_out, float **values_out,
                           int **valid_out, size_t *n_out);

/*
 * Close and free MITgcm file data.
 */
void mitgcm_close(USFile *file);

#endif /* FILE_MITGCM_H */
