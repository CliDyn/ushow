/*
 * file_netcdf.h - NetCDF file reading
 */

#ifndef FILE_NETCDF_H
#define FILE_NETCDF_H

#include "ushow.defines.h"

/*
 * Open a NetCDF file and create file structure.
 */
USFile *netcdf_open(const char *filename);

/*
 * Scan file for displayable variables (2D, 3D, 4D with spatial dimensions).
 * Returns linked list of variables.
 */
USVar *netcdf_scan_variables(USFile *file, USMesh *mesh);

/*
 * Read a 2D slice of data from a variable.
 * var: variable to read
 * time_idx: time index (ignored if no time dimension)
 * depth_idx: depth index (ignored if no depth dimension)
 * data: output buffer [n_points], must be preallocated
 */
int netcdf_read_slice(USVar *var, size_t time_idx, size_t depth_idx, float *data);

/*
 * Estimate min/max range for a variable by sampling.
 */
int netcdf_estimate_range(USVar *var, float *min_val, float *max_val);

/*
 * Close NetCDF file.
 */
void netcdf_close(USFile *file);

/*
 * Read dimension coordinate values and metadata.
 * Returns allocated USDimInfo array (must be freed with netcdf_free_dim_info).
 * n_dims_out: number of dimensions returned
 */
USDimInfo *netcdf_get_dim_info(USVar *var, int *n_dims_out);

/*
 * Free dimension info array.
 */
void netcdf_free_dim_info(USDimInfo *dims, int n_dims);

/*
 * Multi-file support
 */

/*
 * Create a file set from an array of filenames.
 * Files are opened and sorted alphabetically by filename.
 * Returns NULL on error.
 */
USFileSet *netcdf_open_fileset(const char **filenames, int n_files);

/*
 * Create a file set from a glob pattern (e.g., "data.*.nc").
 * Returns NULL on error or if no files match.
 */
USFileSet *netcdf_open_glob(const char *pattern);

/*
 * Map a virtual time index to file index and local time index.
 * Returns 0 on success, -1 on error (index out of range).
 */
int netcdf_fileset_map_time(USFileSet *fs, size_t virtual_time,
                            int *file_idx_out, size_t *local_time_out);

/*
 * Read a slice from a file set using virtual time index.
 * var must be from the first file (fs->files[0]).
 */
int netcdf_read_slice_fileset(USFileSet *fs, USVar *var,
                              size_t virtual_time, size_t depth_idx, float *data);

/*
 * Get total time steps across all files.
 */
size_t netcdf_fileset_total_times(USFileSet *fs);

/*
 * Get dimension info with virtual time from fileset.
 */
USDimInfo *netcdf_get_dim_info_fileset(USFileSet *fs, USVar *var, int *n_dims_out);

/*
 * Close all files in a file set.
 */
void netcdf_close_fileset(USFileSet *fs);

#endif /* FILE_NETCDF_H */
