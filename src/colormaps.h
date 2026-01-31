/*
 * colormaps.h - Colormap management
 */

#ifndef COLORMAPS_H
#define COLORMAPS_H

#include "ushow.defines.h"

/*
 * Initialize colormaps (load built-in colormaps).
 */
void colormaps_init(void);

/*
 * Get current colormap.
 */
USColormap *colormap_get_current(void);

/*
 * Cycle to next colormap.
 */
void colormap_next(void);

/*
 * Get colormap by name.
 */
USColormap *colormap_get_by_name(const char *name);

/*
 * Get number of available colormaps.
 */
int colormap_count(void);

/*
 * Map a normalized value [0, 1] to RGB.
 */
void colormap_map_value(const USColormap *cmap, float value,
                        unsigned char *r, unsigned char *g, unsigned char *b);

/*
 * Convert data array to RGB pixels.
 * data: input data [ny * nx]
 * min_val, max_val: data range for scaling
 * fill_value: value to treat as missing (will be drawn as black)
 * pixels: output RGB pixels [ny * nx * 3]
 */
void colormap_apply(const USColormap *cmap, const float *data,
                    size_t nx, size_t ny,
                    float min_val, float max_val, float fill_value,
                    unsigned char *pixels);

/*
 * Free colormap resources.
 */
void colormaps_cleanup(void);

#endif /* COLORMAPS_H */
