/*
 * colormaps.c - Colormap management
 */

#include "colormaps.h"
#include "cmocean_colormaps.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define N_COLORS 256

/* Viridis colormap definition (approximation) */
static void create_viridis_colormap(USColormap *cmap) {
    strcpy(cmap->name, "viridis");
    cmap->n_colors = N_COLORS;
    cmap->colors = malloc(N_COLORS * sizeof(USColor));

    /* Viridis color points (simplified) */
    for (int i = 0; i < N_COLORS; i++) {
        float t = (float)i / (N_COLORS - 1);

        float r = 0.267004f + t * (0.993248f - 0.267004f);
        float g = 0.004874f + t * (0.906157f - 0.004874f);
        float b = 0.329415f + t * (0.143936f - 0.329415f);

        /* Better approximation using cubic interpolation */
        r = 0.267004f + t * (0.282327f + t * (-0.605696f + t * 1.049613f));
        g = 0.004874f + t * (1.421801f + t * (-0.759744f + t * 0.239226f));
        b = 0.329415f + t * (0.266658f + t * (0.123926f + t * (-0.576063f)));

        /* Clamp */
        if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
        if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
        if (b < 0.0f) b = 0.0f; if (b > 1.0f) b = 1.0f;

        cmap->colors[i].r = (unsigned char)(r * 255.0f);
        cmap->colors[i].g = (unsigned char)(g * 255.0f);
        cmap->colors[i].b = (unsigned char)(b * 255.0f);
    }
}

static void create_colormap_from_rgb256(USColormap *cmap, const char *name,
                                        const unsigned char (*data)[3]) {
    strcpy(cmap->name, name);
    cmap->n_colors = N_COLORS;
    cmap->colors = malloc(N_COLORS * sizeof(USColor));

    for (int i = 0; i < N_COLORS; i++) {
        cmap->colors[i].r = data[i][0];
        cmap->colors[i].g = data[i][1];
        cmap->colors[i].b = data[i][2];
    }
}

/* Grayscale colormap */
static void create_grayscale_colormap(USColormap *cmap) {
    strcpy(cmap->name, "grayscale");
    cmap->n_colors = N_COLORS;
    cmap->colors = malloc(N_COLORS * sizeof(USColor));

    for (int i = 0; i < N_COLORS; i++) {
        cmap->colors[i].r = (unsigned char)i;
        cmap->colors[i].g = (unsigned char)i;
        cmap->colors[i].b = (unsigned char)i;
    }
}

/* Hot colormap */
static void create_hot_colormap(USColormap *cmap) {
    strcpy(cmap->name, "hot");
    cmap->n_colors = N_COLORS;
    cmap->colors = malloc(N_COLORS * sizeof(USColor));

    for (int i = 0; i < N_COLORS; i++) {
        float t = (float)i / (N_COLORS - 1);
        float r, g, b;

        if (t < 0.33333f) {
            r = t * 3.0f;
            g = 0.0f;
            b = 0.0f;
        } else if (t < 0.66667f) {
            r = 1.0f;
            g = (t - 0.33333f) * 3.0f;
            b = 0.0f;
        } else {
            r = 1.0f;
            g = 1.0f;
            b = (t - 0.66667f) * 3.0f;
        }

        cmap->colors[i].r = (unsigned char)(r * 255.0f);
        cmap->colors[i].g = (unsigned char)(g * 255.0f);
        cmap->colors[i].b = (unsigned char)(b * 255.0f);
    }
}

/* Global colormap storage */
#define MAX_COLORMAPS 32
static USColormap colormaps[MAX_COLORMAPS];
static int n_colormaps = 0;
static int current_colormap = 0;

void colormaps_init(void) {
    /* Create built-in colormaps */
    create_viridis_colormap(&colormaps[n_colormaps++]);
    create_hot_colormap(&colormaps[n_colormaps++]);
    create_grayscale_colormap(&colormaps[n_colormaps++]);

    for (int i = 0; i < CMOCEAN_COLORMAP_COUNT; i++) {
        create_colormap_from_rgb256(&colormaps[n_colormaps++],
                                    cmocean_colormaps[i].name,
                                    cmocean_colormaps[i].data);
    }

    /* Default to viridis if available */
    for (int i = 0; i < n_colormaps; i++) {
        if (strcmp(colormaps[i].name, "viridis") == 0) {
            current_colormap = i;
            break;
        }
    }
}

USColormap *colormap_get_current(void) {
    if (n_colormaps == 0) return NULL;
    return &colormaps[current_colormap];
}

void colormap_next(void) {
    if (n_colormaps > 0) {
        current_colormap = (current_colormap + 1) % n_colormaps;
    }
}

void colormap_prev(void) {
    if (n_colormaps > 0) {
        current_colormap = (current_colormap + n_colormaps - 1) % n_colormaps;
    }
}

USColormap *colormap_get_by_name(const char *name) {
    for (int i = 0; i < n_colormaps; i++) {
        if (strcmp(colormaps[i].name, name) == 0) {
            return &colormaps[i];
        }
    }
    return NULL;
}

int colormap_count(void) {
    return n_colormaps;
}

void colormap_map_value(const USColormap *cmap, float value,
                        unsigned char *r, unsigned char *g, unsigned char *b) {
    if (!cmap || !cmap->colors) {
        *r = *g = *b = 0;
        return;
    }

    /* Clamp to [0, 1] */
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    int idx = (int)(value * (cmap->n_colors - 1));
    if (idx < 0) idx = 0;
    if (idx >= cmap->n_colors) idx = cmap->n_colors - 1;

    *r = cmap->colors[idx].r;
    *g = cmap->colors[idx].g;
    *b = cmap->colors[idx].b;
}

void colormap_apply(const USColormap *cmap, const float *data,
                    size_t nx, size_t ny,
                    float min_val, float max_val, float fill_value,
                    unsigned char *pixels) {
    if (!cmap || !data || !pixels) return;

    float range = max_val - min_val;
    if (range <= 0.0f) range = 1.0f;

    /* Flip y-axis: data row 0 is south (-90), screen row 0 is top (north) */
    for (size_t y = 0; y < ny; y++) {
        size_t src_row = ny - 1 - y;  /* Flip: top of screen = north = last data row */
        for (size_t x = 0; x < nx; x++) {
            size_t src_idx = src_row * nx + x;
            size_t dst_idx = y * nx + x;
            float v = data[src_idx];

            /* Check for fill value or NaN */
            if (fabsf(v) > 1e10f || v != v ||
                fabsf(v - fill_value) < 1e-6f * fabsf(fill_value)) {
                /* Missing data: draw as dark gray */
                pixels[dst_idx * 3 + 0] = 30;
                pixels[dst_idx * 3 + 1] = 30;
                pixels[dst_idx * 3 + 2] = 30;
            } else {
                /* Normalize to [0, 1] */
                float t = (v - min_val) / range;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;

                colormap_map_value(cmap, t,
                                   &pixels[dst_idx * 3 + 0],
                                   &pixels[dst_idx * 3 + 1],
                                   &pixels[dst_idx * 3 + 2]);
            }
        }
    }
}

void colormap_apply_scaled(const USColormap *cmap, const float *data,
                           size_t data_nx, size_t data_ny,
                           float min_val, float max_val, float fill_value,
                           unsigned char *pixels, int scale) {
    if (!cmap || !data || !pixels || scale < 1) return;

    float range = max_val - min_val;
    if (range <= 0.0f) range = 1.0f;

    size_t display_nx = data_nx * scale;

    /* Flip y-axis and apply scaling */
    for (size_t data_y = 0; data_y < data_ny; data_y++) {
        size_t src_row = data_ny - 1 - data_y;  /* Flip: screen top = north */

        for (size_t data_x = 0; data_x < data_nx; data_x++) {
            size_t src_idx = src_row * data_nx + data_x;
            float v = data[src_idx];

            unsigned char r, g, b;

            /* Check for fill value or NaN */
            if (fabsf(v) > 1e10f || v != v ||
                fabsf(v - fill_value) < 1e-6f * fabsf(fill_value)) {
                /* Missing data: draw as dark gray */
                r = g = b = 30;
            } else {
                /* Normalize to [0, 1] */
                float t = (v - min_val) / range;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                colormap_map_value(cmap, t, &r, &g, &b);
            }

            /* Replicate pixel to scale x scale block */
            for (int sy = 0; sy < scale; sy++) {
                size_t disp_y = data_y * scale + sy;
                for (int sx = 0; sx < scale; sx++) {
                    size_t disp_x = data_x * scale + sx;
                    size_t dst_idx = disp_y * display_nx + disp_x;
                    pixels[dst_idx * 3 + 0] = r;
                    pixels[dst_idx * 3 + 1] = g;
                    pixels[dst_idx * 3 + 2] = b;
                }
            }
        }
    }
}

void colormaps_cleanup(void) {
    for (int i = 0; i < n_colormaps; i++) {
        free(colormaps[i].colors);
        colormaps[i].colors = NULL;
    }
    n_colormaps = 0;
}
