/*
 * timeseries_popup.c - Time series plot popup window
 *
 * Non-modal popup with custom XLib drawing that displays a time series
 * plot (value vs time) at a clicked spatial location.
 */

#include "timeseries_popup.h"
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

/* X11 handles */
static Display *ts_display = NULL;
static Widget ts_shell = NULL;
static Widget ts_plot_widget = NULL;
static Widget ts_close_btn = NULL;
static GC ts_gc = None;

/* Colors */
static unsigned long color_blue = 0;
static unsigned long color_gray = 0;
static int colors_allocated = 0;

/* Cached data (deep copy) */
static TSData ts_cache;
static int ts_cache_valid = 0;

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

static void allocate_colors(void) {
    if (colors_allocated || !ts_display) return;

    int screen = DefaultScreen(ts_display);
    Colormap cmap = DefaultColormap(ts_display, screen);
    XColor xc;

    /* Blue for data line */
    xc.red = 0x3333; xc.green = 0x6666; xc.blue = 0xFFFF;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(ts_display, cmap, &xc)) {
        color_blue = xc.pixel;
    } else {
        color_blue = BlackPixel(ts_display, screen);
    }

    /* Light gray for grid */
    xc.red = 0xCCCC; xc.green = 0xCCCC; xc.blue = 0xCCCC;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(ts_display, cmap, &xc)) {
        color_gray = xc.pixel;
    } else {
        color_gray = WhitePixel(ts_display, screen);
    }

    colors_allocated = 1;
}

/* ========== Drawing ========== */

static void draw_plot(Widget w) {
    if (!ts_cache_valid || !ts_display || ts_gc == None) return;
    if (!XtIsRealized(w)) return;

    Window win = XtWindow(w);
    int screen = DefaultScreen(ts_display);
    unsigned long black = BlackPixel(ts_display, screen);
    unsigned long white = WhitePixel(ts_display, screen);

    allocate_colors();

    /* Plot area dimensions */
    int plot_x0 = MARGIN_LEFT;
    int plot_y0 = MARGIN_TOP;
    int plot_x1 = PLOT_WIDTH - MARGIN_RIGHT;
    int plot_y1 = PLOT_HEIGHT - MARGIN_BOTTOM;
    int plot_w = plot_x1 - plot_x0;
    int plot_h = plot_y1 - plot_y0;

    /* White background */
    XSetForeground(ts_display, ts_gc, white);
    XFillRectangle(ts_display, win, ts_gc, 0, 0, PLOT_WIDTH, PLOT_HEIGHT);

    /* Compute data range for Y axis (valid values only) */
    double y_min = 1e30, y_max = -1e30;
    for (size_t i = 0; i < ts_cache.n_points; i++) {
        if (ts_cache.valid[i]) {
            double v = (double)ts_cache.values[i];
            if (v < y_min) y_min = v;
            if (v > y_max) y_max = v;
        }
    }
    if (y_min >= y_max) {
        y_min -= 0.5;
        y_max += 0.5;
    }

    /* X axis range */
    double x_min = ts_cache.times[0];
    double x_max = ts_cache.times[ts_cache.n_points - 1];
    if (x_min >= x_max) {
        x_min -= 0.5;
        x_max += 0.5;
    }

    /* Compute ticks */
    double y_tick_min, y_tick_max, y_tick_step;
    int n_y_ticks;
    compute_ticks(y_min, y_max, 8, &y_tick_min, &y_tick_max, &y_tick_step, &n_y_ticks);

    double x_tick_min, x_tick_max, x_tick_step;
    int n_x_ticks;
    compute_ticks(x_min, x_max, 6, &x_tick_min, &x_tick_max, &x_tick_step, &n_x_ticks);

    /* Use tick range for actual plot range */
    double range_y = y_tick_max - y_tick_min;
    double range_x = x_tick_max - x_tick_min;
    if (range_y <= 0) range_y = 1.0;
    if (range_x <= 0) range_x = 1.0;

    /* Check if CF time formatting is possible */
    int use_cf_time = (ts_cache.x_label[0] != '\0' && strstr(ts_cache.x_label, "since") != NULL);

    /* Draw grid lines (light gray) */
    XSetForeground(ts_display, ts_gc, color_gray);

    /* Y grid */
    for (int i = 0; i < n_y_ticks; i++) {
        double val = y_tick_min + i * y_tick_step;
        if (val > y_tick_max + y_tick_step * 0.01) break;
        int py = plot_y1 - (int)((val - y_tick_min) / range_y * plot_h);
        if (py >= plot_y0 && py <= plot_y1) {
            XDrawLine(ts_display, win, ts_gc, plot_x0, py, plot_x1, py);
        }
    }

    /* X grid */
    for (int i = 0; i < n_x_ticks; i++) {
        double val = x_tick_min + i * x_tick_step;
        if (val > x_tick_max + x_tick_step * 0.01) break;
        int px = plot_x0 + (int)((val - x_tick_min) / range_x * plot_w);
        if (px >= plot_x0 && px <= plot_x1) {
            XDrawLine(ts_display, win, ts_gc, px, plot_y0, px, plot_y1);
        }
    }

    /* Draw axes (black) */
    XSetForeground(ts_display, ts_gc, black);
    XDrawRectangle(ts_display, win, ts_gc, plot_x0, plot_y0, plot_w, plot_h);

    /* Y-axis tick labels */
    XFontStruct *font = XQueryFont(ts_display, XGContextFromGC(ts_gc));
    int font_ascent = font ? font->ascent : 10;

    for (int i = 0; i < n_y_ticks; i++) {
        double val = y_tick_min + i * y_tick_step;
        if (val > y_tick_max + y_tick_step * 0.01) break;
        int py = plot_y1 - (int)((val - y_tick_min) / range_y * plot_h);
        if (py < plot_y0 || py > plot_y1) continue;

        /* Tick mark */
        XDrawLine(ts_display, win, ts_gc, plot_x0 - TICK_LEN, py, plot_x0, py);

        /* Label */
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

        /* Tick mark */
        XDrawLine(ts_display, win, ts_gc, px, plot_y1, px, plot_y1 + TICK_LEN);

        /* Label */
        char buf[32];
        if (use_cf_time) {
            if (!ts_format_time(buf, sizeof(buf), val, ts_cache.x_label))
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
    if (ts_cache.x_label[0]) {
        const char *xlabel = use_cf_time ? "Date" : ts_cache.x_label;
        int tw = font ? XTextWidth(font, xlabel, (int)strlen(xlabel)) : 40;
        XDrawString(ts_display, win, ts_gc,
                    plot_x0 + plot_w / 2 - tw / 2,
                    PLOT_HEIGHT - 5,
                    xlabel, (int)strlen(xlabel));
    }

    /* Y-axis label (drawn horizontally at top-left) */
    if (ts_cache.y_label[0]) {
        XDrawString(ts_display, win, ts_gc,
                    4, plot_y0 - 8,
                    ts_cache.y_label, (int)strlen(ts_cache.y_label));
    }

    /* Title (centered at top) */
    if (ts_cache.title[0]) {
        int tw = font ? XTextWidth(font, ts_cache.title, (int)strlen(ts_cache.title)) : 100;
        XDrawString(ts_display, win, ts_gc,
                    PLOT_WIDTH / 2 - tw / 2,
                    font_ascent + 4,
                    ts_cache.title, (int)strlen(ts_cache.title));
    }

    /* Draw data line (blue, 2px thick) */
    XSetForeground(ts_display, ts_gc, color_blue);
    XSetLineAttributes(ts_display, ts_gc, 2, LineSolid, CapRound, JoinRound);

    int prev_px = -1, prev_py = -1;
    int prev_valid = 0;

    for (size_t i = 0; i < ts_cache.n_points; i++) {
        if (!ts_cache.valid[i]) {
            prev_valid = 0;
            continue;
        }

        double t = ts_cache.times[i];
        double v = (double)ts_cache.values[i];

        int px = plot_x0 + (int)((t - x_tick_min) / range_x * plot_w);
        int py = plot_y1 - (int)((v - y_tick_min) / range_y * plot_h);

        /* Clamp to plot area */
        if (px < plot_x0) px = plot_x0;
        if (px > plot_x1) px = plot_x1;
        if (py < plot_y0) py = plot_y0;
        if (py > plot_y1) py = plot_y1;

        /* Connect to previous valid point */
        if (prev_valid) {
            XDrawLine(ts_display, win, ts_gc, prev_px, prev_py, px, py);
        }

        /* Small dot at each valid data point */
        XFillArc(ts_display, win, ts_gc,
                 px - DOT_RADIUS, py - DOT_RADIUS,
                 DOT_RADIUS * 2, DOT_RADIUS * 2, 0, 360 * 64);

        prev_px = px;
        prev_py = py;
        prev_valid = 1;
    }

    /* Reset line width */
    XSetLineAttributes(ts_display, ts_gc, 0, LineSolid, CapButt, JoinMiter);
    XSetForeground(ts_display, ts_gc, black);

    if (font) {
        XFreeFontInfo(NULL, font, 1);
    }

    XFlush(ts_display);
}

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
    }
}

/* ========== Cache Management ========== */

static void free_cache(void) {
    if (ts_cache_valid) {
        free(ts_cache.times);
        free(ts_cache.values);
        free(ts_cache.valid);
        ts_cache.times = NULL;
        ts_cache.values = NULL;
        ts_cache.valid = NULL;
        ts_cache_valid = 0;
    }
}

static void copy_to_cache(const TSData *data) {
    free_cache();

    ts_cache.n_points = data->n_points;
    ts_cache.n_valid = data->n_valid;
    memcpy(ts_cache.title, data->title, sizeof(ts_cache.title));
    memcpy(ts_cache.x_label, data->x_label, sizeof(ts_cache.x_label));
    memcpy(ts_cache.y_label, data->y_label, sizeof(ts_cache.y_label));

    ts_cache.times = malloc(data->n_points * sizeof(double));
    ts_cache.values = malloc(data->n_points * sizeof(float));
    ts_cache.valid = malloc(data->n_points * sizeof(int));

    if (ts_cache.times && ts_cache.values && ts_cache.valid) {
        memcpy(ts_cache.times, data->times, data->n_points * sizeof(double));
        memcpy(ts_cache.values, data->values, data->n_points * sizeof(float));
        memcpy(ts_cache.valid, data->valid, data->n_points * sizeof(int));
        ts_cache_valid = 1;
    } else {
        free(ts_cache.times);
        free(ts_cache.values);
        free(ts_cache.valid);
        ts_cache.times = NULL;
        ts_cache.values = NULL;
        ts_cache.valid = NULL;
    }
}

/* ========== Public API ========== */

void timeseries_popup_init(Widget parent, Display *dpy, XtAppContext app_ctx) {
    (void)app_ctx;
    ts_display = dpy;

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

    /* Deep copy data */
    copy_to_cache(data);
    if (!ts_cache_valid) return;

    /* Create GC if needed */
    if (ts_gc == None && XtIsRealized(ts_shell)) {
        ts_gc = XCreateGC(ts_display, XtWindow(ts_plot_widget), 0, NULL);
    }

    /* Update title */
    XtVaSetValues(ts_shell, XtNtitle, data->title[0] ? data->title : "Time Series", NULL);

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

void timeseries_popup_cleanup(void) {
    free_cache();
    if (ts_gc != None && ts_display) {
        XFreeGC(ts_display, ts_gc);
        ts_gc = None;
    }
    ts_shell = NULL;
    ts_plot_widget = NULL;
    ts_close_btn = NULL;
    colors_allocated = 0;
}
