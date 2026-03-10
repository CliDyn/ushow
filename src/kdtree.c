/*
 * kdtree.c - Advanced KDTree implementation for 3D nearest-neighbor queries
 *
 * Based on libkdtree by Jörg Dietrich
 * (https://github.com/joergdietrich/libkdtree)
 *
 * Key improvements over the simple median-split implementation:
 * - Hyperrectangle bounds per node for tighter search pruning
 * - Parallelized tree construction using POSIX threads
 * - Multi-dimensional distance-to-rectangle pruning in NN search
 *
 * Adapted for 3D double-precision coordinates with the same public API.
 */

#define _GNU_SOURCE  /* for qsort_r on Linux */
#include "kdtree.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <pthread.h>

#define KDTREE_DIM 3

/* Minimum number of points before spawning a new thread */
#define KDTREE_THREAD_THRESHOLD 4096

/* KDTree node with hyperrectangle bounds (from libkdtree design) */
typedef struct KDNode {
    size_t          idx;                  /* Original point index */
    double          location[KDTREE_DIM]; /* Node position */
    double          hr_min[KDTREE_DIM];   /* Hyperrectangle min bounds */
    double          hr_max[KDTREE_DIM];   /* Hyperrectangle max bounds */
    int             split;                /* Split axis */
    struct KDNode  *left;
    struct KDNode  *right;
} KDNode;

/* KDTree structure */
struct KDTree {
    KDNode     *root;
    size_t      n_points;
    double     *points;         /* Copy of original points */
};

/* Thread argument for parallel tree construction (from libkdtree design) */
typedef struct {
    const double   *points;
    size_t         *indices;
    size_t          n;
    double          hr_min[KDTREE_DIM];
    double          hr_max[KDTREE_DIM];
    int             depth;
    int             max_threads;
} KDBuildArg;

/* -------------------------------------------------------------------
 * Comparison for qsort_r (thread-safe, no global state)
 * ------------------------------------------------------------------- */

typedef struct {
    const double *points;
    int axis;
} SortCtx;

static int compare_by_axis_r(const void *a, const void *b, void *arg) {
    SortCtx *ctx = (SortCtx *)arg;
    size_t ia = *(const size_t *)a;
    size_t ib = *(const size_t *)b;
    double va = ctx->points[ia * KDTREE_DIM + ctx->axis];
    double vb = ctx->points[ib * KDTREE_DIM + ctx->axis];
    if (va < vb) return -1;
    if (va > vb) return  1;
    return 0;
}

/* -------------------------------------------------------------------
 * Squared Euclidean distance
 * ------------------------------------------------------------------- */

static inline double dist_sq(const double *a, const double *b) {
    double dx = a[0] - b[0];
    double dy = a[1] - b[1];
    double dz = a[2] - b[2];
    return dx*dx + dy*dy + dz*dz;
}

/* -------------------------------------------------------------------
 * Minimum squared distance from a point to a hyperrectangle.
 * This is the key pruning improvement: instead of checking only the
 * split-axis distance (simple impl) or the distance to the further
 * node's split-axis bounds (original libkdtree), we compute the true
 * minimum distance across all dimensions.
 * ------------------------------------------------------------------- */

static inline double min_dist_sq_to_hr(const double *query,
                                       const double *hr_min,
                                       const double *hr_max) {
    double dsq = 0.0;
    for (int d = 0; d < KDTREE_DIM; d++) {
        if (query[d] < hr_min[d]) {
            double diff = hr_min[d] - query[d];
            dsq += diff * diff;
        } else if (query[d] > hr_max[d]) {
            double diff = query[d] - hr_max[d];
            dsq += diff * diff;
        }
    }
    return dsq;
}

/* -------------------------------------------------------------------
 * Tree construction (adapted from libkdtree: kd_doBuildTree)
 *
 * Each node stores the bounding hyperrectangle for its subtree,
 * enabling tighter pruning during nearest-neighbor search.
 * Construction can be parallelized across POSIX threads.
 * ------------------------------------------------------------------- */

static KDNode *build_tree(const double *points, size_t *indices, size_t n,
                          const double *hr_min, const double *hr_max,
                          int depth, int max_threads);
static void *build_tree_thread(void *arg);

static KDNode *build_tree(const double *points, size_t *indices, size_t n,
                          const double *hr_min, const double *hr_max,
                          int depth, int max_threads) {
    if (n == 0) return NULL;

    int axis = depth % KDTREE_DIM;

    /* Thread-safe sort by current axis (cf. libkdtree pmergesort) */
    SortCtx ctx = { .points = points, .axis = axis };
    qsort_r(indices, n, sizeof(size_t), compare_by_axis_r, &ctx);

    size_t median = n / 2;

    /* Allocate node with hyperrectangle bounds (cf. libkdtree kd_allocNode) */
    KDNode *node = malloc(sizeof(KDNode));
    if (!node) return NULL;

    node->idx   = indices[median];
    node->split = axis;
    memcpy(node->location, &points[node->idx * KDTREE_DIM],
           KDTREE_DIM * sizeof(double));
    memcpy(node->hr_min, hr_min, KDTREE_DIM * sizeof(double));
    memcpy(node->hr_max, hr_max, KDTREE_DIM * sizeof(double));
    node->left  = NULL;
    node->right = NULL;

    if (n == 1) return node;

    /* Child hyperrectangle bounds split at the median's coordinate
     * (cf. libkdtree: tmpMaxLeft[sortaxis] = node->location[sortaxis]) */
    double left_max[KDTREE_DIM], right_min[KDTREE_DIM];
    memcpy(left_max,  hr_max,  KDTREE_DIM * sizeof(double));
    memcpy(right_min, hr_min,  KDTREE_DIM * sizeof(double));
    left_max[axis]  = node->location[axis];
    right_min[axis] = node->location[axis];

    /* Parallel construction (cf. libkdtree: pthread_create in kd_doBuildTree) */
    if (max_threads > 1 && n > KDTREE_THREAD_THRESHOLD) {
        KDBuildArg left_arg = {
            .points      = points,
            .indices     = indices,
            .n           = median,
            .depth       = depth + 1,
            .max_threads = max_threads / 2
        };
        memcpy(left_arg.hr_min, hr_min,   KDTREE_DIM * sizeof(double));
        memcpy(left_arg.hr_max, left_max, KDTREE_DIM * sizeof(double));

        pthread_t tid;
        if (pthread_create(&tid, NULL, build_tree_thread, &left_arg) == 0) {
            /* Build right subtree in this thread */
            node->right = build_tree(points, indices + median + 1,
                                     n - median - 1, right_min, hr_max,
                                     depth + 1, max_threads / 2);
            pthread_join(tid, (void **)&node->left);
        } else {
            /* pthread_create failed – fall back to sequential */
            goto sequential;
        }
    } else {
sequential:
        node->left  = build_tree(points, indices, median,
                                 hr_min, left_max, depth + 1, max_threads);
        node->right = build_tree(points, indices + median + 1,
                                 n - median - 1, right_min, hr_max,
                                 depth + 1, max_threads);
    }

    return node;
}

static void *build_tree_thread(void *arg) {
    KDBuildArg *d = (KDBuildArg *)arg;
    return build_tree(d->points, d->indices, d->n,
                      d->hr_min, d->hr_max, d->depth, d->max_threads);
}

/* -------------------------------------------------------------------
 * Public: create tree
 * ------------------------------------------------------------------- */

KDTree *kdtree_create(const double *points, size_t n_points) {
    if (!points || n_points == 0) return NULL;

    KDTree *tree = malloc(sizeof(KDTree));
    if (!tree) return NULL;

    tree->n_points = n_points;

    /* Copy points */
    tree->points = malloc(n_points * KDTREE_DIM * sizeof(double));
    if (!tree->points) {
        free(tree);
        return NULL;
    }
    memcpy(tree->points, points, n_points * KDTREE_DIM * sizeof(double));

    /* Create index array */
    size_t *indices = malloc(n_points * sizeof(size_t));
    if (!indices) {
        free(tree->points);
        free(tree);
        return NULL;
    }

    /* Compute bounding hyperrectangle (cf. libkdtree: min/max args) */
    double bb_min[KDTREE_DIM], bb_max[KDTREE_DIM];
    for (int d = 0; d < KDTREE_DIM; d++) {
        bb_min[d] =  DBL_MAX;
        bb_max[d] = -DBL_MAX;
    }
    for (size_t i = 0; i < n_points; i++) {
        indices[i] = i;
        for (int d = 0; d < KDTREE_DIM; d++) {
            double v = points[i * KDTREE_DIM + d];
            if (v < bb_min[d]) bb_min[d] = v;
            if (v > bb_max[d]) bb_max[d] = v;
        }
    }

    /* Build tree with up to 4 threads (cf. libkdtree: max_threads param) */
    int max_threads = 4;
    tree->root = build_tree(tree->points, indices, n_points,
                            bb_min, bb_max, 0, max_threads);

    free(indices);

    if (!tree->root && n_points > 0) {
        free(tree->points);
        free(tree);
        return NULL;
    }

    return tree;
}

/* -------------------------------------------------------------------
 * Nearest-neighbor search (adapted from libkdtree: kd_nearest)
 *
 * Uses hyperrectangle bounds to prune the further subtree: if the
 * minimum distance from the query to the further child's bounding
 * box exceeds the current best, the entire subtree is skipped.
 * ------------------------------------------------------------------- */

static void search_nearest(const KDNode *node, const double *query,
                           size_t *best_idx, double *best_dist_sq) {
    if (!node) return;

    /* Check current node */
    double d = dist_sq(node->location, query);
    if (d < *best_dist_sq) {
        *best_dist_sq = d;
        *best_idx = node->idx;
    }

    /* Determine nearer / further subtree (cf. libkdtree kd_nearest) */
    const KDNode *nearer, *further;
    if (query[node->split] < node->location[node->split]) {
        nearer  = node->left;
        further = node->right;
    } else {
        nearer  = node->right;
        further = node->left;
    }

    /* Search closer subtree first */
    search_nearest(nearer, query, best_idx, best_dist_sq);

    /* Prune using minimum distance to further child's hyperrectangle.
     * This is tighter than the single-axis check in the simple
     * implementation, and uses all dimensions unlike the original
     * libkdtree which only checked the split axis. */
    if (further && min_dist_sq_to_hr(query, further->hr_min,
                                     further->hr_max) < *best_dist_sq) {
        search_nearest(further, query, best_idx, best_dist_sq);
    }
}

void kdtree_query_nearest(const KDTree *tree, const double *query,
                          size_t *nn_idx, double *nn_dist) {
    if (!tree || !tree->root || !query || !nn_idx || !nn_dist) {
        if (nn_idx) *nn_idx = 0;
        if (nn_dist) *nn_dist = DBL_MAX;
        return;
    }

    double best_dist_sq = DBL_MAX;
    *nn_idx = 0;

    search_nearest(tree->root, query, nn_idx, &best_dist_sq);

    /* Return actual distance (not squared) */
    *nn_dist = sqrt(best_dist_sq);
}

/* -------------------------------------------------------------------
 * Cleanup (cf. libkdtree: kd_destroyTree / kd_freeNode)
 * ------------------------------------------------------------------- */

static void free_node(KDNode *node) {
    if (!node) return;
    free_node(node->left);
    free_node(node->right);
    free(node);
}

void kdtree_free(KDTree *tree) {
    if (!tree) return;
    free_node(tree->root);
    free(tree->points);
    free(tree);
}

size_t kdtree_size(const KDTree *tree) {
    return tree ? tree->n_points : 0;
}
