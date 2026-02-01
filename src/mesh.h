/*
 * mesh.h - Mesh and coordinate handling
 */

#ifndef MESH_H
#define MESH_H

#include "ushow.defines.h"

/*
 * Convert longitude/latitude to Cartesian on unit sphere.
 */
void lonlat_to_cartesian(double lon_deg, double lat_deg,
                         double *x, double *y, double *z);

/*
 * Convert an array of lon/lat to Cartesian coordinates.
 * lon, lat: input arrays [n_points]
 * xyz: output array [n_points * 3] in x,y,z,x,y,z,... layout
 */
void lonlat_to_cartesian_batch(const double *lon, const double *lat,
                               double *xyz, size_t n_points);

/*
 * Convert meters to chord distance on unit sphere.
 */
double meters_to_chord(double meters);

/*
 * Create mesh from coordinate arrays.
 * Takes ownership of lon and lat arrays.
 */
USMesh *mesh_create(double *lon, double *lat, size_t n_points, CoordType type);

/*
 * Create mesh by loading coordinates from a NetCDF file.
 * If mesh_filename is NULL, coordinates are read from data_ncid.
 */
USMesh *mesh_create_from_netcdf(int data_ncid, const char *mesh_filename);

#ifdef HAVE_ZARR
/*
 * Create mesh by loading coordinates from a Zarr store.
 * Coordinates are expected in latitude/longitude arrays within the store.
 */
USMesh *mesh_create_from_zarr(USFile *file);
#endif

/*
 * Free mesh and all associated memory.
 */
void mesh_free(USMesh *mesh);

#endif /* MESH_H */
