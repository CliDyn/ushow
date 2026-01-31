/*
 * view.h - View state management
 */

#ifndef VIEW_H
#define VIEW_H

#include "ushow.defines.h"

/*
 * Create view for displaying data.
 */
USView *view_create(void);

/*
 * Set current variable.
 */
int view_set_variable(USView *view, USVar *var, USMesh *mesh, USRegrid *regrid);

/*
 * Set time index and reload data.
 */
int view_set_time(USView *view, size_t time_idx);

/*
 * Set depth index and reload data.
 */
int view_set_depth(USView *view, size_t depth_idx);

/*
 * Step time forward/backward.
 * Returns new time index, or -1 if at boundary.
 */
int view_step_time(USView *view, int delta);

/*
 * Update display: read data, regrid, and convert to pixels.
 */
int view_update(USView *view);

/*
 * Get current pixel buffer for display.
 */
unsigned char *view_get_pixels(USView *view, size_t *width, size_t *height);

/*
 * Free view and all associated memory.
 */
void view_free(USView *view);

#endif /* VIEW_H */
