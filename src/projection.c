/*
 * projection.c - Lambert Azimuthal Equal-Area (LAEA) projection
 */

#include "projection.h"
#include <math.h>

void target_config_init_default(USTargetConfig *cfg) {
    cfg->projection = PROJ_EQUIRECTANGULAR;
    cfg->lon_min = -180.0;
    cfg->lon_max = 180.0;
    cfg->lat_min = -90.0;
    cfg->lat_max = 90.0;
    cfg->cutoff_lat = 60.0;
}

/*
 * LAEA forward projection centered on a pole.
 *
 * North pole formulas:
 *   k' = sqrt(2 / (1 + sin(phi)))
 *   x  = R * k' * cos(phi) * sin(lambda)
 *   y  = -R * k' * cos(phi) * cos(lambda)
 *
 * South pole: negate lat, negate output y.
 */
void laea_forward(double lon_deg, double lat_deg, int pole, double R,
                  double *x, double *y) {
    double lat = lat_deg * DEG2RAD;
    double lon = lon_deg * DEG2RAD;

    if (pole < 0) lat = -lat;

    double sin_lat = sin(lat);
    double cos_lat = cos(lat);
    double denom = 1.0 + sin_lat;
    if (denom < 1e-10) denom = 1e-10;  /* antipodal guard */
    double kp = sqrt(2.0 / denom);

    *x = R * kp * cos_lat * sin(lon);
    double yval = -R * kp * cos_lat * cos(lon);

    if (pole < 0) yval = -yval;
    *y = yval;
}

/*
 * LAEA inverse projection centered on a pole.
 * Returns 0 on success, -1 if rho > 2R (outside projection disk).
 */
int laea_inverse(double x, double y, int pole, double R,
                 double *lon_deg, double *lat_deg) {
    if (pole < 0) y = -y;

    double rho = sqrt(x * x + y * y);
    if (rho > 2.0 * R) return -1;
    if (rho < 1e-10) {
        *lon_deg = 0.0;
        *lat_deg = (pole > 0) ? 90.0 : -90.0;
        return 0;
    }

    double c = 2.0 * asin(rho / (2.0 * R));
    double lat = asin(cos(c));  /* North pole: phi0 = 90, so cos(c)*sin(phi0) + ... simplifies */
    double lon = atan2(x, -y);

    double lat_out = lat * RAD2DEG;
    if (pole < 0) lat_out = -lat_out;

    *lat_deg = lat_out;
    *lon_deg = lon * RAD2DEG;
    return 0;
}

/*
 * Compute projected half-extent from cutoff latitude.
 * For north pole with cutoff_lat=60: the boundary circle is at 60°N.
 * We project that latitude and return the radius.
 */
double laea_extent_from_cutoff(double cutoff_lat, int pole, double R) {
    double px, py;
    /* Project cutoff latitude at lon=0 to get the radius */
    double lat = (pole > 0) ? cutoff_lat : -cutoff_lat;
    laea_forward(0.0, lat, pole, R, &px, &py);
    return sqrt(px * px + py * py);
}
