/*
 * kdtree.c - Simple KDTree implementation for 3D nearest-neighbor queries
 *
 * Uses median-split construction and recursive nearest-neighbor search.
 * Optimized for the case of building once and querying many times.
 */

#include "kdtree.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define KDTREE_DIM 3

/* KDTree node */
typedef struct KDNode {
    size_t          idx;        /* Original point index */
    double          point[KDTREE_DIM];
    struct KDNode  *left;
    struct KDNode  *right;
} KDNode;

/* KDTree structure */
struct KDTree {
    KDNode     *root;
    size_t      n_points;
    double     *points;         /* Copy of original points */
};

/* Comparison context for qsort */
typedef struct {
    const double *points;
    int axis;
} SortContext;

static SortContext sort_ctx;

/* Comparison function for sorting indices by coordinate */
static int compare_by_axis(const void *a, const void *b) {
    size_t ia = *(const size_t *)a;
    size_t ib = *(const size_t *)b;
    double va = sort_ctx.points[ia * KDTREE_DIM + sort_ctx.axis];
    double vb = sort_ctx.points[ib * KDTREE_DIM + sort_ctx.axis];
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* Build KDTree recursively */
static KDNode *build_tree(const double *points, size_t *indices,
                          size_t n, int depth) {
    if (n == 0) return NULL;

    int axis = depth % KDTREE_DIM;

    /* Sort indices by current axis */
    sort_ctx.points = points;
    sort_ctx.axis = axis;
    qsort(indices, n, sizeof(size_t), compare_by_axis);

    /* Find median */
    size_t median = n / 2;

    /* Create node */
    KDNode *node = malloc(sizeof(KDNode));
    if (!node) return NULL;

    node->idx = indices[median];
    node->point[0] = points[node->idx * KDTREE_DIM + 0];
    node->point[1] = points[node->idx * KDTREE_DIM + 1];
    node->point[2] = points[node->idx * KDTREE_DIM + 2];

    /* Build subtrees */
    node->left = build_tree(points, indices, median, depth + 1);
    node->right = build_tree(points, indices + median + 1, n - median - 1, depth + 1);

    return node;
}

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
    for (size_t i = 0; i < n_points; i++) {
        indices[i] = i;
    }

    /* Build tree */
    tree->root = build_tree(tree->points, indices, n_points, 0);

    free(indices);

    if (!tree->root && n_points > 0) {
        free(tree->points);
        free(tree);
        return NULL;
    }

    return tree;
}

/* Squared Euclidean distance */
static inline double dist_sq(const double *a, const double *b) {
    double dx = a[0] - b[0];
    double dy = a[1] - b[1];
    double dz = a[2] - b[2];
    return dx*dx + dy*dy + dz*dz;
}

/* Recursive nearest neighbor search */
static void search_nearest(const KDNode *node, const double *query,
                           int depth, size_t *best_idx, double *best_dist_sq) {
    if (!node) return;

    /* Check current node */
    double d = dist_sq(node->point, query);
    if (d < *best_dist_sq) {
        *best_dist_sq = d;
        *best_idx = node->idx;
    }

    /* Determine which subtree to search first */
    int axis = depth % KDTREE_DIM;
    double diff = query[axis] - node->point[axis];

    KDNode *first = (diff < 0) ? node->left : node->right;
    KDNode *second = (diff < 0) ? node->right : node->left;

    /* Search closer subtree first */
    search_nearest(first, query, depth + 1, best_idx, best_dist_sq);

    /* Check if we need to search the other subtree */
    if (diff * diff < *best_dist_sq) {
        search_nearest(second, query, depth + 1, best_idx, best_dist_sq);
    }
}

void kdtree_query_nearest(const KDTree *tree, const double *query,
                          size_t *nn_idx, double *nn_dist) {
    if (!tree || !tree->root || !query || !nn_idx || !nn_dist) {
        if (nn_idx) *nn_idx = 0;
        if (nn_dist) *nn_dist = DBL_MAX;
        return;
    }

    *nn_dist = DBL_MAX;
    *nn_idx = 0;

    search_nearest(tree->root, query, 0, nn_idx, nn_dist);

    /* Return actual distance (not squared) */
    *nn_dist = sqrt(*nn_dist);
}

/* Free tree recursively */
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
