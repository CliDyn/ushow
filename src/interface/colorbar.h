/*
 * colorbar.h - Colorbar rendering
 */

#ifndef COLORBAR_H
#define COLORBAR_H

#include <stddef.h>

/* Default colorbar height */
#define COLORBAR_HEIGHT 20

/*
 * Initialize colorbar with specified width and height.
 */
void colorbar_init(size_t width, size_t height);

/*
 * Render colorbar using current colormap.
 */
void colorbar_render(void);

/*
 * Get rendered colorbar pixels (RGB format).
 * Returns pointer to pixel buffer, sets width/height if not NULL.
 */
unsigned char *colorbar_get_pixels(size_t *width, size_t *height);

/*
 * Free colorbar resources.
 */
void colorbar_cleanup(void);

#endif /* COLORBAR_H */
