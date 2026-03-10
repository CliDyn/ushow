/*
 * common.h - Shared utilities for ushow and uterm
 */

#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>

/*
 * Apply thread settings for kdtree and OpenMP.
 *   user_threads > 0: use that value for both kdtree and omp_set_num_threads.
 *   user_threads <= 0: leave kdtree default; if OMP_NUM_THREADS is unset,
 *                      set omp_set_num_threads to 4.
 */
void apply_thread_settings(int user_threads);

/*
 * Parse a CF-convention "units since origin" string.
 * Returns 1 on success, 0 on failure.
 *   unit_seconds: conversion factor (e.g. 86400 for days)
 *   y,mo,d,h,mi,sec: parsed origin date/time components
 */
int parse_time_units(const char *units, double *unit_seconds,
                     int *y, int *mo, int *d, int *h, int *mi, double *sec);

/*
 * Calendar arithmetic helpers (proleptic Gregorian).
 */
int64_t days_from_civil(int y, unsigned m, unsigned d);
void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d);

/*
 * Format a CF time value as "YYYY-MM-DD HH:MM:SS" (or "YYYY-MM-DD" when
 * the time-of-day is exactly midnight).
 * Returns 1 on success, 0 on failure.
 */
int format_time_from_units(char *out, size_t outlen, double value, const char *units);

#endif /* COMMON_H */
