/*
 * test_kdtree.c - Unit tests for KDTree implementation
 */

#include "test_framework.h"
#include "../src/kdtree.h"
#include <stdlib.h>
#include <float.h>

/* Test creating a simple tree with one point */
TEST(kdtree_create_single_point) {
    double points[] = {1.0, 0.0, 0.0};  /* x, y, z */

    KDTree *tree = kdtree_create(points, 1);
    ASSERT_NOT_NULL(tree);
    ASSERT_EQ_SIZET(kdtree_size(tree), 1);

    kdtree_free(tree);
    return 1;
}

/* Test creating tree with NULL points */
TEST(kdtree_create_null_points) {
    KDTree *tree = kdtree_create(NULL, 10);
    ASSERT_NULL(tree);
    return 1;
}

/* Test creating tree with zero points */
TEST(kdtree_create_zero_points) {
    double points[] = {1.0, 0.0, 0.0};
    KDTree *tree = kdtree_create(points, 0);
    ASSERT_NULL(tree);
    return 1;
}

/* Test creating tree with multiple points */
TEST(kdtree_create_multiple_points) {
    double points[] = {
        0.0, 0.0, 0.0,   /* point 0 */
        1.0, 0.0, 0.0,   /* point 1 */
        0.0, 1.0, 0.0,   /* point 2 */
        0.0, 0.0, 1.0,   /* point 3 */
        1.0, 1.0, 1.0    /* point 4 */
    };

    KDTree *tree = kdtree_create(points, 5);
    ASSERT_NOT_NULL(tree);
    ASSERT_EQ_SIZET(kdtree_size(tree), 5);

    kdtree_free(tree);
    return 1;
}

/* Test nearest neighbor query - exact match */
TEST(kdtree_query_exact_match) {
    double points[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0
    };

    KDTree *tree = kdtree_create(points, 3);
    ASSERT_NOT_NULL(tree);

    double query[] = {1.0, 0.0, 0.0};  /* Should find point 1 */
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

    ASSERT_EQ_SIZET(nn_idx, 1);
    ASSERT_NEAR(nn_dist, 0.0, 1e-10);

    kdtree_free(tree);
    return 1;
}

/* Test nearest neighbor query - closest match */
TEST(kdtree_query_closest) {
    double points[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0
    };

    KDTree *tree = kdtree_create(points, 3);
    ASSERT_NOT_NULL(tree);

    /* Query point slightly closer to point 1 than others */
    double query[] = {0.9, 0.0, 0.0};
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

    ASSERT_EQ_SIZET(nn_idx, 1);
    ASSERT_NEAR(nn_dist, 0.1, 1e-10);

    kdtree_free(tree);
    return 1;
}

/* Test nearest neighbor with unit sphere points */
TEST(kdtree_query_unit_sphere) {
    /* Points on unit sphere (like Cartesian coords from lonlat) */
    double points[] = {
        1.0, 0.0, 0.0,   /* lon=0, lat=0 */
        0.0, 1.0, 0.0,   /* lon=90, lat=0 */
        0.0, 0.0, 1.0,   /* lat=90 (north pole) */
        -1.0, 0.0, 0.0,  /* lon=180, lat=0 */
        0.0, -1.0, 0.0   /* lon=-90, lat=0 */
    };

    KDTree *tree = kdtree_create(points, 5);
    ASSERT_NOT_NULL(tree);

    /* Query for north pole */
    double query[] = {0.0, 0.0, 0.99};
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

    ASSERT_EQ_SIZET(nn_idx, 2);  /* North pole is point 2 */
    ASSERT_LT(nn_dist, 0.02);

    kdtree_free(tree);
    return 1;
}

/* Test query with NULL tree */
TEST(kdtree_query_null_tree) {
    double query[] = {0.0, 0.0, 0.0};
    size_t nn_idx = 999;
    double nn_dist = -1.0;

    kdtree_query_nearest(NULL, query, &nn_idx, &nn_dist);

    ASSERT_EQ_SIZET(nn_idx, 0);
    ASSERT_EQ(nn_dist, DBL_MAX);

    return 1;
}

/* Test with many points (stress test) */
TEST(kdtree_stress_test) {
    size_t n = 1000;
    double *points = malloc(n * 3 * sizeof(double));
    ASSERT_NOT_NULL(points);

    /* Create grid of points */
    for (size_t i = 0; i < n; i++) {
        points[i * 3 + 0] = (double)(i % 10) / 10.0;
        points[i * 3 + 1] = (double)((i / 10) % 10) / 10.0;
        points[i * 3 + 2] = (double)(i / 100) / 10.0;
    }

    KDTree *tree = kdtree_create(points, n);
    ASSERT_NOT_NULL(tree);
    ASSERT_EQ_SIZET(kdtree_size(tree), n);

    /* Query 100 random points */
    for (int q = 0; q < 100; q++) {
        double query[] = {0.05, 0.05, 0.05};  /* Should be close to point 55 */
        size_t nn_idx;
        double nn_dist;

        kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

        ASSERT_LT(nn_idx, n);
        ASSERT_GE(nn_dist, 0.0);
    }

    kdtree_free(tree);
    free(points);
    return 1;
}

/* Test kdtree_size with NULL */
TEST(kdtree_size_null) {
    size_t size = kdtree_size(NULL);
    ASSERT_EQ_SIZET(size, 0);
    return 1;
}

/* Test that free handles NULL gracefully */
TEST(kdtree_free_null) {
    kdtree_free(NULL);  /* Should not crash */
    return 1;
}

/* Test correctness of nearest neighbor in 3D */
TEST(kdtree_nn_correctness_3d) {
    /* 8 corners of a unit cube */
    double points[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        1.0, 1.0, 0.0,
        0.0, 0.0, 1.0,
        1.0, 0.0, 1.0,
        0.0, 1.0, 1.0,
        1.0, 1.0, 1.0
    };

    KDTree *tree = kdtree_create(points, 8);
    ASSERT_NOT_NULL(tree);

    /* Query center of cube - should find corner but distance sqrt(3)/2 */
    double center[] = {0.5, 0.5, 0.5};
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, center, &nn_idx, &nn_dist);

    /* Distance from center to any corner is sqrt(0.5^2 + 0.5^2 + 0.5^2) = sqrt(0.75) */
    double expected_dist = sqrt(0.75);
    ASSERT_NEAR(nn_dist, expected_dist, 1e-10);
    ASSERT_LT(nn_idx, 8);

    kdtree_free(tree);
    return 1;
}

/* Test distance calculation accuracy */
TEST(kdtree_distance_accuracy) {
    double points[] = {0.0, 0.0, 0.0, 3.0, 4.0, 0.0};

    KDTree *tree = kdtree_create(points, 2);
    ASSERT_NOT_NULL(tree);

    double query[] = {0.0, 0.0, 0.0};
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

    /* Point (0,0,0) is exact match */
    ASSERT_EQ_SIZET(nn_idx, 0);
    ASSERT_NEAR(nn_dist, 0.0, 1e-10);

    /* Query near point 1 */
    double query2[] = {3.0, 4.0, 0.0};
    kdtree_query_nearest(tree, query2, &nn_idx, &nn_dist);

    ASSERT_EQ_SIZET(nn_idx, 1);
    ASSERT_NEAR(nn_dist, 0.0, 1e-10);

    /* Query midpoint - should find nearest with distance 2.5 */
    double query3[] = {1.5, 2.0, 0.0};
    kdtree_query_nearest(tree, query3, &nn_idx, &nn_dist);

    /* Both points are equidistant (2.5), either is valid */
    ASSERT_NEAR(nn_dist, 2.5, 1e-10);

    kdtree_free(tree);
    return 1;
}

/* Test collinear points */
TEST(kdtree_collinear_points) {
    /* Points along x-axis */
    double points[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        2.0, 0.0, 0.0,
        3.0, 0.0, 0.0,
        4.0, 0.0, 0.0
    };

    KDTree *tree = kdtree_create(points, 5);
    ASSERT_NOT_NULL(tree);

    /* Query between points 2 and 3 */
    double query[] = {2.3, 0.0, 0.0};
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

    ASSERT_EQ_SIZET(nn_idx, 2);  /* Point at x=2.0 is closest */
    ASSERT_NEAR(nn_dist, 0.3, 1e-10);

    kdtree_free(tree);
    return 1;
}

/* Test duplicate points */
TEST(kdtree_duplicate_points) {
    double points[] = {
        1.0, 1.0, 1.0,
        1.0, 1.0, 1.0,  /* Duplicate */
        2.0, 2.0, 2.0
    };

    KDTree *tree = kdtree_create(points, 3);
    ASSERT_NOT_NULL(tree);

    double query[] = {1.0, 1.0, 1.0};
    size_t nn_idx;
    double nn_dist;

    kdtree_query_nearest(tree, query, &nn_idx, &nn_dist);

    /* Should find one of the duplicates with distance 0 */
    ASSERT_TRUE(nn_idx == 0 || nn_idx == 1);
    ASSERT_NEAR(nn_dist, 0.0, 1e-10);

    kdtree_free(tree);
    return 1;
}

RUN_TESTS("KDTree")
