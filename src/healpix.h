/*
 * healpix.h - Latitude-band connectivity generation for unstructured grids
 *
 * Generates triangular element connectivity from point clouds organized
 * in latitude bands (HEALPix, reduced Gaussian, etc.), enabling YAC
 * averaging interpolation methods.
 */

#ifndef HEALPIX_H
#define HEALPIX_H

#include "ushow.defines.h"

/*
 * Generate triangular element connectivity for a mesh whose points are
 * organized in latitude bands (e.g., HEALPix RING, reduced Gaussian).
 *
 * Detects latitude bands from coordinate data, sorts each band by longitude,
 * and creates strip triangulations between adjacent bands.
 *
 * Sets mesh->n_elements, mesh->n_vertices (=3), mesh->elem_nodes.
 * Returns 0 on success, -1 on failure.
 */
int latband_generate_connectivity(USMesh *mesh);

#endif /* HEALPIX_H */
