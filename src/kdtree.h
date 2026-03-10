/*
 * kdtree.h - Simple KDTree for 3D nearest-neighbor queries
 *
 * Optimized for single nearest-neighbor queries on unit sphere coordinates.
 */

#ifndef KDTREE_H
#define KDTREE_H

#include <stddef.h>

typedef struct KDTree KDTree;

/*
 * Create KDTree from points array.
 * points: array of coordinates in [x0,y0,z0, x1,y1,z1, ...] layout
 * n_points: number of points
 * Returns: KDTree handle or NULL on failure
 */
KDTree *kdtree_create(const double *points, size_t n_points);

/*
 * Query single nearest neighbor.
 * tree: KDTree handle
 * query: query point [x, y, z]
 * nn_idx: output nearest neighbor index
 * nn_dist: output distance to nearest neighbor (squared Euclidean)
 */
void kdtree_query_nearest(const KDTree *tree, const double *query,
                          size_t *nn_idx, double *nn_dist);

/*
 * Free KDTree and all associated memory.
 */
void kdtree_free(KDTree *tree);

/*
 * Get number of points in tree.
 */
size_t kdtree_size(const KDTree *tree);

/*
 * Set the maximum number of threads for parallel tree construction.
 * If never called, kdtree_create uses OMP_NUM_THREADS (if set),
 * otherwise falls back to KDTREE_DEFAULT_THREADS (4).
 * A value <= 0 is ignored.
 */
void kdtree_set_max_threads(int n);

#endif /* KDTREE_H */
