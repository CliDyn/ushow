#ifndef USHOW_WINDOW_TITLE_H
#define USHOW_WINDOW_TITLE_H

#include <stddef.h>

/* Format the window-title string for a data source.
 *
 * Writes the basename of `path` (the substring after the last '/'), and
 * appends " (N files)" when `total_files > 1`. If `path` is NULL or empty,
 * the placeholder "(unknown)" is used in its place.
 *
 * Output is always NUL-terminated when `out_sz > 0`.
 *
 * The NULL-safe behaviour is load-bearing: callers in ushow.c snapshot
 * argv-aliased filename pointers before x_init() because XtVaAppInitialize
 * prefix-matches "-i" as "-iconic" and shifts argv. If a caller skips the
 * snapshot and passes the now-stale slot, this function must not segfault.
 */
void format_window_title(const char *path, int total_files,
                         char *out, size_t out_sz);

#endif
