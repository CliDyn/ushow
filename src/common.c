/*
 * common.c - Shared utilities for ushow and uterm
 */

#include "common.h"
#include "kdtree.h"

#include <stdlib.h>
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
