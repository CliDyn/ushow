/*
 * x_interface.h - X11/Xaw display interface
 */

#ifndef X_INTERFACE_H
#define X_INTERFACE_H

#include "../ushow.defines.h"

/*
 * Initialize X11 display.
 * Returns 0 on success, -1 on failure.
 */
int x_init(int *argc, char **argv);

/*
 * Set callbacks for user interactions.
 */
typedef void (*VarSelectCallback)(int var_index);
typedef void (*TimeChangeCallback)(size_t time_idx);
typedef void (*DepthChangeCallback)(size_t depth_idx);
typedef void (*AnimationCallback)(int direction);  /* -1: back, 0: pause, 1: forward */
typedef void (*ColormapCallback)(void);

void x_set_var_callback(VarSelectCallback cb);
void x_set_time_callback(TimeChangeCallback cb);
void x_set_depth_callback(DepthChangeCallback cb);
void x_set_animation_callback(AnimationCallback cb);
void x_set_colormap_callback(ColormapCallback cb);
void x_set_mouse_callback(void (*cb)(int x, int y));

/*
 * Set up variable selector buttons.
 * var_names: array of variable names
 * n_vars: number of variables
 */
void x_setup_var_selector(const char **var_names, int n_vars);

/*
 * Update display image from RGB pixel data.
 * pixels: RGB data [height * width * 3]
 */
void x_update_image(const unsigned char *pixels, size_t width, size_t height);

/*
 * Update time slider and label.
 */
void x_update_time(size_t time_idx, size_t n_times);

/*
 * Update depth slider and label.
 */
void x_update_depth(size_t depth_idx, size_t n_depths);

/*
 * Update info labels.
 */
void x_update_var_name(const char *name);
void x_update_range_label(float min_val, float max_val);
void x_update_colormap_label(const char *name);
void x_update_value_label(double lon, double lat, float value);

/*
 * Set animation timer.
 * delay_ms: delay between frames in milliseconds
 * callback: function to call on timer
 */
void x_set_timer(int delay_ms, void (*callback)(void));

/*
 * Clear animation timer.
 */
void x_clear_timer(void);

/*
 * Enter X11 event loop.
 */
void x_main_loop(void);

/*
 * Cleanup X11 resources.
 */
void x_cleanup(void);

#endif /* X_INTERFACE_H */
