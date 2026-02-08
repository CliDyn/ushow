/*
 * file_grib.h - GRIB file reading (optional compile-time feature)
 *
 * Build with: make WITH_GRIB=1
 * Requires: eccodes library
 */

#ifndef FILE_GRIB_H
#define FILE_GRIB_H

#include "ushow.defines.h"

#ifdef HAVE_GRIB

/*
 * Check if path is a GRIB file.
 * Returns 1 if GRIB, 0 otherwise.
 */
int grib_is_grib_file(const char *path);

/*
 * Open a GRIB file.
 * Returns NULL on error.
 */
USFile *grib_open(const char *filename);

/*
 * Create a mesh by loading coordinates from the first GRIB message.
 * Uses eccodes to retrieve lat/lon arrays. Returns NULL on error.
 */
USMesh *grib_create_mesh(USFile *file);

/*
 * Scan GRIB file for displayable variables.
 * Returns linked list of variables.
 */
USVar *grib_scan_variables(USFile *file, USMesh *mesh);

/*
 * Read a 2D slice of data from a GRIB variable.
 * var: variable to read
 * time_idx: time index (ignored if no time dimension)
 * depth_idx: depth index (ignored if no depth dimension)
 * data: output buffer [n_points], must be preallocated
 */
int grib_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data);

/*
 * Estimate min/max range for a variable by sampling.
 */
int grib_estimate_range(USVar *var, float *min_val, float *max_val);

/*
 * Close GRIB file and free resources.
 */
void grib_close(USFile *file);

/*
 * Read dimension coordinate values and metadata.
 * Returns allocated USDimInfo array (must be freed with grib_free_dim_info).
 */
USDimInfo *grib_get_dim_info(USVar *var, int *n_dims_out);

/*
 * Free dimension info array.
 */
void grib_free_dim_info(USDimInfo *dims, int n_dims);

/*
 * Read time series at a single spatial node across all time steps.
 */
int grib_read_timeseries(USVar *var, size_t node_idx, size_t depth_idx,
                         double **times_out, float **values_out,
                         int **valid_out, size_t *n_out);

/*
 * Multi-file support
 */

/*
 * Create a file set from an array of GRIB filenames.
 * Files are opened and sorted alphabetically by filename.
 * Returns NULL on error.
 */
USFileSet *grib_open_fileset(const char **filenames, int n_files);

/*
 * Create a file set from a glob pattern (e.g., "data.*.grb").
 * Returns NULL on error or if no files match.
 */
USFileSet *grib_open_glob(const char *pattern);

/*
 * Map a virtual time index to file index and local time index.
 * Returns 0 on success, -1 on error (index out of range).
 */
int grib_fileset_map_time(USFileSet *fs, size_t virtual_time,
                          int *file_idx_out, size_t *local_time_out);

/*
 * Read a slice from a file set using virtual time index.
 * var must be from the first file (fs->files[0]).
 */
int grib_read_slice_fileset(USFileSet *fs, USVar *var,
                            size_t virtual_time, size_t depth_idx, float *data);

/*
 * Get total time steps across all files.
 */
size_t grib_fileset_total_times(USFileSet *fs);

/*
 * Get dimension info with virtual time from fileset.
 */
USDimInfo *grib_get_dim_info_fileset(USFileSet *fs, USVar *var, int *n_dims_out);

/*
 * Close all files in a file set.
 */
void grib_close_fileset(USFileSet *fs);

/*
 * Read time series at a single spatial node across all files in a fileset.
 * Same interface as grib_read_timeseries but concatenates across files.
 */
int grib_read_timeseries_fileset(USFileSet *fs, USVar *var,
                                 size_t node_idx, size_t depth_idx,
                                 double **times_out, float **values_out,
                                 int **valid_out, size_t *n_out);

#endif /* HAVE_GRIB */
#endif /* FILE_GRIB_H */
