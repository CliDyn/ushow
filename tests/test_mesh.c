/*
 * test_mesh.c - Unit tests for mesh and coordinate handling
 */

#include "test_framework.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include "../src/ushow.defines.h"
#include "../src/mesh.h"
#include <stdlib.h>

/* Tolerance for floating point comparisons */
#define EPSILON 1e-10
#define EPSILON_LOOSE 1e-6

/* Test lonlat_to_cartesian at equator, prime meridian (0, 0) */
TEST(lonlat_to_cartesian_origin) {
    double x, y, z;
    lonlat_to_cartesian(0.0, 0.0, &x, &y, &z);

    /* At lon=0, lat=0: x=1, y=0, z=0 (unit sphere) */
    ASSERT_NEAR(x, 1.0, EPSILON);
    ASSERT_NEAR(y, 0.0, EPSILON);
    ASSERT_NEAR(z, 0.0, EPSILON);

    return 1;
}

/* Test lonlat_to_cartesian at north pole */
TEST(lonlat_to_cartesian_north_pole) {
    double x, y, z;
    lonlat_to_cartesian(0.0, 90.0, &x, &y, &z);

    /* At north pole: x=0, y=0, z=1 */
    ASSERT_NEAR(x, 0.0, EPSILON);
    ASSERT_NEAR(y, 0.0, EPSILON);
    ASSERT_NEAR(z, 1.0, EPSILON);

    return 1;
}

/* Test lonlat_to_cartesian at south pole */
TEST(lonlat_to_cartesian_south_pole) {
    double x, y, z;
    lonlat_to_cartesian(0.0, -90.0, &x, &y, &z);

    /* At south pole: x=0, y=0, z=-1 */
    ASSERT_NEAR(x, 0.0, EPSILON);
    ASSERT_NEAR(y, 0.0, EPSILON);
    ASSERT_NEAR(z, -1.0, EPSILON);

    return 1;
}

/* Test lonlat_to_cartesian at lon=90 (y-axis) */
TEST(lonlat_to_cartesian_lon90) {
    double x, y, z;
    lonlat_to_cartesian(90.0, 0.0, &x, &y, &z);

    /* At lon=90, lat=0: x=0, y=1, z=0 */
    ASSERT_NEAR(x, 0.0, EPSILON);
    ASSERT_NEAR(y, 1.0, EPSILON);
    ASSERT_NEAR(z, 0.0, EPSILON);

    return 1;
}

/* Test lonlat_to_cartesian at lon=-90 */
TEST(lonlat_to_cartesian_lon_minus90) {
    double x, y, z;
    lonlat_to_cartesian(-90.0, 0.0, &x, &y, &z);

    /* At lon=-90, lat=0: x=0, y=-1, z=0 */
    ASSERT_NEAR(x, 0.0, EPSILON);
    ASSERT_NEAR(y, -1.0, EPSILON);
    ASSERT_NEAR(z, 0.0, EPSILON);

    return 1;
}

/* Test lonlat_to_cartesian at lon=180 */
TEST(lonlat_to_cartesian_lon180) {
    double x, y, z;
    lonlat_to_cartesian(180.0, 0.0, &x, &y, &z);

    /* At lon=180, lat=0: x=-1, y=0, z=0 */
    ASSERT_NEAR(x, -1.0, EPSILON);
    ASSERT_NEAR(y, 0.0, EPSILON_LOOSE);
    ASSERT_NEAR(z, 0.0, EPSILON);

    return 1;
}

/* Test lonlat_to_cartesian at lat=45 */
TEST(lonlat_to_cartesian_lat45) {
    double x, y, z;
    lonlat_to_cartesian(0.0, 45.0, &x, &y, &z);

    /* At lon=0, lat=45: x=cos(45)=sqrt(2)/2, y=0, z=sin(45)=sqrt(2)/2 */
    double cos45 = cos(45.0 * M_PI / 180.0);
    double sin45 = sin(45.0 * M_PI / 180.0);

    ASSERT_NEAR(x, cos45, EPSILON);
    ASSERT_NEAR(y, 0.0, EPSILON);
    ASSERT_NEAR(z, sin45, EPSILON);

    return 1;
}

/* Test that all points are on unit sphere */
TEST(lonlat_to_cartesian_unit_sphere) {
    /* Test many random points */
    double lons[] = {0, 45, 90, 135, 180, -45, -90, -135, -180};
    double lats[] = {0, 30, 45, 60, 90, -30, -45, -60, -90};

    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 9; j++) {
            double x, y, z;
            lonlat_to_cartesian(lons[i], lats[j], &x, &y, &z);

            /* Distance from origin should be 1.0 */
            double r = sqrt(x*x + y*y + z*z);
            ASSERT_NEAR(r, 1.0, EPSILON);
        }
    }

    return 1;
}

/* Test lonlat_to_cartesian_batch */
TEST(lonlat_to_cartesian_batch_basic) {
    double lon[] = {0.0, 90.0, 0.0};
    double lat[] = {0.0, 0.0, 90.0};
    double xyz[9];

    lonlat_to_cartesian_batch(lon, lat, xyz, 3);

    /* Point 0: lon=0, lat=0 -> (1, 0, 0) */
    ASSERT_NEAR(xyz[0], 1.0, EPSILON);
    ASSERT_NEAR(xyz[1], 0.0, EPSILON);
    ASSERT_NEAR(xyz[2], 0.0, EPSILON);

    /* Point 1: lon=90, lat=0 -> (0, 1, 0) */
    ASSERT_NEAR(xyz[3], 0.0, EPSILON);
    ASSERT_NEAR(xyz[4], 1.0, EPSILON);
    ASSERT_NEAR(xyz[5], 0.0, EPSILON);

    /* Point 2: lon=0, lat=90 -> (0, 0, 1) */
    ASSERT_NEAR(xyz[6], 0.0, EPSILON);
    ASSERT_NEAR(xyz[7], 0.0, EPSILON);
    ASSERT_NEAR(xyz[8], 1.0, EPSILON);

    return 1;
}

/* Test meters_to_chord for zero distance */
TEST(meters_to_chord_zero) {
    double chord = meters_to_chord(0.0);
    ASSERT_NEAR(chord, 0.0, EPSILON);
    return 1;
}

/* Test meters_to_chord for quarter circumference */
TEST(meters_to_chord_quarter) {
    /* Quarter of Earth's circumference = pi/2 radians arc
     * Chord = 2*sin(pi/4) = sqrt(2) */
    double quarter_circ = M_PI * EARTH_RADIUS_M / 2.0;
    double chord = meters_to_chord(quarter_circ);

    ASSERT_NEAR(chord, sqrt(2.0), EPSILON_LOOSE);
    return 1;
}

/* Test meters_to_chord for 200km (default influence radius) */
TEST(meters_to_chord_200km) {
    double chord = meters_to_chord(200000.0);

    /* For small distances, chord ~ arc length / R_earth */
    /* But actual formula: chord = 2 * sin(arc_radians / 2) */
    double arc_radians = 200000.0 / EARTH_RADIUS_M;
    double expected = 2.0 * sin(arc_radians / 2.0);

    ASSERT_NEAR(chord, expected, EPSILON);
    ASSERT_GT(chord, 0.0);
    ASSERT_LT(chord, 0.1);  /* Should be small */

    return 1;
}

/* Test mesh_create basic */
TEST(mesh_create_basic) {
    double *lon = malloc(3 * sizeof(double));
    double *lat = malloc(3 * sizeof(double));

    lon[0] = 0.0; lat[0] = 0.0;
    lon[1] = 90.0; lat[1] = 0.0;
    lon[2] = 0.0; lat[2] = 90.0;

    USMesh *mesh = mesh_create(lon, lat, 3, COORD_TYPE_1D_UNSTRUCTURED);
    ASSERT_NOT_NULL(mesh);

    ASSERT_EQ_SIZET(mesh->n_points, 3);
    ASSERT_EQ(mesh->coord_type, COORD_TYPE_1D_UNSTRUCTURED);
    ASSERT_NOT_NULL(mesh->xyz);

    /* Verify Cartesian coordinates */
    ASSERT_NEAR(mesh->xyz[0], 1.0, EPSILON);  /* Point 0: x */
    ASSERT_NEAR(mesh->xyz[1], 0.0, EPSILON);  /* Point 0: y */
    ASSERT_NEAR(mesh->xyz[2], 0.0, EPSILON);  /* Point 0: z */

    mesh_free(mesh);
    return 1;
}

/* Test mesh_create with NULL inputs */
TEST(mesh_create_null_lon) {
    double *lat = malloc(3 * sizeof(double));
    lat[0] = lat[1] = lat[2] = 0.0;

    /* This might return NULL or crash - we just want to ensure we handle it */
    /* Note: The actual implementation takes ownership, so we need different test */
    free(lat);
    return 1;
}

/* Test mesh_create with various coord types */
TEST(mesh_create_coord_types) {
    /* Test COORD_TYPE_1D_STRUCTURED */
    double *lon1 = malloc(4 * sizeof(double));
    double *lat1 = malloc(4 * sizeof(double));
    for (int i = 0; i < 4; i++) {
        lon1[i] = i * 10.0;
        lat1[i] = i * 5.0;
    }

    USMesh *mesh1 = mesh_create(lon1, lat1, 4, COORD_TYPE_1D_STRUCTURED);
    ASSERT_NOT_NULL(mesh1);
    ASSERT_EQ(mesh1->coord_type, COORD_TYPE_1D_STRUCTURED);
    mesh_free(mesh1);

    /* Test COORD_TYPE_2D_CURVILINEAR */
    double *lon2 = malloc(4 * sizeof(double));
    double *lat2 = malloc(4 * sizeof(double));
    for (int i = 0; i < 4; i++) {
        lon2[i] = i * 10.0;
        lat2[i] = i * 5.0;
    }

    USMesh *mesh2 = mesh_create(lon2, lat2, 4, COORD_TYPE_2D_CURVILINEAR);
    ASSERT_NOT_NULL(mesh2);
    ASSERT_EQ(mesh2->coord_type, COORD_TYPE_2D_CURVILINEAR);
    mesh_free(mesh2);

    return 1;
}

/* Test mesh_free handles NULL */
TEST(mesh_free_null) {
    mesh_free(NULL);  /* Should not crash */
    return 1;
}

/* Test that mesh xyz array is normalized to unit sphere */
TEST(mesh_xyz_unit_sphere) {
    size_t n = 10;
    double *lon = malloc(n * sizeof(double));
    double *lat = malloc(n * sizeof(double));

    for (size_t i = 0; i < n; i++) {
        lon[i] = (double)(i * 36 - 180);  /* -180 to 144 */
        lat[i] = (double)(i * 18 - 90);   /* -90 to 72 */
    }

    USMesh *mesh = mesh_create(lon, lat, n, COORD_TYPE_1D_UNSTRUCTURED);
    ASSERT_NOT_NULL(mesh);

    for (size_t i = 0; i < n; i++) {
        double x = mesh->xyz[i * 3 + 0];
        double y = mesh->xyz[i * 3 + 1];
        double z = mesh->xyz[i * 3 + 2];
        double r = sqrt(x*x + y*y + z*z);

        ASSERT_NEAR(r, 1.0, EPSILON);
    }

    mesh_free(mesh);
    return 1;
}

/* Test antipodal points */
TEST(lonlat_to_cartesian_antipodal) {
    double x1, y1, z1, x2, y2, z2;

    /* Two antipodal points */
    lonlat_to_cartesian(0.0, 0.0, &x1, &y1, &z1);
    lonlat_to_cartesian(180.0, 0.0, &x2, &y2, &z2);

    /* Should be exactly opposite */
    ASSERT_NEAR(x1, -x2, EPSILON);
    ASSERT_NEAR(y1, -y2, EPSILON_LOOSE);  /* Slight numerical error at 180 degrees */
    ASSERT_NEAR(z1, -z2, EPSILON);

    return 1;
}

/* Test coordinate wrapping (lon > 180 or < -180) */
TEST(lonlat_to_cartesian_wrapping) {
    double x1, y1, z1, x2, y2, z2;

    /* lon=370 should equal lon=10 */
    lonlat_to_cartesian(10.0, 0.0, &x1, &y1, &z1);
    lonlat_to_cartesian(370.0, 0.0, &x2, &y2, &z2);

    ASSERT_NEAR(x1, x2, EPSILON);
    ASSERT_NEAR(y1, y2, EPSILON);
    ASSERT_NEAR(z1, z2, EPSILON);

    return 1;
}

/* Test batch conversion matches single conversion */
TEST(lonlat_batch_matches_single) {
    size_t n = 5;
    double lon[] = {0.0, 45.0, 90.0, -45.0, 180.0};
    double lat[] = {0.0, 30.0, -30.0, 60.0, -90.0};
    double xyz_batch[15];

    lonlat_to_cartesian_batch(lon, lat, xyz_batch, n);

    for (size_t i = 0; i < n; i++) {
        double x, y, z;
        lonlat_to_cartesian(lon[i], lat[i], &x, &y, &z);

        ASSERT_NEAR(xyz_batch[i*3 + 0], x, EPSILON);
        ASSERT_NEAR(xyz_batch[i*3 + 1], y, EPSILON);
        ASSERT_NEAR(xyz_batch[i*3 + 2], z, EPSILON);
    }

    return 1;
}

/* Test meters_to_chord is monotonic */
TEST(meters_to_chord_monotonic) {
    double prev_chord = 0.0;

    for (double m = 0; m < 1000000; m += 100000) {
        double chord = meters_to_chord(m);
        ASSERT_GE(chord, prev_chord);
        prev_chord = chord;
    }

    return 1;
}

/* Test meters_to_chord maximum (half circumference = diameter = 2) */
TEST(meters_to_chord_half_circumference) {
    /* Half circumference = pi * R_earth -> chord = 2 (diameter) */
    double half_circ = M_PI * EARTH_RADIUS_M;
    double chord = meters_to_chord(half_circ);

    ASSERT_NEAR(chord, 2.0, EPSILON);
    return 1;
}

RUN_TESTS("Mesh")
