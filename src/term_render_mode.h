/*
 * term_render_mode.h - Terminal render mode helpers
 */

#ifndef TERM_RENDER_MODE_H
#define TERM_RENDER_MODE_H

#define TERM_RENDER_ASCII   0
#define TERM_RENDER_HALF    1
#define TERM_RENDER_BRAILLE 2
#define TERM_RENDER_COUNT   3

/*
 * Get human-readable name for a render mode.
 */
const char *term_render_mode_name(int mode);

/*
 * Parse render mode name.
 * Returns 0 on success, -1 on invalid input.
 */
int term_parse_render_mode(const char *s, int *mode_out);

/*
 * Return next render mode in cycle.
 */
int term_cycle_render_mode(int mode);

#endif /* TERM_RENDER_MODE_H */
