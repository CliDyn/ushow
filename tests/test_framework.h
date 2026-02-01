/*
 * test_framework.h - Simple unit testing framework for ushow
 *
 * A minimal testing framework for C that provides:
 * - Test case registration and execution
 * - Assertion macros
 * - Summary reporting
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Test counters */
static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;
static int assertions_count = 0;
static int assertions_failed = 0;

/* Current test name for error reporting */
static const char *current_test_name = NULL;

/* Colors for terminal output */
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_RESET   "\x1b[0m"

/* Assertion macros */
#define ASSERT_TRUE(cond) do { \
    assertions_count++; \
    if (!(cond)) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_TRUE(%s)\n", \
                __FILE__, __LINE__, #cond); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    assertions_count++; \
    if (cond) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_FALSE(%s)\n", \
                __FILE__, __LINE__, #cond); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    assertions_count++; \
    if ((a) != (b)) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_EQ(%s, %s)\n", \
                __FILE__, __LINE__, #a, #b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    assertions_count++; \
    long _a = (long)(a); \
    long _b = (long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_EQ_INT(%s=%ld, %s=%ld)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ_SIZET(a, b) do { \
    assertions_count++; \
    size_t _a = (size_t)(a); \
    size_t _b = (size_t)(b); \
    if (_a != _b) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_EQ_SIZET(%s=%zu, %s=%zu)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, epsilon) do { \
    assertions_count++; \
    double _a = (double)(a); \
    double _b = (double)(b); \
    double _eps = (double)(epsilon); \
    if (fabs(_a - _b) > _eps) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_NEAR(%s=%g, %s=%g, eps=%g)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b, _eps); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    assertions_count++; \
    if ((ptr) != NULL) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_NULL(%s)\n", \
                __FILE__, __LINE__, #ptr); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    assertions_count++; \
    if ((ptr) == NULL) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_NOT_NULL(%s)\n", \
                __FILE__, __LINE__, #ptr); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    assertions_count++; \
    const char *_a = (a); \
    const char *_b = (b); \
    if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_STR_EQ(%s=\"%s\", %s=\"%s\")\n", \
                __FILE__, __LINE__, #a, _a ? _a : "(null)", #b, _b ? _b : "(null)"); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_GT(a, b) do { \
    assertions_count++; \
    double _a = (double)(a); \
    double _b = (double)(b); \
    if (!(_a > _b)) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_GT(%s=%g > %s=%g)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    assertions_count++; \
    double _a = (double)(a); \
    double _b = (double)(b); \
    if (!(_a >= _b)) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_GE(%s=%g >= %s=%g)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_LT(a, b) do { \
    assertions_count++; \
    double _a = (double)(a); \
    double _b = (double)(b); \
    if (!(_a < _b)) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_LT(%s=%g < %s=%g)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    assertions_count++; \
    double _a = (double)(a); \
    double _b = (double)(b); \
    if (!(_a <= _b)) { \
        fprintf(stderr, "    " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: ASSERT_LE(%s=%g <= %s=%g)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        assertions_failed++; \
        return 0; \
    } \
} while(0)

/* Test definition and execution */
typedef int (*TestFunc)(void);

typedef struct {
    const char *test_name;
    TestFunc test_func;
} TestCase;

#define MAX_TESTS 256
static TestCase registered_tests[MAX_TESTS];
static int num_registered_tests = 0;

#define TEST(name) \
    static int test_##name(void); \
    __attribute__((constructor)) static void register_##name(void) { \
        if (num_registered_tests < MAX_TESTS) { \
            registered_tests[num_registered_tests].test_name = #name; \
            registered_tests[num_registered_tests].test_func = test_##name; \
            num_registered_tests++; \
        } \
    } \
    static int test_##name(void)

/* Run a single test */
static int run_test(const char *name, TestFunc func) {
    current_test_name = name;
    test_count++;

    printf("  Running: %s ... ", name);
    fflush(stdout);

    int result = func();

    if (result) {
        printf(COLOR_GREEN "PASS" COLOR_RESET "\n");
        test_passed++;
        return 1;
    } else {
        test_failed++;
        return 0;
    }
}

/* Run all registered tests */
static int run_all_tests(const char *suite_name) {
    printf("\n=== Running test suite: %s ===\n\n", suite_name);

    for (int i = 0; i < num_registered_tests; i++) {
        run_test(registered_tests[i].test_name, registered_tests[i].test_func);
    }

    printf("\n=== Test Summary ===\n");
    printf("Tests:      %d total, " COLOR_GREEN "%d passed" COLOR_RESET ", "
           COLOR_RED "%d failed" COLOR_RESET "\n",
           test_count, test_passed, test_failed);
    printf("Assertions: %d total, %d failed\n",
           assertions_count, assertions_failed);
    printf("Result:     %s\n\n",
           test_failed == 0 ? COLOR_GREEN "SUCCESS" COLOR_RESET : COLOR_RED "FAILURE" COLOR_RESET);

    return test_failed == 0 ? 0 : 1;
}

/* Main macro for test executables */
#define RUN_TESTS(suite_name) \
    int main(void) { \
        return run_all_tests(suite_name); \
    }

#endif /* TEST_FRAMEWORK_H */
