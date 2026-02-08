/*
 * uterm.c - Terminal viewer for unstructured data
 *
 * Lightweight interactive viewer that reuses the ushow data pipeline
 * (netCDF/Zarr -> mesh -> regrid -> view) and renders frames as text.
 */

#include "ushow.defines.h"
#include "mesh.h"
#include "regrid.h"
#include "file_netcdf.h"
#ifdef HAVE_ZARR
#include "file_zarr.h"
#endif
#ifdef HAVE_GRIB
#include "file_grib.h"
#endif
#include "colormaps.h"
#include "view.h"
#include "term_render_mode.h"

#include <errno.h>
#include <getopt.h>
#include <glob.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#define HEADER_LINES 5
#define MIN_DRAW_COLS 20
#define MIN_DRAW_ROWS 6
#define DEFAULT_GLYPH_RAMP " .:-=+*#%@"
#define CP_BRAILLE_BASE 0x2800
#define CP_UPPER_HALF_BLOCK 0x2580
#define CP_LOWER_HALF_BLOCK 0x2584
#define CP_FULL_BLOCK 0x2588

/* Global state */
static USFile *file = NULL;
static USFileSet *fileset = NULL;
#ifdef HAVE_ZARR
static USFileSet *zarr_fileset = NULL;
#endif
static USMesh *mesh = NULL;
static USRegrid *regrid = NULL;
static USView *view = NULL;
static USVar *variables = NULL;
static USVar *current_var = NULL;
static USVar **var_array = NULL;
static int n_variables = 0;
static int current_var_index = 0;

/* Options */
typedef struct {
    double influence_radius;
    double target_resolution;
    int frame_delay_ms;
    int color_mode;      /* -1 auto, 0 off, 1 on */
    int render_mode;     /* TERM_RENDER_* */
    char mesh_file[MAX_NAME_LEN];
    char glyph_ramp[128];
} UTermOptions;

static UTermOptions options = {
    .influence_radius = DEFAULT_INFLUENCE_RADIUS_M,
    .target_resolution = DEFAULT_RESOLUTION,
    .frame_delay_ms = 200,
    .color_mode = -1,
    .render_mode = TERM_RENDER_ASCII,
    .mesh_file = "",
    .glyph_ramp = DEFAULT_GLYPH_RAMP
};

/* Terminal mode */
static struct termios orig_termios;
static int termios_enabled = 0;

static void disable_raw_mode(void) {
    if (termios_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        termios_enabled = 0;
    }
    printf("\x1b[0m\x1b[?25h");
    fflush(stdout);
}

static void signal_handler(int sig) {
    (void)sig;
    disable_raw_mode();
    _exit(128 + sig);
}

static int enable_raw_mode(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "uterm requires a terminal (tty) on stdin and stdout\n");
        return -1;
    }

    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) {
        perror("tcgetattr");
        return -1;
    }

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        perror("tcsetattr");
        return -1;
    }

    termios_enabled = 1;
    atexit(disable_raw_mode);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("\x1b[?25l");
    fflush(stdout);
    return 0;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int get_terminal_size(int *cols_out, int *rows_out) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols_out = ws.ws_col;
        *rows_out = ws.ws_row;
        return 0;
    }
    *cols_out = 80;
    *rows_out = 24;
    return -1;
}

static int color_enabled(void) {
    if (options.color_mode == 0) return 0;
    if (options.color_mode == 1) return 1;

    const char *term = getenv("TERM");
    const char *no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') return 0;
    if (!term || strcmp(term, "dumb") == 0) return 0;
    return isatty(STDOUT_FILENO);
}

static int is_missing_value(float v, float fill_value) {
    if (fabsf(v) > INVALID_DATA_THRESHOLD || v != v) return 1;
    if (fabsf(fill_value) > 0.0f && fabsf(v - fill_value) < 1e-6f * fabsf(fill_value)) return 1;
    return 0;
}

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void cycle_render_mode(void) {
    options.render_mode = term_cycle_render_mode(options.render_mode);
}

static void print_utf8_codepoint(unsigned int cp) {
    char out[4];
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        fwrite(out, 1, 1, stdout);
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        fwrite(out, 1, 2, stdout);
    } else {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        fwrite(out, 1, 3, stdout);
    }
}

static int sample_field(size_t sx, size_t sy, size_t sub_cols, size_t sub_rows,
                        float range, float *norm_out) {
    if (!view || !current_var || sub_cols == 0 || sub_rows == 0 || !norm_out) return 1;

    size_t data_x = (size_t)(((double)sx + 0.5) * (double)view->data_nx / (double)sub_cols);
    size_t data_y = (size_t)(((double)sy + 0.5) * (double)view->data_ny / (double)sub_rows);
    if (data_x >= view->data_nx) data_x = view->data_nx - 1;
    if (data_y >= view->data_ny) data_y = view->data_ny - 1;

    size_t src_y = view->data_ny - 1 - data_y;
    size_t idx = src_y * view->data_nx + data_x;
    float v = view->regridded_data[idx];

    if (is_missing_value(v, current_var->fill_value)) return 1;

    *norm_out = clamp01((v - current_var->user_min) / range);
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <data_file.nc|data.grib|data.zarr> [file2 ...]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -m, --mesh <file>      Mesh file with coordinates\n");
    fprintf(stderr, "  -r, --resolution <deg> Target grid resolution (default: 1.0)\n");
    fprintf(stderr, "  -i, --influence <m>    Influence radius in meters (default: 200000)\n");
    fprintf(stderr, "  -d, --delay <ms>       Animation frame delay in ms (default: 200)\n");
    fprintf(stderr, "      --chars <ramp>     Glyph ramp, e.g. \" .:-=+*#%%@\"\n");
    fprintf(stderr, "      --render <mode>    Render mode: ascii | half | braille\n");
    fprintf(stderr, "      --color            Force ANSI color output\n");
    fprintf(stderr, "      --no-color         Disable ANSI colors\n");
    fprintf(stderr, "  -h, --help             Show this help\n\n");

    fprintf(stderr, "Keys:\n");
    fprintf(stderr, "  q quit | space pause/resume | j/k time -/+ | u/i depth -/+\n");
    fprintf(stderr, "  n/p next/prev variable | c/C next/prev colormap\n");
    fprintf(stderr, "  m cycle render mode (ascii/half/braille)\n");
    fprintf(stderr, "  [ / ] adjust min down/up | { / } adjust max down/up\n");
    fprintf(stderr, "  r reset range | s save PPM | ? toggle help\n");
}

static int collect_variables(USVar *head) {
    int count = 0;
    for (USVar *v = head; v; v = v->next) count++;
    if (count <= 0) return -1;

    var_array = calloc((size_t)count, sizeof(USVar *));
    if (!var_array) return -1;

    int i = 0;
    for (USVar *v = head; v; v = v->next) {
        var_array[i++] = v;
    }

    n_variables = count;
    return 0;
}

static int set_variable_index(int idx) {
    if (!view || !mesh || !regrid || !var_array) return -1;
    if (idx < 0 || idx >= n_variables) return -1;

    current_var_index = idx;
    current_var = var_array[idx];

    if (view_set_variable(view, current_var, mesh, regrid) != 0) {
        return -1;
    }

    return 0;
}

static void adjust_range(int action) {
    if (!current_var) return;

    float range = current_var->user_max - current_var->user_min;
    float step = range * 0.1f;
    if (step < 0.001f) step = 0.001f;

    switch (action) {
        case 0: /* min down */
            current_var->user_min -= step;
            break;
        case 1: /* min up */
            current_var->user_min += step;
            if (current_var->user_min >= current_var->user_max - step) {
                current_var->user_min = current_var->user_max - step;
            }
            break;
        case 2: /* max down */
            current_var->user_max -= step;
            if (current_var->user_max <= current_var->user_min + step) {
                current_var->user_max = current_var->user_min + step;
            }
            break;
        case 3: /* max up */
            current_var->user_max += step;
            break;
        default:
            break;
    }

    view->data_valid = 0;
}

static void reset_range(void) {
    if (!current_var) return;
    current_var->user_min = current_var->global_min;
    current_var->user_max = current_var->global_max;
    view->data_valid = 0;
}

static void save_frame(void) {
    if (!view || !current_var) return;

    char filename[512];
    snprintf(filename, sizeof(filename), "%s_t%zu_d%zu.ppm",
             current_var->name, view->time_index, view->depth_index);

    if (view_save_ppm(view, filename) == 0) {
        fprintf(stderr, "Saved: %s\n", filename);
    } else {
        fprintf(stderr, "Failed to save frame\n");
    }
}

static void render_frame(int show_help, int animating) {
    if (!view || !current_var) return;

    if (!view->data_valid) {
        if (view_update(view) != 0) {
            fprintf(stderr, "Failed to update view\n");
            return;
        }
    }

    int term_cols = 80;
    int term_rows = 24;
    get_terminal_size(&term_cols, &term_rows);

    int draw_cols = term_cols;
    int draw_rows = term_rows - HEADER_LINES;
    if (draw_cols < MIN_DRAW_COLS) draw_cols = MIN_DRAW_COLS;
    if (draw_rows < MIN_DRAW_ROWS) draw_rows = MIN_DRAW_ROWS;

    const char *ramp = options.glyph_ramp;
    int ramp_len = (int)strlen(ramp);
    if (ramp_len <= 0) {
        ramp = DEFAULT_GLYPH_RAMP;
        ramp_len = (int)strlen(ramp);
    }

    int use_color = color_enabled();
    USColormap *cmap = colormap_get_current();

    printf("\x1b[H\x1b[2J");

    printf("uterm | var %d/%d: %s | time %zu/%zu | depth %zu/%zu | %s\n",
           current_var_index + 1, n_variables, current_var->name,
           view->time_index + 1, view->n_times,
           view->depth_index + 1, view->n_depths,
           animating ? "anim" : "paused");

    if (cmap) {
        printf("cmap: %s | range: %.6g .. %.6g | color: %s | render: %s\n",
               cmap->name, current_var->user_min, current_var->user_max,
               use_color ? "on" : "off", term_render_mode_name(options.render_mode));
    } else {
        printf("cmap: none | range: %.6g .. %.6g | color: %s | render: %s\n",
               current_var->user_min, current_var->user_max,
               use_color ? "on" : "off", term_render_mode_name(options.render_mode));
    }

    printf("keys: q quit | n/p var | j/k time | u/i depth | space play/pause | c/C cmap | m mode\n");
    if (show_help) {
        printf("      [ ] min-/min+  { } max-/max+  r reset range  s save ppm\n");
    } else {
        printf("      ? more help\n");
    }

    float range = current_var->user_max - current_var->user_min;
    if (range <= 0.0f) range = 1.0f;

    if (options.render_mode == TERM_RENDER_ASCII) {
        for (int row = 0; row < draw_rows; row++) {
            int last_r = -1, last_g = -1, last_b = -1;

            for (int col = 0; col < draw_cols; col++) {
                float t = 0.0f;
                if (sample_field((size_t)col, (size_t)row, (size_t)draw_cols, (size_t)draw_rows, range, &t) != 0) {
                    if (use_color && (last_r != -1 || last_g != -1 || last_b != -1)) {
                        printf("\x1b[0m");
                        last_r = last_g = last_b = -1;
                    }
                    putchar(' ');
                    continue;
                }

                int ridx = (int)(t * (float)(ramp_len - 1) + 0.5f);
                if (ridx < 0) ridx = 0;
                if (ridx >= ramp_len) ridx = ramp_len - 1;
                char ch = ramp[ridx];

                if (use_color && cmap) {
                    unsigned char r, g, b;
                    colormap_map_value(cmap, t, &r, &g, &b);
                    if ((int)r != last_r || (int)g != last_g || (int)b != last_b) {
                        printf("\x1b[38;2;%u;%u;%um", r, g, b);
                        last_r = (int)r;
                        last_g = (int)g;
                        last_b = (int)b;
                    }
                }

                putchar(ch);
            }

            if (use_color && (last_r != -1 || last_g != -1 || last_b != -1)) {
                printf("\x1b[0m");
            }
            putchar('\n');
        }
    } else if (options.render_mode == TERM_RENDER_HALF) {
        for (int row = 0; row < draw_rows; row++) {
            int last_fr = -1, last_fg = -1, last_fb = -1;
            int last_br = -1, last_bg = -1, last_bb = -1;

            for (int col = 0; col < draw_cols; col++) {
                float top = 0.0f, bot = 0.0f;
                int top_miss = sample_field((size_t)col, (size_t)(row * 2), (size_t)draw_cols, (size_t)(draw_rows * 2), range, &top);
                int bot_miss = sample_field((size_t)col, (size_t)(row * 2 + 1), (size_t)draw_cols, (size_t)(draw_rows * 2), range, &bot);

                if (top_miss && bot_miss) {
                    if (use_color && (last_fr != -1 || last_br != -1)) {
                        printf("\x1b[0m");
                        last_fr = last_fg = last_fb = -1;
                        last_br = last_bg = last_bb = -1;
                    }
                    putchar(' ');
                    continue;
                }

                if (use_color && cmap) {
                    unsigned char tr = 255, tg = 255, tb = 255;
                    unsigned char br = 255, bg = 255, bb = 255;
                    if (!top_miss) colormap_map_value(cmap, top, &tr, &tg, &tb);
                    if (!bot_miss) colormap_map_value(cmap, bot, &br, &bg, &bb);

                    if ((int)tr != last_fr || (int)tg != last_fg || (int)tb != last_fb ||
                        (int)br != last_br || (int)bg != last_bg || (int)bb != last_bb) {
                        printf("\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um", tr, tg, tb, br, bg, bb);
                        last_fr = (int)tr;
                        last_fg = (int)tg;
                        last_fb = (int)tb;
                        last_br = (int)br;
                        last_bg = (int)bg;
                        last_bb = (int)bb;
                    }
                    print_utf8_codepoint(CP_UPPER_HALF_BLOCK);
                } else {
                    int top_on = (!top_miss && top >= 0.5f) ? 1 : 0;
                    int bot_on = (!bot_miss && bot >= 0.5f) ? 1 : 0;

                    if (top_on && bot_on) {
                        print_utf8_codepoint(CP_FULL_BLOCK);
                    } else if (top_on) {
                        print_utf8_codepoint(CP_UPPER_HALF_BLOCK);
                    } else if (bot_on) {
                        print_utf8_codepoint(CP_LOWER_HALF_BLOCK);
                    } else {
                        putchar(' ');
                    }
                }
            }

            if (use_color && (last_fr != -1 || last_br != -1)) {
                printf("\x1b[0m");
            }
            putchar('\n');
        }
    } else {
        /* 2x4 subpixel braille renderer with ordered dithering */
        static const float bayer_4x2[4][2] = {
            {0.0625f, 0.5625f},
            {0.8125f, 0.3125f},
            {0.4375f, 0.9375f},
            {0.6875f, 0.1875f}
        };
        static const unsigned char dot_bit[4][2] = {
            {0x01, 0x08}, /* dots 1,4 */
            {0x02, 0x10}, /* dots 2,5 */
            {0x04, 0x20}, /* dots 3,6 */
            {0x40, 0x80}  /* dots 7,8 */
        };

        for (int row = 0; row < draw_rows; row++) {
            int last_r = -1, last_g = -1, last_b = -1;

            for (int col = 0; col < draw_cols; col++) {
                unsigned char mask = 0;
                float mean_t = 0.0f;
                int valid = 0;

                for (int dy = 0; dy < 4; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        float t = 0.0f;
                        size_t sx = (size_t)(col * 2 + dx);
                        size_t sy = (size_t)(row * 4 + dy);
                        if (sample_field(sx, sy, (size_t)(draw_cols * 2), (size_t)(draw_rows * 4), range, &t) != 0) {
                            continue;
                        }

                        valid++;
                        mean_t += t;
                        if (t >= bayer_4x2[dy][dx]) {
                            mask |= dot_bit[dy][dx];
                        }
                    }
                }

                if (valid == 0) {
                    if (use_color && (last_r != -1 || last_g != -1 || last_b != -1)) {
                        printf("\x1b[0m");
                        last_r = last_g = last_b = -1;
                    }
                    putchar(' ');
                    continue;
                }

                if (use_color && cmap) {
                    float avg_t = mean_t / (float)valid;
                    unsigned char r, g, b;
                    colormap_map_value(cmap, avg_t, &r, &g, &b);
                    if ((int)r != last_r || (int)g != last_g || (int)b != last_b) {
                        printf("\x1b[38;2;%u;%u;%um", r, g, b);
                        last_r = (int)r;
                        last_g = (int)g;
                        last_b = (int)b;
                    }
                }

                if (mask == 0) {
                    putchar(' ');
                } else {
                    print_utf8_codepoint(CP_BRAILLE_BASE + mask);
                }
            }

            if (use_color && (last_r != -1 || last_g != -1 || last_b != -1)) {
                printf("\x1b[0m");
            }
            putchar('\n');
        }
    }

    fflush(stdout);
}

static int open_data_files(int n_data_files, const char **data_filenames) {
    int use_glob = 0;
    if (n_data_files == 1) {
        const char *fn = data_filenames[0];
        for (const char *p = fn; *p; p++) {
            if (*p == '*' || *p == '?' || *p == '[') {
                use_glob = 1;
                break;
            }
        }
    }

#ifdef HAVE_GRIB
    if (!use_glob && n_data_files == 1 && grib_is_grib_file(data_filenames[0])) {
        file = grib_open(data_filenames[0]);
        if (!file) {
            fprintf(stderr, "Failed to open GRIB file: %s\n", data_filenames[0]);
            return -1;
        }
        return 0;
    }
#endif

#ifdef HAVE_ZARR
    if (n_data_files == 1 && !use_glob && zarr_is_zarr_store(data_filenames[0])) {
        file = zarr_open(data_filenames[0]);
        if (!file) {
            fprintf(stderr, "Failed to open zarr store: %s\n", data_filenames[0]);
            return -1;
        }
        return 0;
    } else if (use_glob) {
        glob_t test_glob;
        int is_zarr_glob = 0;
        if (glob(data_filenames[0], GLOB_TILDE | GLOB_NOSORT, NULL, &test_glob) == 0 &&
            test_glob.gl_pathc > 0) {
            is_zarr_glob = zarr_is_zarr_store(test_glob.gl_pathv[0]);
            globfree(&test_glob);
        }

        if (is_zarr_glob) {
            zarr_fileset = zarr_open_glob(data_filenames[0]);
            if (!zarr_fileset) {
                fprintf(stderr, "Failed to open zarr stores matching: %s\n", data_filenames[0]);
                return -1;
            }
            file = zarr_fileset->files[0];
            return 0;
        }
    }
#endif

    if (use_glob) {
        fileset = netcdf_open_glob(data_filenames[0]);
        if (!fileset) {
            fprintf(stderr, "Failed to open files matching: %s\n", data_filenames[0]);
            return -1;
        }
        file = fileset->files[0];
        return 0;
    }

#ifdef HAVE_ZARR
    if (n_data_files > 1 && zarr_is_zarr_store(data_filenames[0])) {
        zarr_fileset = zarr_open_fileset(data_filenames, n_data_files);
        if (!zarr_fileset) {
            fprintf(stderr, "Failed to open zarr stores\n");
            return -1;
        }
        file = zarr_fileset->files[0];
        return 0;
    }
#endif

    if (n_data_files > 1) {
        fileset = netcdf_open_fileset(data_filenames, n_data_files);
        if (!fileset) {
            fprintf(stderr, "Failed to open data files\n");
            return -1;
        }
        file = fileset->files[0];
    } else {
        file = netcdf_open(data_filenames[0]);
        if (!file) {
            fprintf(stderr, "Failed to open data file: %s\n", data_filenames[0]);
            return -1;
        }
    }

    return 0;
}

static void cleanup_all(void) {
    free(var_array);
    var_array = NULL;

    view_free(view);
    view = NULL;

    regrid_free(regrid);
    regrid = NULL;

    mesh_free(mesh);
    mesh = NULL;

#ifdef HAVE_ZARR
    if (zarr_fileset) {
        zarr_close_fileset(zarr_fileset);
        zarr_fileset = NULL;
    } else if (file && file->file_type == FILE_TYPE_ZARR) {
        zarr_close(file);
        file = NULL;
    } else
#endif
#ifdef HAVE_GRIB
    if (file && file->file_type == FILE_TYPE_GRIB) {
        grib_close(file);
        file = NULL;
    } else
#endif
    if (fileset) {
        netcdf_close_fileset(fileset);
        fileset = NULL;
    } else if (file) {
        netcdf_close(file);
        file = NULL;
    }

    colormaps_cleanup();
}

static int parse_options(int argc, char **argv, int *first_data_arg) {
    static struct option long_options[] = {
        {"mesh", required_argument, 0, 'm'},
        {"resolution", required_argument, 0, 'r'},
        {"influence", required_argument, 0, 'i'},
        {"delay", required_argument, 0, 'd'},
        {"chars", required_argument, 0, 1000},
        {"render", required_argument, 0, 1003},
        {"color", no_argument, 0, 1001},
        {"no-color", no_argument, 0, 1002},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:r:i:d:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                strncpy(options.mesh_file, optarg, MAX_NAME_LEN - 1);
                options.mesh_file[MAX_NAME_LEN - 1] = '\0';
                break;
            case 'r':
                options.target_resolution = atof(optarg);
                break;
            case 'i':
                options.influence_radius = atof(optarg);
                break;
            case 'd':
                options.frame_delay_ms = atoi(optarg);
                if (options.frame_delay_ms < 10) options.frame_delay_ms = 10;
                break;
            case 'h':
                print_usage(argv[0]);
                return 1;
            case 1000:
                strncpy(options.glyph_ramp, optarg, sizeof(options.glyph_ramp) - 1);
                options.glyph_ramp[sizeof(options.glyph_ramp) - 1] = '\0';
                break;
            case 1003: {
                int mode = TERM_RENDER_ASCII;
                if (term_parse_render_mode(optarg, &mode) != 0) {
                    fprintf(stderr, "Invalid render mode: %s (use ascii|half|braille)\n", optarg);
                    return -1;
                }
                options.render_mode = mode;
                break;
            }
            case 1001:
                options.color_mode = 1;
                break;
            case 1002:
                options.color_mode = 0;
                break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No data file specified\n");
        print_usage(argv[0]);
        return -1;
    }

    *first_data_arg = optind;
    return 0;
}

int main(int argc, char *argv[]) {
    int first_data_arg = 0;
    int parse_rc = parse_options(argc, argv, &first_data_arg);
    if (parse_rc != 0) {
        return (parse_rc > 0) ? 0 : 1;
    }

    int n_data_files = argc - first_data_arg;
    const char **data_filenames = (const char **)&argv[first_data_arg];
    const char *mesh_filename = options.mesh_file[0] ? options.mesh_file : NULL;

    colormaps_init();

    if (open_data_files(n_data_files, data_filenames) != 0) {
        cleanup_all();
        return 1;
    }

#ifdef HAVE_ZARR
    if (file->file_type == FILE_TYPE_ZARR) {
        mesh = mesh_create_from_zarr(file);
    } else
#endif
#ifdef HAVE_GRIB
    if (file->file_type == FILE_TYPE_GRIB) {
        mesh = grib_create_mesh(file);
    } else
#endif
    {
        mesh = mesh_create_from_netcdf(file->ncid, mesh_filename);
    }

    if (!mesh) {
        fprintf(stderr, "Failed to load mesh\n");
        cleanup_all();
        return 1;
    }

    regrid = regrid_create(mesh, options.target_resolution, options.influence_radius);
    if (!regrid) {
        fprintf(stderr, "Failed to create regrid structure\n");
        cleanup_all();
        return 1;
    }

#ifdef HAVE_ZARR
    if (file->file_type == FILE_TYPE_ZARR) {
        variables = zarr_scan_variables(file, mesh);
    } else
#endif
#ifdef HAVE_GRIB
    if (file->file_type == FILE_TYPE_GRIB) {
        variables = grib_scan_variables(file, mesh);
    } else
#endif
    {
        variables = netcdf_scan_variables(file, mesh);
    }

    if (!variables) {
        fprintf(stderr, "No displayable variables found\n");
        cleanup_all();
        return 1;
    }

    if (collect_variables(variables) != 0) {
        fprintf(stderr, "Failed to collect variables\n");
        cleanup_all();
        return 1;
    }

    view = view_create();
    if (!view) {
        fprintf(stderr, "Failed to create view\n");
        cleanup_all();
        return 1;
    }

    if (fileset) view_set_fileset(view, fileset);
#ifdef HAVE_ZARR
    if (zarr_fileset) view_set_fileset(view, zarr_fileset);
#endif

    if (set_variable_index(0) != 0) {
        fprintf(stderr, "Failed to set initial variable\n");
        cleanup_all();
        return 1;
    }

    if (enable_raw_mode() != 0) {
        cleanup_all();
        return 1;
    }

    int running = 1;
    int animating = 0;
    int show_help = 0;
    double next_frame_time = now_seconds();

    render_frame(show_help, animating);

    while (running) {
        double now = now_seconds();
        int timeout_ms = 200;
        if (animating) {
            double wait_sec = next_frame_time - now;
            if (wait_sec < 0.0) wait_sec = 0.0;
            timeout_ms = (int)(wait_sec * 1000.0);
            if (timeout_ms > options.frame_delay_ms) timeout_ms = options.frame_delay_ms;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            unsigned char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n == 1) {
                int changed = 0;

                if (ch == '\033') {
                    /* Basic arrow key support: ESC [ A/B/C/D */
                    unsigned char seq[2];
                    if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1 && seq[0] == '[') {
                        if (seq[1] == 'D') { /* left */
                            if (view_step_time(view, -1) >= 0) changed = 1;
                        } else if (seq[1] == 'C') { /* right */
                            if (view_step_time(view, 1) >= 0) changed = 1;
                        } else if (seq[1] == 'A') { /* up */
                            if (view_set_depth(view, (view->depth_index + 1 < view->n_depths) ? view->depth_index + 1 : view->depth_index) == 0) changed = 1;
                        } else if (seq[1] == 'B') { /* down */
                            if (view_set_depth(view, (view->depth_index > 0) ? view->depth_index - 1 : 0) == 0) changed = 1;
                        }
                    }
                } else {
                    switch (ch) {
                        case 'q':
                            running = 0;
                            break;
                        case ' ':
                            animating = !animating;
                            next_frame_time = now_seconds() + (double)options.frame_delay_ms / 1000.0;
                            changed = 1;
                            break;
                        case 'j':
                            if (view_step_time(view, -1) >= 0) changed = 1;
                            break;
                        case 'k':
                            if (view_step_time(view, 1) >= 0) changed = 1;
                            break;
                        case 'u':
                            if (view->depth_index > 0) {
                                view_set_depth(view, view->depth_index - 1);
                                changed = 1;
                            }
                            break;
                        case 'i':
                            if (view->depth_index + 1 < view->n_depths) {
                                view_set_depth(view, view->depth_index + 1);
                                changed = 1;
                            }
                            break;
                        case 'n':
                            if (n_variables > 1) {
                                int ni = (current_var_index + 1) % n_variables;
                                if (set_variable_index(ni) == 0) changed = 1;
                            }
                            break;
                        case 'p':
                            if (n_variables > 1) {
                                int pi = (current_var_index + n_variables - 1) % n_variables;
                                if (set_variable_index(pi) == 0) changed = 1;
                            }
                            break;
                        case 'c':
                            colormap_next();
                            view->data_valid = 0;
                            changed = 1;
                            break;
                        case 'C':
                            colormap_prev();
                            view->data_valid = 0;
                            changed = 1;
                            break;
                        case 'm':
                            cycle_render_mode();
                            changed = 1;
                            break;
                        case '[':
                            adjust_range(0);
                            changed = 1;
                            break;
                        case ']':
                            adjust_range(1);
                            changed = 1;
                            break;
                        case '{':
                            adjust_range(2);
                            changed = 1;
                            break;
                        case '}':
                            adjust_range(3);
                            changed = 1;
                            break;
                        case 'r':
                            reset_range();
                            changed = 1;
                            break;
                        case 's':
                            save_frame();
                            changed = 1;
                            break;
                        case '?':
                            show_help = !show_help;
                            changed = 1;
                            break;
                        default:
                            if (ch >= '1' && ch <= '9') {
                                int idx = (int)(ch - '1');
                                if (idx < n_variables && set_variable_index(idx) == 0) {
                                    changed = 1;
                                }
                            }
                            break;
                    }
                }

                if (changed) {
                    render_frame(show_help, animating);
                }
            }
        }

        if (animating) {
            now = now_seconds();
            if (now >= next_frame_time) {
                if (view_step_time(view, 1) < 0) {
                    view_set_time(view, 0);
                }
                render_frame(show_help, animating);
                next_frame_time = now + (double)options.frame_delay_ms / 1000.0;
            }
        }
    }

    printf("\x1b[H\x1b[2J");
    cleanup_all();
    return 0;
}
