/*
 * common.c - Shared utilities for ushow and uterm
 */

#include "common.h"
#include "kdtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define DEFAULT_THREADS 4

void apply_thread_settings(int user_threads) {
    if (user_threads > 0) {
        kdtree_set_max_threads(user_threads);
#ifdef _OPENMP
        omp_set_num_threads(user_threads);
#endif
    } else if (!getenv("OMP_NUM_THREADS")) {
#ifdef _OPENMP
        omp_set_num_threads(DEFAULT_THREADS);
#endif
    }
}

int parse_time_units(const char *units, double *unit_seconds,
                     int *y, int *mo, int *d, int *h, int *mi, double *sec) {
    if (!units || !unit_seconds || !y || !mo || !d || !h || !mi || !sec) return 0;

    const char *since = strstr(units, "since");
    if (!since) return 0;

    char unit_buf[32] = {0};
    if (sscanf(units, "%31s", unit_buf) != 1) return 0;

    /* Normalize unit token to lower case */
    for (char *p = unit_buf; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    }

    if (strcmp(unit_buf, "seconds") == 0 || strcmp(unit_buf, "second") == 0 ||
        strcmp(unit_buf, "secs") == 0 || strcmp(unit_buf, "sec") == 0 || strcmp(unit_buf, "s") == 0) {
        *unit_seconds = 1.0;
    } else if (strcmp(unit_buf, "minutes") == 0 || strcmp(unit_buf, "minute") == 0 ||
               strcmp(unit_buf, "mins") == 0 || strcmp(unit_buf, "min") == 0) {
        *unit_seconds = 60.0;
    } else if (strcmp(unit_buf, "hours") == 0 || strcmp(unit_buf, "hour") == 0 ||
               strcmp(unit_buf, "hrs") == 0 || strcmp(unit_buf, "hr") == 0) {
        *unit_seconds = 3600.0;
    } else if (strcmp(unit_buf, "days") == 0 || strcmp(unit_buf, "day") == 0) {
        *unit_seconds = 86400.0;
    } else {
        return 0;
    }

    /* Parse origin date/time after "since" */
    const char *p = since + 5;
    while (*p == ' ') p++;
    int n = sscanf(p, "%d-%d-%d %d:%d:%lf", y, mo, d, h, mi, sec);
    if (n < 3) return 0;
    if (n == 3) { *h = 0; *mi = 0; *sec = 0.0; }
    return 1;
}

int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)(era * 146097 + (int)doe - 719468);
}

void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y_tmp = (int)(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d_tmp = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m_tmp = mp + (mp < 10 ? 3 : -9);
    y_tmp += (m_tmp <= 2);

    *y = y_tmp;
    *m = m_tmp;
    *d = d_tmp;
}

int format_time_from_units(char *out, size_t outlen, double value, const char *units) {
    if (!out || outlen == 0) return 0;

    double unit_seconds = 0.0;
    int y, mo, d, h, mi;
    double sec;
    if (!parse_time_units(units, &unit_seconds, &y, &mo, &d, &h, &mi, &sec)) {
        return 0;
    }

    int64_t days = days_from_civil(y, (unsigned)mo, (unsigned)d);
    double total_sec = (double)days * 86400.0 + (double)h * 3600.0 + (double)mi * 60.0 + sec;
    total_sec += value * unit_seconds;

    int64_t out_days = (int64_t)(total_sec / 86400.0);
    double rem = total_sec - (double)out_days * 86400.0;
    if (rem < 0) {
        rem += 86400.0;
        out_days -= 1;
    }

    int out_y;
    unsigned out_m, out_d;
    civil_from_days(out_days, &out_y, &out_m, &out_d);

    int out_h = (int)(rem / 3600.0);
    rem -= out_h * 3600.0;
    int out_mi = (int)(rem / 60.0);
    rem -= out_mi * 60.0;
    int out_s = (int)(rem + 0.5);

    if (out_h == 0 && out_mi == 0 && out_s == 0) {
        snprintf(out, outlen, "%04d-%02u-%02u", out_y, out_m, out_d);
    } else {
        snprintf(out, outlen, "%04d-%02u-%02u %02d:%02d:%02d",
                 out_y, out_m, out_d, out_h, out_mi, out_s);
    }
    return 1;
}
