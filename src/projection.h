/*
 * projection.h - Map projections for target grid generation
 */

#ifndef PROJECTION_H
#define PROJECTION_H

#include "us_types.h"

/*
 * Initialize target config with global equirectangular defaults.
 */
void target_config_init_default(USTargetConfig *cfg);

/*
 * LAEA forward projection (lon/lat degrees -> projected x/y meters).
 * pole: +1 for north pole, -1 for south pole
 */
void laea_forward(double lon_deg, double lat_deg, int pole, double R,
                  double *x, double *y);

/*
 * LAEA inverse projection (projected x/y meters -> lon/lat degrees).
 * Returns 0 on success, -1 if point is outside projection disk.
 */
int laea_inverse(double x, double y, int pole, double R,
                 double *lon_deg, double *lat_deg);

/*
 * Compute projected half-extent from cutoff latitude.
 * cutoff_lat: degrees from equator (e.g. 60 means 60°N for north pole)
 * Returns the projected radius in meters for the cutoff circle.
 */
double laea_extent_from_cutoff(double cutoff_lat, int pole, double R);

#endif /* PROJECTION_H */
