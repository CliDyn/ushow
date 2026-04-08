/*
 * timeseries_popup.c - Time series plot popup window
 *
 * Non-modal popup with custom XLib drawing that displays overlaid time series
 * plots (value vs time) at clicked spatial locations. Up to MAX_TRACES traces
 * are kept in a ring buffer; oldest is dropped when full. Right-click clears.
 */

#include "timeseries_popup.h"
#include "x_interface.h"
#include <X11/Xlib.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Simple.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* Layout constants */
#define PLOT_WIDTH      600
#define PLOT_HEIGHT     400
#define MARGIN_LEFT     80
#define MARGIN_RIGHT    20
#define MARGIN_TOP      40
#define MARGIN_BOTTOM   60
#define TICK_LEN        5
#define DOT_RADIUS      3
#define MAX_TRACES      8

/* X11 handles */
static Display *ts_display = NULL;
static Widget ts_shell = NULL;
static Widget ts_plot_widget = NULL;
static Widget ts_close_btn = NULL;
static GC ts_gc = None;

/* Colors */
static unsigned long color_gray = 0;
static unsigned long color_bg = 0;
static unsigned long color_axis = 0;
static unsigned long color_label = 0;
static unsigned long trace_colors[MAX_TRACES];
static int colors_allocated = 0;

/* Multi-trace ring buffer */
static TSData traces[MAX_TRACES];
static int trace_valid[MAX_TRACES];  /* 1 if slot has data */
static int trace_count = 0;         /* number of active traces */
static int trace_head = 0;          /* next write position */

/* ========== CF Time Formatting (self-contained) ========== */

static int ts_parse_time_units(const char *units, double *unit_seconds,
                               int *y, int *mo, int *d, int *h, int *mi, double *sec) {
    if (!units || !unit_seconds || !y || !mo || !d || !h || !mi || !sec) return 0;

    const char *since = strstr(units, "since");
    if (!since) return 0;

    char unit_buf[32] = {0};
    if (sscanf(units, "%31s", unit_buf) != 1) return 0;

    for (char *p = unit_buf; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    }

    if (strcmp(unit_buf, "seconds") == 0 || strcmp(unit_buf, "second") == 0 ||
        strcmp(unit_buf, "secs") == 0 || strcmp(unit_buf, "sec") == 0 || strcmp(unit_buf, "s") == 0) {
        *unit_seconds = 1.0;
    } else if (strcmp(unit_buf, "minutes") == 0 || strcmp(unit_buf, "minute") == 0 ||
               strcmp(unit_buf, "mins") == 0 || strcmp(unit_buf, "min") == 0) {
        *unit_seconds = 60.0;
    } else if (strcmp(unit_buf, "hours") == 0 || strcmp(unit_buf, "hour") == 0 ||
               strcmp(unit_buf, "hrs") == 0 || strcmp(unit_buf, "hr") == 0) {
        *unit_seconds = 3600.0;
    } else if (strcmp(unit_buf, "days") == 0 || strcmp(unit_buf, "day") == 0) {
        *unit_seconds = 86400.0;
    } else {
        return 0;
    }

    const char *p = since + 5;
    while (*p == ' ') p++;
    int n = sscanf(p, "%d-%d-%d %d:%d:%lf", y, mo, d, h, mi, sec);
    if (n < 3) return 0;
    if (n == 3) { *h = 0; *mi = 0; *sec = 0.0; }
    return 1;
}

static int64_t ts_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)(era * 146097 + (int)doe - 719468);
}

static void ts_civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int era = (int)((z >= 0 ? z : z - 146096) / 146097);
    const unsigned doe = (unsigned)(z - (int64_t)era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y_tmp = (int)(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d_tmp = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m_tmp = mp + (mp < 10 ? 3 : -9);
    y_tmp += (m_tmp <= 2);
    *y = y_tmp;
    *m = m_tmp;
    *d = d_tmp;
}

static int ts_format_time(char *out, size_t outlen, double value, const char *units) {
    double unit_seconds = 0.0;
    int y, mo, d, h, mi;
    double sec;
    if (!ts_parse_time_units(units, &unit_seconds, &y, &mo, &d, &h, &mi, &sec))
        return 0;

    int64_t days = ts_days_from_civil(y, (unsigned)mo, (unsigned)d);
    double total_sec = (double)days * 86400.0 + (double)h * 3600.0 + (double)mi * 60.0 + sec;
    total_sec += value * unit_seconds;

    int64_t out_days = (int64_t)(total_sec / 86400.0);
    double rem = total_sec - (double)out_days * 86400.0;
    if (rem < 0) { rem += 86400.0; out_days -= 1; }

    int out_y;
    unsigned out_m, out_d;
    ts_civil_from_days(out_days, &out_y, &out_m, &out_d);

    snprintf(out, outlen, "%04d-%02u-%02u", out_y, out_m, out_d);
    return 1;
}

/* ========== "Nice Numbers" Tick Algorithm ========== */

static double nice_number(double x, int round_flag) {
    int exp_val = (int)floor(log10(x));
    double f = x / pow(10.0, exp_val);
    double nf;

    if (round_flag) {
        if (f < 1.5) nf = 1.0;
        else if (f < 3.0) nf = 2.0;
        else if (f < 7.0) nf = 5.0;
        else nf = 10.0;
    } else {
        if (f <= 1.0) nf = 1.0;
        else if (f <= 2.0) nf = 2.0;
        else if (f <= 5.0) nf = 5.0;
        else nf = 10.0;
    }
    return nf * pow(10.0, exp_val);
}

static void compute_ticks(double data_min, double data_max, int max_ticks,
                          double *tick_min, double *tick_max, double *tick_step, int *n_ticks) {
    double range = data_max - data_min;
    if (range <= 0.0) {
        range = 1.0;
        data_min -= 0.5;
        data_max += 0.5;
    }

    double nice_range = nice_number(range, 0);
    *tick_step = nice_number(nice_range / (max_ticks - 1), 1);
    *tick_min = floor(data_min / *tick_step) * *tick_step;
    *tick_max = ceil(data_max / *tick_step) * *tick_step;
    *n_ticks = (int)((*tick_max - *tick_min) / *tick_step) + 1;
    if (*n_ticks > max_ticks + 2) *n_ticks = max_ticks + 2;
}

/* ========== Allocate Colors ========== */

static unsigned long alloc_color(int screen, Colormap cmap,
                                 unsigned short r, unsigned short g, unsigned short b,
                                 unsigned long fallback) {
    XColor xc;
    xc.red = r; xc.green = g; xc.blue = b;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(ts_display, cmap, &xc))
        return xc.pixel;
    return fallback;
}

static void allocate_colors(void) {
    if (colors_allocated || !ts_display) return;

    int screen = DefaultScreen(ts_display);
    Colormap cmap = DefaultColormap(ts_display, screen);
    int is_light = x_is_light_theme();
    unsigned long black = BlackPixel(ts_display, screen);
    unsigned long white = WhitePixel(ts_display, screen);

    /* 8 trace colors — visually distinct on both themes */
    if (is_light) {
        trace_colors[0] = alloc_color(screen, cmap, 0x2222, 0x5555, 0xCCCC, black); /* blue */
        trace_colors[1] = alloc_color(screen, cmap, 0xCC00, 0x3333, 0x3333, black); /* red */
        trace_colors[2] = alloc_color(screen, cmap, 0x2222, 0x8888, 0x2222, black); /* green */
        trace_colors[3] = alloc_color(screen, cmap, 0xCC00, 0x7700, 0x0000, black); /* orange */
        trace_colors[4] = alloc_color(screen, cmap, 0x8800, 0x2222, 0xAA00, black); /* purple */
        trace_colors[5] = alloc_color(screen, cmap, 0x0000, 0x8888, 0x8888, black); /* cyan */
        trace_colors[6] = alloc_color(screen, cmap, 0xBB00, 0x2222, 0x8800, black); /* magenta */
        trace_colors[7] = alloc_color(screen, cmap, 0x8800, 0x5500, 0x1100, black); /* brown */
    } else {
        trace_colors[0] = alloc_color(screen, cmap, 0x5555, 0x9999, 0xFFFF, white); /* blue */
        trace_colors[1] = alloc_color(screen, cmap, 0xFFFF, 0x5555, 0x5555, white); /* red */
        trace_colors[2] = alloc_color(screen, cmap, 0x5555, 0xDD00, 0x5555, white); /* green */
        trace_colors[3] = alloc_color(screen, cmap, 0xFFFF, 0xAA00, 0x3333, white); /* orange */
        trace_colors[4] = alloc_color(screen, cmap, 0xBB00, 0x5555, 0xFFFF, white); /* purple */
        trace_colors[5] = alloc_color(screen, cmap, 0x5555, 0xEEEE, 0xEEEE, white); /* cyan */
        trace_colors[6] = alloc_color(screen, cmap, 0xFFFF, 0x5555, 0xCC00, white); /* magenta */
        trace_colors[7] = alloc_color(screen, cmap, 0xCC00, 0x8888, 0x3333, white); /* brown */
    }

    /* Grid lines */
    if (is_light)
        color_gray = alloc_color(screen, cmap, 0xCCCC, 0xCCCC, 0xCCCC, white);
    else
        color_gray = alloc_color(screen, cmap, 0x4444, 0x4444, 0x4444, black);

    /* Background */
    if (is_light) {
        color_bg = white;
    } else {
        color_bg = alloc_color(screen, cmap, 0x1E1E, 0x1E1E, 0x1E1E, black);
    }

    /* Axis color */
    if (is_light)
        color_axis = alloc_color(screen, cmap, 0x4444, 0x4444, 0x4444, black);
    else
        color_axis = alloc_color(screen, cmap, 0x8888, 0x8888, 0x8888, white);

    /* Label color */
    if (is_light)
        color_label = alloc_color(screen, cmap, 0x2222, 0x2222, 0x2222, black);
    else
        color_label = alloc_color(screen, cmap, 0xCCCC, 0xCCCC, 0xCCCC, white);

    colors_allocated = 1;
}

/* ========== Drawing ========== */

static void draw_plot(Widget w) {
    if (trace_count == 0 || !ts_display || ts_gc == None) return;
    if (!XtIsRealized(w)) return;

    Window win = XtWindow(w);

    allocate_colors();

    /* Plot area dimensions */
    int plot_x0 = MARGIN_LEFT;
    int plot_y0 = MARGIN_TOP;
    int plot_x1 = PLOT_WIDTH - MARGIN_RIGHT;
    int plot_y1 = PLOT_HEIGHT - MARGIN_BOTTOM;
    int plot_w = plot_x1 - plot_x0;
    int plot_h = plot_y1 - plot_y0;

    /* Background */
    XSetForeground(ts_display, ts_gc, color_bg);
    XFillRectangle(ts_display, win, ts_gc, 0, 0, PLOT_WIDTH, PLOT_HEIGHT);

    /* Compute Y range across ALL traces */
    double y_min = 1e30, y_max = -1e30;
    double x_min = 1e30, x_max = -1e30;
    /* Use first valid trace for x_label (CF time detection) */
    const char *x_label = "";
    const char *y_label = "";

    for (int t = 0; t < MAX_TRACES; t++) {
        if (!trace_valid[t]) continue;
        TSData *tr = &traces[t];
        if (x_label[0] == '\0' && tr->x_label[0] != '\0') x_label = tr->x_label;
        if (y_label[0] == '\0' && tr->y_label[0] != '\0') y_label = tr->y_label;
        for (size_t i = 0; i < tr->n_points; i++) {
            if (tr->valid[i]) {
                double v = (double)tr->values[i];
                if (v < y_min) y_min = v;
                if (v > y_max) y_max = v;
            }
            double tx = tr->times[i];
            if (tx < x_min) x_min = tx;
            if (tx > x_max) x_max = tx;
        }
    }
    if (y_min >= y_max) { y_min -= 0.5; y_max += 0.5; }
    if (x_min >= x_max) { x_min -= 0.5; x_max += 0.5; }

    /* Compute ticks */
    double y_tick_min, y_tick_max, y_tick_step;
    int n_y_ticks;
    compute_ticks(y_min, y_max, 8, &y_tick_min, &y_tick_max, &y_tick_step, &n_y_ticks);

    double x_tick_min, x_tick_max, x_tick_step;
    int n_x_ticks;
    compute_ticks(x_min, x_max, 6, &x_tick_min, &x_tick_max, &x_tick_step, &n_x_ticks);

    double range_y = y_tick_max - y_tick_min;
    double range_x = x_tick_max - x_tick_min;
    if (range_y <= 0) range_y = 1.0;
    if (range_x <= 0) range_x = 1.0;

    int use_cf_time = (x_label[0] != '\0' && strstr(x_label, "since") != NULL);

    /* Draw grid lines */
    XSetForeground(ts_display, ts_gc, color_gray);
    for (int i = 0; i < n_y_ticks; i++) {
        double val = y_tick_min + i * y_tick_step;
        if (val > y_tick_max + y_tick_step * 0.01) break;
        int py = plot_y1 - (int)((val - y_tick_min) / range_y * plot_h);
        if (py >= plot_y0 && py <= plot_y1)
            XDrawLine(ts_display, win, ts_gc, plot_x0, py, plot_x1, py);
    }
    for (int i = 0; i < n_x_ticks; i++) {
        double val = x_tick_min + i * x_tick_step;
        if (val > x_tick_max + x_tick_step * 0.01) break;
        int px = plot_x0 + (int)((val - x_tick_min) / range_x * plot_w);
        if (px >= plot_x0 && px <= plot_x1)
            XDrawLine(ts_display, win, ts_gc, px, plot_y0, px, plot_y1);
    }

    /* Draw axes */
    XSetForeground(ts_display, ts_gc, color_axis);
    XDrawRectangle(ts_display, win, ts_gc, plot_x0, plot_y0, plot_w, plot_h);

    /* Y-axis tick labels */
    XSetForeground(ts_display, ts_gc, color_label);
    XFontStruct *font = XQueryFont(ts_display, XGContextFromGC(ts_gc));
    int font_ascent = font ? font->ascent : 10;

    for (int i = 0; i < n_y_ticks; i++) {
        double val = y_tick_min + i * y_tick_step;
        if (val > y_tick_max + y_tick_step * 0.01) break;
        int py = plot_y1 - (int)((val - y_tick_min) / range_y * plot_h);
        if (py < plot_y0 || py > plot_y1) continue;

        XSetForeground(ts_display, ts_gc, color_axis);
        XDrawLine(ts_display, win, ts_gc, plot_x0 - TICK_LEN, py, plot_x0, py);

        XSetForeground(ts_display, ts_gc, color_label);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g", val);
        int tw = font ? XTextWidth(font, buf, (int)strlen(buf)) : 40;
        XDrawString(ts_display, win, ts_gc,
                    plot_x0 - TICK_LEN - tw - 4, py + font_ascent / 2,
                    buf, (int)strlen(buf));
    }

    /* X-axis tick labels */
    for (int i = 0; i < n_x_ticks; i++) {
        double val = x_tick_min + i * x_tick_step;
        if (val > x_tick_max + x_tick_step * 0.01) break;
        int px = plot_x0 + (int)((val - x_tick_min) / range_x * plot_w);
        if (px < plot_x0 || px > plot_x1) continue;

        XSetForeground(ts_display, ts_gc, color_axis);
        XDrawLine(ts_display, win, ts_gc, px, plot_y1, px, plot_y1 + TICK_LEN);

        XSetForeground(ts_display, ts_gc, color_label);
        char buf[32];
        if (use_cf_time) {
            if (!ts_format_time(buf, sizeof(buf), val, x_label))
                snprintf(buf, sizeof(buf), "%.4g", val);
        } else {
            snprintf(buf, sizeof(buf), "%.4g", val);
        }
        int tw = font ? XTextWidth(font, buf, (int)strlen(buf)) : 40;
        XDrawString(ts_display, win, ts_gc,
                    px - tw / 2, plot_y1 + TICK_LEN + font_ascent + 4,
                    buf, (int)strlen(buf));
    }

    /* X-axis label */
    if (x_label[0]) {
        const char *xlabel = use_cf_time ? "Date" : x_label;
        int tw = font ? XTextWidth(font, xlabel, (int)strlen(xlabel)) : 40;
        XSetForeground(ts_display, ts_gc, color_label);
        XDrawString(ts_display, win, ts_gc,
                    plot_x0 + plot_w / 2 - tw / 2, PLOT_HEIGHT - 5,
                    xlabel, (int)strlen(xlabel));
    }

    /* Y-axis label */
    if (y_label[0]) {
        XSetForeground(ts_display, ts_gc, color_label);
        XDrawString(ts_display, win, ts_gc,
                    4, plot_y0 - 8,
                    y_label, (int)strlen(y_label));
    }

    /* Title */
    {
        char title_buf[128];
        if (trace_count == 1) {
            /* Find the single valid trace and use its title */
            for (int t = 0; t < MAX_TRACES; t++) {
                if (trace_valid[t]) {
                    snprintf(title_buf, sizeof(title_buf), "%s", traces[t].title);
                    break;
                }
            }
        } else {
            snprintf(title_buf, sizeof(title_buf), "Time Series (%d traces)", trace_count);
        }
        int tw = font ? XTextWidth(font, title_buf, (int)strlen(title_buf)) : 100;
        XSetForeground(ts_display, ts_gc, color_label);
        XDrawString(ts_display, win, ts_gc,
                    PLOT_WIDTH / 2 - tw / 2, font_ascent + 4,
                    title_buf, (int)strlen(title_buf));
    }

    /* Draw all traces */
    XSetLineAttributes(ts_display, ts_gc, 2, LineSolid, CapRound, JoinRound);

    int color_idx = 0;
    for (int t = 0; t < MAX_TRACES; t++) {
        if (!trace_valid[t]) continue;
        TSData *tr = &traces[t];

        XSetForeground(ts_display, ts_gc, trace_colors[color_idx % MAX_TRACES]);

        int prev_px = -1, prev_py = -1;
        int prev_valid = 0;

        for (size_t i = 0; i < tr->n_points; i++) {
            if (!tr->valid[i]) {
                prev_valid = 0;
                continue;
            }

            double tx = tr->times[i];
            double v = (double)tr->values[i];

            int px = plot_x0 + (int)((tx - x_tick_min) / range_x * plot_w);
            int py = plot_y1 - (int)((v - y_tick_min) / range_y * plot_h);

            if (px < plot_x0) px = plot_x0;
            if (px > plot_x1) px = plot_x1;
            if (py < plot_y0) py = plot_y0;
            if (py > plot_y1) py = plot_y1;

            if (prev_valid)
                XDrawLine(ts_display, win, ts_gc, prev_px, prev_py, px, py);

            XFillArc(ts_display, win, ts_gc,
                     px - DOT_RADIUS, py - DOT_RADIUS,
                     DOT_RADIUS * 2, DOT_RADIUS * 2, 0, 360 * 64);

            prev_px = px;
            prev_py = py;
            prev_valid = 1;
        }
        color_idx++;
    }

    /* Legend (top-right corner) */
    if (trace_count > 1) {
        int legend_x = plot_x1 - 150;
        int legend_y = plot_y0 + 5;
        int line_h = font_ascent + 4;

        color_idx = 0;
        for (int t = 0; t < MAX_TRACES; t++) {
            if (!trace_valid[t]) continue;

            /* Extract short location label from title: find "at ..." part */
            const char *loc = strstr(traces[t].title, "at ");
            const char *label = loc ? loc : traces[t].title;
            char short_label[32];
            snprintf(short_label, sizeof(short_label), "%s", label);

            /* Colored line segment */
            XSetForeground(ts_display, ts_gc, trace_colors[color_idx % MAX_TRACES]);
            XDrawLine(ts_display, win, ts_gc,
                      legend_x, legend_y + color_idx * line_h + font_ascent / 2,
                      legend_x + 15, legend_y + color_idx * line_h + font_ascent / 2);

            /* Label text */
            XSetForeground(ts_display, ts_gc, color_label);
            XDrawString(ts_display, win, ts_gc,
                        legend_x + 20, legend_y + color_idx * line_h + font_ascent,
                        short_label, (int)strlen(short_label));

            color_idx++;
        }
    }

    /* Reset line width */
    XSetLineAttributes(ts_display, ts_gc, 0, LineSolid, CapButt, JoinMiter);
    XSetForeground(ts_display, ts_gc, color_label);

    if (font) {
        XFreeFontInfo(NULL, font, 1);
    }

    XFlush(ts_display);
}

/* Forward declarations */
static void free_all_traces(void);

/* ========== Event Handlers ========== */

static void ts_expose_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data; (void)cont;
    if (event->type == Expose) {
        draw_plot(w);
    }
}

static void ts_close_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (ts_shell) {
        XtPopdown(ts_shell);
        free_all_traces();
    }
}

/* ========== Trace Management ========== */

static void free_trace(int idx) {
    if (trace_valid[idx]) {
        free(traces[idx].times);
        free(traces[idx].values);
        free(traces[idx].valid);
        traces[idx].times = NULL;
        traces[idx].values = NULL;
        traces[idx].valid = NULL;
        trace_valid[idx] = 0;
    }
}

static void free_all_traces(void) {
    for (int i = 0; i < MAX_TRACES; i++)
        free_trace(i);
    trace_count = 0;
    trace_head = 0;
}

static void add_trace(const TSData *data) {
    /* Free old trace at this slot if occupied */
    free_trace(trace_head);

    TSData *tr = &traces[trace_head];
    tr->n_points = data->n_points;
    tr->n_valid = data->n_valid;
    memcpy(tr->title, data->title, sizeof(tr->title));
    memcpy(tr->x_label, data->x_label, sizeof(tr->x_label));
    memcpy(tr->y_label, data->y_label, sizeof(tr->y_label));

    tr->times = malloc(data->n_points * sizeof(double));
    tr->values = malloc(data->n_points * sizeof(float));
    tr->valid = malloc(data->n_points * sizeof(int));

    if (tr->times && tr->values && tr->valid) {
        memcpy(tr->times, data->times, data->n_points * sizeof(double));
        memcpy(tr->values, data->values, data->n_points * sizeof(float));
        memcpy(tr->valid, data->valid, data->n_points * sizeof(int));
        trace_valid[trace_head] = 1;
        if (trace_count < MAX_TRACES) trace_count++;
    } else {
        free(tr->times);
        free(tr->values);
        free(tr->valid);
        tr->times = NULL;
        tr->values = NULL;
        tr->valid = NULL;
        return;
    }

    trace_head = (trace_head + 1) % MAX_TRACES;
}

/* ========== Public API ========== */

void timeseries_popup_init(Widget parent, Display *dpy, XtAppContext app_ctx) {
    (void)app_ctx;
    ts_display = dpy;

    memset(traces, 0, sizeof(traces));
    memset(trace_valid, 0, sizeof(trace_valid));

    /* Create popup shell (non-modal) */
    ts_shell = XtVaCreatePopupShell(
        "Time Series",
        transientShellWidgetClass,
        parent,
        XtNwidth, PLOT_WIDTH,
        XtNheight, PLOT_HEIGHT + 40,
        XtNtitle, "Time Series",
        NULL);

    /* Form container */
    Widget form = XtVaCreateManagedWidget(
        "tsForm", formWidgetClass, ts_shell,
        XtNborderWidth, 0,
        NULL);

    /* Plot area */
    ts_plot_widget = XtVaCreateManagedWidget(
        "tsPlot", simpleWidgetClass, form,
        XtNwidth, PLOT_WIDTH,
        XtNheight, PLOT_HEIGHT,
        XtNborderWidth, 0,
        NULL);

    /* Close button */
    ts_close_btn = XtVaCreateManagedWidget(
        "Close", commandWidgetClass, form,
        XtNfromVert, ts_plot_widget,
        XtNwidth, 60,
        XtNhorizDistance, PLOT_WIDTH / 2 - 30,
        NULL);
    XtAddCallback(ts_close_btn, XtNcallback, ts_close_callback, NULL);

    /* Event handler for expose (redraw) */
    XtAddEventHandler(ts_plot_widget, ExposureMask, False, ts_expose_callback, NULL);
}

void timeseries_popup_show(const TSData *data) {
    if (!data || !ts_shell || !ts_plot_widget) return;

    /* Add trace to ring buffer */
    add_trace(data);

    /* Create GC if needed */
    if (ts_gc == None && XtIsRealized(ts_shell)) {
        ts_gc = XCreateGC(ts_display, XtWindow(ts_plot_widget), 0, NULL);
    }

    /* Update title */
    char title_buf[128];
    if (trace_count == 1) {
        snprintf(title_buf, sizeof(title_buf), "%s",
                 data->title[0] ? data->title : "Time Series");
    } else {
        snprintf(title_buf, sizeof(title_buf), "Time Series (%d traces)", trace_count);
    }
    XtVaSetValues(ts_shell, XtNtitle, title_buf, NULL);

    /* Show popup (non-modal) */
    XtPopup(ts_shell, XtGrabNone);

    /* Create GC after popup if not yet created */
    if (ts_gc == None) {
        ts_gc = XCreateGC(ts_display, XtWindow(ts_plot_widget), 0, NULL);
    }

    /* Force redraw */
    if (XtIsRealized(ts_plot_widget)) {
        XClearArea(ts_display, XtWindow(ts_plot_widget), 0, 0, 0, 0, True);
    }
}

void timeseries_popup_clear(void) {
    free_all_traces();
    /* Redraw empty plot if visible */
    if (ts_shell && ts_plot_widget && XtIsRealized(ts_plot_widget) && ts_gc != None) {
        Window win = XtWindow(ts_plot_widget);
        allocate_colors();
        XSetForeground(ts_display, ts_gc, color_bg);
        XFillRectangle(ts_display, win, ts_gc, 0, 0, PLOT_WIDTH, PLOT_HEIGHT);
        XFlush(ts_display);
    }
}

void timeseries_popup_cleanup(void) {
    free_all_traces();
    if (ts_gc != None && ts_display) {
        XFreeGC(ts_display, ts_gc);
        ts_gc = None;
    }
    ts_shell = NULL;
    ts_plot_widget = NULL;
    ts_close_btn = NULL;
    colors_allocated = 0;
}
