#include "window_title.h"

#include <stdio.h>
#include <string.h>

void format_window_title(const char *path, int total_files,
                         char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return;
    out[0] = '\0';

    const char *display = (path && path[0]) ? path : "(unknown)";
    const char *slash = strrchr(display, '/');
    if (slash) display = slash + 1;

    /* A path ending in '/' (e.g. zarr store) leaves display pointing at
     * the empty string; fall back to the placeholder so the title is not
     * just " (N files)". */
    if (display[0] == '\0') display = "(unknown)";

    if (total_files > 1) {
        snprintf(out, out_sz, "%s (%d files)", display, total_files);
    } else {
        snprintf(out, out_sz, "%s", display);
    }
}
