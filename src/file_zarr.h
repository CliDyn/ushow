/*
 * file_zarr.h - Zarr v2 file reading (optional compile-time feature)
 *
 * Build with: make WITH_ZARR=1
 * Requires: c-blosc, lz4 libraries
 */

#ifndef FILE_ZARR_H
#define FILE_ZARR_H

#include "ushow.defines.h"

#ifdef HAVE_ZARR

/*
 * Check if path is a zarr store (directory containing .zgroup).
 * Returns 1 if zarr, 0 otherwise.
 */
int zarr_is_zarr_store(const char *path);

/*
 * Open a zarr store.
 * path: path to zarr directory (e.g., "data.zarr/")
 * Returns NULL on error.
 */
USFile *zarr_open(const char *path);

/*
 * Scan zarr store for displayable variables.
 * Returns linked list of variables.
 */
USVar *zarr_scan_variables(USFile *file, USMesh *mesh);

/*
 * Read a 2D slice of data from a zarr variable.
 * var: variable to read
 * time_idx: time index (ignored if no time dimension)
 * depth_idx: depth index (ignored if no depth dimension)
 * data: output buffer [n_points], must be preallocated
 */
int zarr_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data);

/*
 * Estimate min/max range for a variable by sampling.
 */
int zarr_estimate_range(USVar *var, float *min_val, float *max_val);

/*
 * Read dimension coordinate values and metadata.
 * Returns allocated USDimInfo array (must be freed with zarr_free_dim_info).
 */
USDimInfo *zarr_get_dim_info(USVar *var, int *n_dims_out);

/*
 * Free dimension info array.
 */
void zarr_free_dim_info(USDimInfo *dims, int n_dims);

/*
 * Close zarr store and free all resources.
 */
void zarr_close(USFile *file);

/*
 * Multi-file zarr support
 */

/*
 * Create a zarr file set from a glob pattern.
 * Returns NULL on error or if no files match.
 */
USFileSet *zarr_open_glob(const char *pattern);

/*
 * Create a zarr file set from an array of paths.
 * Returns NULL on error.
 */
USFileSet *zarr_open_fileset(const char **paths, int n_files);

/*
 * Read a slice from a zarr file set using virtual time index.
 */
int zarr_read_slice_fileset(USFileSet *fs, USVar *var,
                            size_t virtual_time, size_t depth_idx, float *data);

/*
 * Get dimension info with virtual time from zarr fileset.
 */
USDimInfo *zarr_get_dim_info_fileset(USFileSet *fs, USVar *var, int *n_dims_out);

/*
 * Close all zarr stores in a file set.
 */
void zarr_close_fileset(USFileSet *fs);

/*
 * Read time series at a single spatial node across all time steps.
 * Reads one slice per time step and extracts the node value.
 */
int zarr_read_timeseries(USVar *var, size_t node_idx, size_t depth_idx,
                         double **times_out, float **values_out,
                         int **valid_out, size_t *n_out);

/*
 * Read time series at a single spatial node across all files in a fileset.
 */
int zarr_read_timeseries_fileset(USFileSet *fs, USVar *var,
                                 size_t node_idx, size_t depth_idx,
                                 double **times_out, float **values_out,
                                 int **valid_out, size_t *n_out);

/*
 * Get total time steps across all files.
 */
size_t zarr_fileset_total_times(USFileSet *fs);

#endif /* HAVE_ZARR */
#endif /* FILE_ZARR_H */
