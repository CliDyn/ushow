/*
 * range_utils.h - Pure logic utilities for range manipulation
 *
 * No X11 dependency - can be used in tests and non-GUI code.
 */

#ifndef RANGE_UTILS_H
#define RANGE_UTILS_H

#define RANGE_POPUP_OK     1
#define RANGE_POPUP_CANCEL 0

/*
 * Compute symmetric range about zero.
 * Sets new_min = -max(|cur_min|, |cur_max|), new_max = max(|cur_min|, |cur_max|).
 */
void range_compute_symmetric(float cur_min, float cur_max,
                             float *new_min, float *new_max);

/*
 * Parse a range value from a string.
 * Returns 1 on success, 0 on failure. On failure, *value is unchanged.
 */
int range_parse_value(const char *str, float *value);

#endif /* RANGE_UTILS_H */
