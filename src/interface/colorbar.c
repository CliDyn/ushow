/*
 * colorbar.c - Colorbar rendering (horizontal)
 */

#include "colorbar.h"
#include "../colormaps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static unsigned char *cbar_pixels = NULL;
static size_t cbar_width = 0;
static size_t cbar_height = COLORBAR_HEIGHT;  /* Default height */

void colorbar_init(size_t width, size_t height) {
    /* Free existing buffer if reallocating */
    if (cbar_pixels && (cbar_width != width || cbar_height != height)) {
        free(cbar_pixels);
        cbar_pixels = NULL;
    }

    cbar_width = width;
    cbar_height = height;
    if (!cbar_pixels && cbar_width > 0) {
        cbar_pixels = malloc(cbar_width * cbar_height * 3);
    }
}

void colorbar_render(void) {
    USColormap *cmap = colormap_get_current();
    if (!cmap || !cbar_pixels || cbar_width == 0) return;

    /* Render horizontal colorbar from left (min) to right (max) */
    for (size_t x = 0; x < cbar_width; x++) {
        /* Map x to normalized value: left=0.0, right=1.0 */
        float t = (float)x / (cbar_width - 1);

        unsigned char r, g, b;
        colormap_map_value(cmap, t, &r, &g, &b);

        /* Fill all rows with the same color */
        for (size_t y = 0; y < cbar_height; y++) {
            size_t idx = (y * cbar_width + x) * 3;
            cbar_pixels[idx + 0] = r;
            cbar_pixels[idx + 1] = g;
            cbar_pixels[idx + 2] = b;
        }
    }
}

unsigned char *colorbar_get_pixels(size_t *width, size_t *height) {
    if (width) *width = cbar_width;
    if (height) *height = cbar_height;
    return cbar_pixels;
}

void colorbar_cleanup(void) {
    free(cbar_pixels);
    cbar_pixels = NULL;
    cbar_width = 0;
}
