/*
 * test_window_title.c — tests for src/window_title.c and the underlying
 * argv-aliasing hazard that motivated extracting it.
 *
 * Background — the bug these tests guard against
 * ---------------------------------------------
 * `ushow data.nc -i 20000` segfaulted inside main() at the strrchr that
 * builds the window title. Root cause: GNU getopt permutes argv so
 * non-option args sit at the end, leaving argv = ["ushow","-i","20000",
 * "data.nc",NULL] with optind=3. main() then stored
 *     data_filenames = (const char **)&argv[3];
 * as a *pointer into argv* and passed argv to x_init(), which calls
 * XtVaAppInitialize(). Xt prefix-matches the standard "-iconic" option
 * — "-i" is a valid prefix — and removes argv[1], shifting the rest
 * down. After Xt: argv = ["ushow","20000","data.nc",NULL] and
 * data_filenames[0] aliases argv[3], which is now NULL.
 * strrchr(NULL,'/') then crashed.
 *
 * The fix snapshots the filename pointer before x_init() and passes the
 * snapshot to a small helper (format_window_title). These tests cover
 * both the helper's behaviour and the argv-mutation invariant, so a
 * future "simplify"-style edit that re-introduces the aliased read is
 * caught.
 */

#include "test_framework.h"
#include "../src/window_title.h"

#include <string.h>

TEST(plain_filename) {
    char buf[64];
    format_window_title("tas.nc", 1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "tas.nc");
    return 1;
}

TEST(absolute_path_uses_basename) {
    char buf[64];
    format_window_title("/work/ab0995/runs/step8000/tas.nc", 1,
                        buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "tas.nc");
    return 1;
}

TEST(relative_path_uses_basename) {
    char buf[64];
    format_window_title("runs/step8000/tas.nc", 1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "tas.nc");
    return 1;
}

TEST(multi_file_suffix) {
    char buf[64];
    format_window_title("/data/tas.nc", 12, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "tas.nc (12 files)");
    return 1;
}

TEST(zero_or_negative_total_files_no_suffix) {
    char buf[64];
    format_window_title("tas.nc", 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "tas.nc");
    format_window_title("tas.nc", -5, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "tas.nc");
    return 1;
}

TEST(trailing_slash_falls_back_to_placeholder) {
    /* zarr stores end with '/'. basename would be empty — avoid an
     * uninformative " (3 files)" title. */
    char buf[64];
    format_window_title("/data/store.zarr/", 3, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(unknown) (3 files)");
    return 1;
}

TEST(null_path_does_not_crash) {
    /* This is the symptom of the original bug: NULL filename pointer
     * after Xt mutated argv. Must produce a safe placeholder, not crash. */
    char buf[64];
    format_window_title(NULL, 1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(unknown)");
    return 1;
}

TEST(empty_path_does_not_crash) {
    char buf[64];
    format_window_title("", 1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(unknown)");
    return 1;
}

TEST(small_buffer_is_truncated_and_terminated) {
    char buf[8];
    format_window_title("/very/long/path/to/dataset.nc", 1,
                        buf, sizeof(buf));
    /* snprintf guarantees NUL termination within out_sz. */
    ASSERT_EQ_INT(buf[sizeof(buf) - 1], 0);
    return 1;
}

TEST(zero_buffer_size_is_safe) {
    char buf[1] = { 'X' };  /* sentinel */
    format_window_title("tas.nc", 1, buf, 0);
    ASSERT_EQ_INT(buf[0], 'X');  /* untouched */
    return 1;
}

TEST(null_buffer_is_safe) {
    format_window_title("tas.nc", 1, NULL, 16);
    return 1;
}

/*
 * Regression test for the argv-mutation hazard. Reproduces the exact
 * argv layout that triggered the crash, simulates Xt's removal of the
 * "-i" slot, and verifies:
 *
 *   1. an aliased pointer into argv becomes NULL after the shift, and
 *   2. a snapshot taken before the shift is unaffected, and
 *   3. the title helper survives being handed the now-NULL aliased slot
 *      (so even a partial revert of the fix can't reintroduce a crash).
 *
 * Item 1 is the failure mode. Item 2 is the fix. Item 3 is the
 * defence-in-depth in format_window_title.
 */
TEST(snapshot_pattern_survives_argv_shift) {
    char arg0[] = "ushow";
    char arg1[] = "-i";
    char arg2[] = "20000";
    char arg3[] = "tas.nc";
    char *argv[] = { arg0, arg1, arg2, arg3, NULL };

    int first_data_arg = 3;  /* what getopt leaves in optind */
    const char **data_filenames = (const char **)&argv[first_data_arg];

    /* The pattern main() now uses: snapshot before x_init() runs. */
    const char *snapshot = data_filenames[0];
    ASSERT_STR_EQ(snapshot, "tas.nc");

    /* Simulate XtVaAppInitialize removing argv[1] ("-i" matched as a
     * prefix of "-iconic") and NULL-terminating the shortened list. */
    argv[1] = argv[2];
    argv[2] = argv[3];
    argv[3] = NULL;

    /* The aliased slot is now NULL — this is what crashed strrchr. */
    ASSERT_NULL(data_filenames[0]);

    /* The snapshot is the property the fix relies on. */
    ASSERT_NOT_NULL(snapshot);
    ASSERT_STR_EQ(snapshot, "tas.nc");

    /* Title formatter must produce the right title from the snapshot... */
    char title[128];
    format_window_title(snapshot, 1, title, sizeof(title));
    ASSERT_STR_EQ(title, "tas.nc");

    /* ...and must not crash if a future regression re-aliases. */
    format_window_title(data_filenames[0], 1, title, sizeof(title));
    ASSERT_STR_EQ(title, "(unknown)");
    return 1;
}

RUN_TESTS("window_title")
