/*
 * healpix.c - Latitude-band connectivity generation
 *
 * Generates a triangulation for point clouds organized in latitude bands.
 * Works for HEALPix RING ordering, reduced Gaussian grids, and any other
 * grid where points can be grouped by latitude.
 *
 * Algorithm:
 * 1. Sort points by latitude (north to south)
 * 2. Group consecutive points with same latitude into bands
 * 3. Sort each band by longitude
 * 4. Create strip triangulation between adjacent bands
 */

#include "healpix.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

/* Latitude tolerance for grouping points into bands (degrees) */
#define LAT_BAND_TOL 1e-6

typedef struct {
    long   start;     /* first index in sorted array */
    long   count;     /* number of points in this band */
} LatBand;

/* Comparison: sort by latitude descending (north first), then longitude */
typedef struct {
    long   orig_idx;  /* original mesh index */
    double lat;
    double lon;
} SortedPoint;

static int cmp_lat_desc(const void *a, const void *b) {
    const SortedPoint *pa = (const SortedPoint *)a;
    const SortedPoint *pb = (const SortedPoint *)b;
    if (pa->lat > pb->lat) return -1;
    if (pa->lat < pb->lat) return 1;
    if (pa->lon < pb->lon) return -1;
    if (pa->lon > pb->lon) return 1;
    return 0;
}

static int cmp_lon_asc(const void *a, const void *b) {
    const SortedPoint *pa = (const SortedPoint *)a;
    const SortedPoint *pb = (const SortedPoint *)b;
    if (pa->lon < pb->lon) return -1;
    if (pa->lon > pb->lon) return 1;
    return 0;
}

/*
 * Triangulate strip between two adjacent latitude bands.
 * Uses advancing-front: advance whichever band has the next point
 * closer in longitude, creating triangles that connect the bands.
 */
static long triangulate_strip(
    const SortedPoint *upper, long n_upper,
    const SortedPoint *lower, long n_lower,
    int *elem_nodes, long tri_offset)
{
    long u = 0, l = 0;
    long u_adv = 0, l_adv = 0;
    long n_strip_tri = n_upper + n_lower;
    long tri_idx = tri_offset;

    for (long t = 0; t < n_strip_tri; t++) {
        int advance_upper;

        if (u_adv >= n_upper) {
            advance_upper = 0;
        } else if (l_adv >= n_lower) {
            advance_upper = 1;
        } else {
            /* Compare next longitude values (unwrapped) */
            double lon_next_u = upper[(u + 1) % n_upper].lon;
            double lon_next_l = lower[(l + 1) % n_lower].lon;

            /* Handle wraparound: if next < current, it wrapped around */
            double lon_cur_u = upper[u].lon;
            double lon_cur_l = lower[l].lon;
            if (lon_next_u < lon_cur_u - 180.0) lon_next_u += 360.0;
            if (lon_next_l < lon_cur_l - 180.0) lon_next_l += 360.0;

            /* Normalize to comparable range using fractional position */
            double frac_u = (double)(u_adv + 1) / (double)n_upper;
            double frac_l = (double)(l_adv + 1) / (double)n_lower;
            advance_upper = (frac_u <= frac_l);
        }

        long idx = tri_idx * 3;
        if (advance_upper) {
            long next_u = (u + 1) % n_upper;
            elem_nodes[idx + 0] = (int)upper[u].orig_idx;
            elem_nodes[idx + 1] = (int)lower[l].orig_idx;
            elem_nodes[idx + 2] = (int)upper[next_u].orig_idx;
            u = next_u;
            u_adv++;
        } else {
            long next_l = (l + 1) % n_lower;
            elem_nodes[idx + 0] = (int)upper[u].orig_idx;
            elem_nodes[idx + 1] = (int)lower[l].orig_idx;
            elem_nodes[idx + 2] = (int)lower[next_l].orig_idx;
            l = next_l;
            l_adv++;
        }
        tri_idx++;
    }

    return n_strip_tri;
}

int latband_generate_connectivity(USMesh *mesh) {
    if (!mesh || mesh->n_points < 3) return -1;

    long npix = (long)mesh->n_points;

    /* Step 1: Create sorted point array */
    SortedPoint *pts = malloc(npix * sizeof(SortedPoint));
    if (!pts) return -1;

    for (long i = 0; i < npix; i++) {
        pts[i].orig_idx = i;
        pts[i].lat = mesh->lat[i];
        pts[i].lon = mesh->lon[i];
    }

    /* Sort by latitude descending (north to south) */
    qsort(pts, npix, sizeof(SortedPoint), cmp_lat_desc);

    /* Step 2: Detect latitude bands */
    long max_bands = npix; /* upper bound */
    LatBand *bands = malloc(max_bands * sizeof(LatBand));
    if (!bands) { free(pts); return -1; }

    long n_bands = 0;
    long band_start = 0;
    for (long i = 1; i <= npix; i++) {
        if (i == npix || fabs(pts[i].lat - pts[band_start].lat) > LAT_BAND_TOL) {
            bands[n_bands].start = band_start;
            bands[n_bands].count = i - band_start;
            n_bands++;
            band_start = i;
        }
    }

    if (n_bands < 2) {
        fprintf(stderr, "latband: only %ld band(s) detected, need >= 2\n", n_bands);
        free(bands);
        free(pts);
        return -1;
    }

    /* Step 3: Sort each band by longitude */
    for (long b = 0; b < n_bands; b++) {
        qsort(&pts[bands[b].start], bands[b].count,
              sizeof(SortedPoint), cmp_lon_asc);
    }

    /* Step 4: Count total triangles */
    long n_total_tri = 0;
    for (long b = 0; b < n_bands - 1; b++) {
        n_total_tri += bands[b].count + bands[b + 1].count;
    }

    int *elem_nodes = malloc(n_total_tri * 3 * sizeof(int));
    if (!elem_nodes) {
        free(bands);
        free(pts);
        return -1;
    }

    /* Step 5: Generate strip triangulations */
    long tri_offset = 0;
    for (long b = 0; b < n_bands - 1; b++) {
        long n_tri = triangulate_strip(
            &pts[bands[b].start], bands[b].count,
            &pts[bands[b + 1].start], bands[b + 1].count,
            elem_nodes, tri_offset);
        tri_offset += n_tri;
    }

    free(bands);
    free(pts);

    mesh->n_elements = (size_t)tri_offset;
    mesh->n_vertices = 3;
    mesh->elem_nodes = elem_nodes;

    printf("Generated %zu triangles from %ld points (%ld latitude bands)\n",
           mesh->n_elements, npix, n_bands);

    return 0;
}
