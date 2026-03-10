/*
 * common.h - Shared utilities for ushow and uterm
 */

#ifndef COMMON_H
#define COMMON_H

/*
 * Apply thread settings for kdtree and OpenMP.
 *   user_threads > 0: use that value for both kdtree and omp_set_num_threads.
 *   user_threads <= 0: leave kdtree default; if OMP_NUM_THREADS is unset,
 *                      set omp_set_num_threads to 4.
 */
void apply_thread_settings(int user_threads);

#endif /* COMMON_H */
