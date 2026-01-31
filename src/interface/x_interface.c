/*
 * x_interface.c - X11/Xaw display interface implementation
 *
 * Two-window architecture inspired by ncview:
 * - Control window: all controls, colorbar, variable selector
 * - Image window: separate popup for data display
 */

#include "x_interface.h"
#include "colorbar.h"
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Toggle.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Simple.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Layout constants (like ncview's app_data) */
#define LABEL_WIDTH    350
#define BUTTON_WIDTH   50
#define CBAR_HEIGHT    20

/* Global X11 resources */
static Display *display = NULL;
static XtAppContext app_context;

/* Main control window */
static Widget top_level = NULL;
static Widget main_form = NULL;

/* Labels (stacked vertically) */
static Widget label_title = NULL;
static Widget label_varname = NULL;
static Widget label_dims = NULL;
static Widget label_range = NULL;
static Widget label_value = NULL;

/* Button boxes */
static Widget buttonbox = NULL;
static Widget optionbox = NULL;

/* Individual control widgets */
static Widget cmap_button = NULL;
static Widget time_label = NULL;
static Widget depth_label = NULL;
static Widget time_scrollbar = NULL;
static Widget depth_scrollbar = NULL;

/* Colorbar */
static Widget colorbar_form = NULL;
static Widget colorbar_widget = NULL;

/* Variable selector */
static Widget varsel_form = NULL;
static Widget varsel_box = NULL;
static Widget *var_toggles = NULL;
static int n_var_toggles = 0;

/* Image popup window */
static Widget image_shell = NULL;
static Widget image_widget = NULL;

/* Image data */
static XImage *ximage = NULL;
static unsigned char *image_data = NULL;
static size_t image_width = 0;
static size_t image_height = 0;
static GC image_gc = None;

/* Colorbar data */
static XImage *cbar_ximage = NULL;
static unsigned char *cbar_image_data = NULL;
static GC cbar_gc = None;

/* Callbacks */
static VarSelectCallback var_select_cb = NULL;
static TimeChangeCallback time_change_cb = NULL;
static DepthChangeCallback depth_change_cb = NULL;
static AnimationCallback animation_cb = NULL;
static ColormapCallback colormap_cb = NULL;

typedef void (*MouseMotionCallback)(int x, int y);
static MouseMotionCallback mouse_motion_cb = NULL;

typedef void (*RangeAdjustCallback)(int action);
static RangeAdjustCallback range_adjust_cb = NULL;

typedef void (*ZoomCallback)(int delta);
static ZoomCallback zoom_cb = NULL;

typedef void (*SaveCallback)(void);
static SaveCallback save_cb = NULL;

/* State */
static size_t current_n_times = 1;
static size_t current_n_depths = 1;
static int current_var_index = -1;

/* ========== Button Callbacks ========== */

static void var_toggle_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)call_data;
    int idx = (int)(intptr_t)client_data;
    Boolean state;
    XtVaGetValues(w, XtNstate, &state, NULL);
    if (!state) return;
    current_var_index = idx;
    if (var_select_cb) var_select_cb(idx);
}

static void rewind_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(-2);
}

static void back_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(-1);
}

static void pause_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(0);
}

static void forward_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(1);
}

static void ffwd_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(2);
}

static void cmap_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (colormap_cb) colormap_cb();
}

static void min_down_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (range_adjust_cb) range_adjust_cb(0);
}

static void min_up_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (range_adjust_cb) range_adjust_cb(1);
}

static void max_down_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (range_adjust_cb) range_adjust_cb(2);
}

static void max_up_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (range_adjust_cb) range_adjust_cb(3);
}

static void zoom_in_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (zoom_cb) zoom_cb(1);
}

static void zoom_out_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (zoom_cb) zoom_cb(-1);
}

static void save_callback_fn(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (save_cb) save_cb();
}

static void time_scroll_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data;
    float percent = *(float *)call_data;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    size_t time_idx = (size_t)(percent * (current_n_times - 1) + 0.5f);
    if (time_change_cb) time_change_cb(time_idx);
}

static void depth_scroll_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data;
    float percent = *(float *)call_data;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    size_t depth_idx = (size_t)(percent * (current_n_depths - 1) + 0.5f);
    if (depth_change_cb) depth_change_cb(depth_idx);
}

/* ========== Event Handlers ========== */

static void image_expose_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data; (void)cont;
    if (event->type == Expose && ximage && image_gc != None) {
        XPutImage(display, XtWindow(w), image_gc, ximage, 0, 0, 0, 0,
                  image_width, image_height);
    }
}

static void image_motion_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)w; (void)client_data; (void)cont;
    if (event->type == MotionNotify && mouse_motion_cb) {
        mouse_motion_cb(event->xmotion.x, event->xmotion.y);
    }
}

static void colorbar_expose_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data; (void)cont;
    if (event->type == Expose && cbar_ximage && cbar_gc != None) {
        size_t cbar_width, cbar_height;
        colorbar_get_pixels(&cbar_width, &cbar_height);
        XPutImage(display, XtWindow(w), cbar_gc, cbar_ximage, 0, 0, 0, 0,
                  cbar_width, cbar_height);
    }
}

/* Timer support */
static XtIntervalId timer_id = 0;
static void (*timer_callback_fn)(void) = NULL;

static void timer_wrapper(XtPointer client_data, XtIntervalId *id) {
    (void)client_data; (void)id;
    timer_id = 0;
    if (timer_callback_fn) timer_callback_fn();
}

/* ========== Initialization ========== */

int x_init(int *argc, char **argv) {
    Widget btn;

    /* Initialize Xt */
    top_level = XtVaAppInitialize(
        &app_context,
        "Ushow",
        NULL, 0,
        argc, argv,
        NULL,
        XtNtitle, "ushow",
        NULL
    );

    if (!top_level) {
        fprintf(stderr, "Failed to initialize X toolkit\n");
        return -1;
    }

    display = XtDisplay(top_level);

    /* ===== Main Form (container for all control widgets) ===== */
    main_form = XtVaCreateManagedWidget(
        "mainForm", formWidgetClass, top_level,
        NULL
    );

    /* ===== Labels (stacked vertically, fixed width) ===== */

    /* Label 1: Title/filename */
    label_title = XtVaCreateManagedWidget(
        "labelTitle", labelWidgetClass, main_form,
        XtNlabel, "ushow - Unstructured Data Viewer",
        XtNwidth, LABEL_WIDTH,
        XtNborderWidth, 0,
        NULL
    );

    /* Label 2: Variable name */
    label_varname = XtVaCreateManagedWidget(
        "labelVarname", labelWidgetClass, main_form,
        XtNlabel, "Variable: (none)",
        XtNwidth, LABEL_WIDTH,
        XtNfromVert, label_title,
        XtNborderWidth, 0,
        NULL
    );

    /* Label 3: Dimensions (time/depth) */
    label_dims = XtVaCreateManagedWidget(
        "labelDims", labelWidgetClass, main_form,
        XtNlabel, "Time: -/-  Depth: -/-",
        XtNwidth, LABEL_WIDTH,
        XtNfromVert, label_varname,
        XtNborderWidth, 0,
        NULL
    );

    /* Label 4: Range */
    label_range = XtVaCreateManagedWidget(
        "labelRange", labelWidgetClass, main_form,
        XtNlabel, "Range: [-, -]",
        XtNwidth, LABEL_WIDTH,
        XtNfromVert, label_dims,
        XtNborderWidth, 0,
        NULL
    );

    /* Label 5: Value at cursor */
    label_value = XtVaCreateManagedWidget(
        "labelValue", labelWidgetClass, main_form,
        XtNlabel, "Lon: -  Lat: -  Val: -",
        XtNwidth, LABEL_WIDTH,
        XtNfromVert, label_range,
        XtNborderWidth, 0,
        NULL
    );

    /* ===== Button Box 1: Animation controls ===== */
    buttonbox = XtVaCreateManagedWidget(
        "buttonbox", boxWidgetClass, main_form,
        XtNorientation, XtorientHorizontal,
        XtNfromVert, label_value,
        NULL
    );

    btn = XtVaCreateManagedWidget("|<", commandWidgetClass, buttonbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, rewind_callback, NULL);

    btn = XtVaCreateManagedWidget("<", commandWidgetClass, buttonbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, back_callback, NULL);

    btn = XtVaCreateManagedWidget("||", commandWidgetClass, buttonbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, pause_callback, NULL);

    btn = XtVaCreateManagedWidget(">", commandWidgetClass, buttonbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, forward_callback, NULL);

    btn = XtVaCreateManagedWidget(">>", commandWidgetClass, buttonbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, ffwd_callback, NULL);

    /* Time slider */
    time_label = XtVaCreateManagedWidget(
        "T:", labelWidgetClass, buttonbox,
        XtNborderWidth, 0,
        NULL
    );

    time_scrollbar = XtVaCreateManagedWidget(
        "timeScroll", scrollbarWidgetClass, buttonbox,
        XtNorientation, XtorientHorizontal,
        XtNlength, 80,
        XtNthickness, 15,
        NULL
    );
    XtAddCallback(time_scrollbar, XtNjumpProc, time_scroll_callback, NULL);

    /* Depth slider */
    depth_label = XtVaCreateManagedWidget(
        "D:", labelWidgetClass, buttonbox,
        XtNborderWidth, 0,
        NULL
    );

    depth_scrollbar = XtVaCreateManagedWidget(
        "depthScroll", scrollbarWidgetClass, buttonbox,
        XtNorientation, XtorientHorizontal,
        XtNlength, 60,
        XtNthickness, 15,
        NULL
    );
    XtAddCallback(depth_scrollbar, XtNjumpProc, depth_scroll_callback, NULL);

    /* ===== Button Box 2: Options ===== */
    optionbox = XtVaCreateManagedWidget(
        "optionbox", boxWidgetClass, main_form,
        XtNorientation, XtorientHorizontal,
        XtNfromVert, buttonbox,
        NULL
    );

    cmap_button = XtVaCreateManagedWidget(
        "Cmap", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH + 20,
        NULL
    );
    XtAddCallback(cmap_button, XtNcallback, cmap_callback, NULL);

    btn = XtVaCreateManagedWidget("Min-", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, min_down_callback, NULL);

    btn = XtVaCreateManagedWidget("Min+", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, min_up_callback, NULL);

    btn = XtVaCreateManagedWidget("Max-", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, max_down_callback, NULL);

    btn = XtVaCreateManagedWidget("Max+", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, max_up_callback, NULL);

    btn = XtVaCreateManagedWidget("Mag-", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, zoom_out_callback, NULL);

    btn = XtVaCreateManagedWidget("Mag+", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, zoom_in_callback, NULL);

    btn = XtVaCreateManagedWidget("Save", commandWidgetClass, optionbox,
        XtNwidth, BUTTON_WIDTH, NULL);
    XtAddCallback(btn, XtNcallback, save_callback_fn, NULL);

    /* ===== Colorbar ===== */
    colorbar_form = XtVaCreateManagedWidget(
        "colorbarForm", formWidgetClass, main_form,
        XtNfromVert, optionbox,
        NULL
    );

    colorbar_widget = XtVaCreateManagedWidget(
        "colorbar", simpleWidgetClass, colorbar_form,
        XtNwidth, LABEL_WIDTH,
        XtNheight, CBAR_HEIGHT,
        XtNborderWidth, 1,
        NULL
    );

    /* ===== Variable Selector ===== */
    varsel_form = XtVaCreateManagedWidget(
        "varselForm", formWidgetClass, main_form,
        XtNfromVert, colorbar_form,
        NULL
    );

    varsel_box = XtVaCreateManagedWidget(
        "varselBox", boxWidgetClass, varsel_form,
        XtNorientation, XtorientHorizontal,
        NULL
    );

    /* ===== Image Popup Window ===== */
    image_shell = XtVaCreatePopupShell(
        "imageWindow",
        topLevelShellWidgetClass,
        top_level,
        XtNtitle, "ushow",
        XtNwidth, 720,
        XtNheight, 360,
        NULL
    );

    image_widget = XtVaCreateManagedWidget(
        "imageWidget", simpleWidgetClass, image_shell,
        XtNwidth, 720,
        XtNheight, 360,
        NULL
    );

    /* Realize main window */
    XtRealizeWidget(top_level);

    /* Create colorbar GC and initialize */
    cbar_gc = XCreateGC(display, XtWindow(colorbar_widget), 0, NULL);
    colorbar_init(LABEL_WIDTH);
    XtAddEventHandler(colorbar_widget, ExposureMask, False, colorbar_expose_callback, NULL);

    /* Pop up the image window */
    XtPopup(image_shell, XtGrabNone);

    /* Create image GC */
    image_gc = XCreateGC(display, XtWindow(image_widget), 0, NULL);
    XtAddEventHandler(image_widget, ExposureMask, False, image_expose_callback, NULL);
    XtAddEventHandler(image_widget, PointerMotionMask, False, image_motion_callback, NULL);

    return 0;
}

/* ========== Callback Setters ========== */

void x_set_var_callback(VarSelectCallback cb) { var_select_cb = cb; }
void x_set_time_callback(TimeChangeCallback cb) { time_change_cb = cb; }
void x_set_depth_callback(DepthChangeCallback cb) { depth_change_cb = cb; }
void x_set_animation_callback(AnimationCallback cb) { animation_cb = cb; }
void x_set_colormap_callback(ColormapCallback cb) { colormap_cb = cb; }
void x_set_mouse_callback(void (*cb)(int, int)) { mouse_motion_cb = cb; }
void x_set_range_callback(void (*cb)(int)) { range_adjust_cb = cb; }
void x_set_zoom_callback(void (*cb)(int)) { zoom_cb = cb; }
void x_set_save_callback(void (*cb)(void)) { save_cb = cb; }

/* ========== Variable Selector ========== */

void x_setup_var_selector(const char **var_names, int n_vars) {
    /* Free old toggles */
    if (var_toggles) {
        for (int i = 0; i < n_var_toggles; i++) {
            XtDestroyWidget(var_toggles[i]);
        }
        free(var_toggles);
        var_toggles = NULL;
    }

    if (n_vars == 0) {
        n_var_toggles = 0;
        return;
    }

    /* Create toggle buttons in radio group */
    var_toggles = malloc(n_vars * sizeof(Widget));
    n_var_toggles = n_vars;

    for (int i = 0; i < n_vars; i++) {
        var_toggles[i] = XtVaCreateManagedWidget(
            var_names[i],
            toggleWidgetClass,
            varsel_box,
            XtNlabel, var_names[i],
            XtNstate, (i == 0) ? True : False,
            XtNradioGroup, (i > 0) ? var_toggles[0] : NULL,
            NULL
        );
        XtAddCallback(var_toggles[i], XtNcallback, var_toggle_callback,
                      (XtPointer)(intptr_t)i);
    }

    current_var_index = 0;
}

/* ========== Update Functions ========== */

void x_update_image(const unsigned char *pixels, size_t width, size_t height) {
    if (!display || !image_widget) return;

    /* Check if we need to recreate the image */
    if (width != image_width || height != image_height) {
        if (ximage) {
            ximage->data = NULL;
            XDestroyImage(ximage);
            ximage = NULL;
        }
        free(image_data);
        image_data = NULL;

        image_width = width;
        image_height = height;

        /* Resize widget and shell */
        XtVaSetValues(image_widget, XtNwidth, width, XtNheight, height, NULL);
        XtVaSetValues(image_shell, XtNwidth, width, XtNheight, height, NULL);
    }

    /* Allocate image data buffer */
    int depth = DefaultDepth(display, DefaultScreen(display));
    int bytes_per_pixel = (depth > 16) ? 4 : 2;
    size_t row_bytes = width * bytes_per_pixel;

    if (!image_data) {
        image_data = malloc(height * row_bytes);
    }

    /* Convert RGB to X11 format */
    Visual *visual = DefaultVisual(display, DefaultScreen(display));

    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t src_idx = (y * width + x) * 3;
            unsigned char r = pixels[src_idx + 0];
            unsigned char g = pixels[src_idx + 1];
            unsigned char b = pixels[src_idx + 2];

            unsigned long pixel;
            if (depth >= 24) {
                pixel = ((unsigned long)r << 16) | ((unsigned long)g << 8) | b;
            } else {
                pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }

            size_t dst_idx = y * row_bytes + x * bytes_per_pixel;
            if (bytes_per_pixel == 4) {
                *(uint32_t *)(image_data + dst_idx) = pixel;
            } else {
                *(uint16_t *)(image_data + dst_idx) = pixel;
            }
        }
    }

    /* Create XImage */
    if (!ximage) {
        ximage = XCreateImage(display, visual, depth, ZPixmap, 0,
                              (char *)image_data, width, height, 32, 0);
    }

    /* Draw to window */
    if (ximage && XtIsRealized(image_widget)) {
        XPutImage(display, XtWindow(image_widget), image_gc, ximage, 0, 0, 0, 0,
                  width, height);
        XFlush(display);
    }
}

void x_update_time(size_t time_idx, size_t n_times) {
    current_n_times = n_times;
    /* Update combined dims label */
    char buf[128];
    snprintf(buf, sizeof(buf), "Time: %zu/%zu", time_idx + 1, n_times);
    /* We'll update this in the combined label update below */

    if (n_times > 1) {
        float pos = (float)time_idx / (n_times - 1);
        XawScrollbarSetThumb(time_scrollbar, pos, 1.0f / n_times);
    }
}

void x_update_depth(size_t depth_idx, size_t n_depths) {
    current_n_depths = n_depths;

    if (n_depths > 1) {
        float pos = (float)depth_idx / (n_depths - 1);
        XawScrollbarSetThumb(depth_scrollbar, pos, 1.0f / n_depths);
    }
}

void x_update_var_name(const char *name) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Variable: %s", name);
    XtVaSetValues(label_varname, XtNlabel, buf, NULL);
}

void x_update_range_label(float min_val, float max_val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Range: [%.4g, %.4g]", min_val, max_val);
    XtVaSetValues(label_range, XtNlabel, buf, NULL);
}

void x_update_colormap_label(const char *name) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", name);
    XtVaSetValues(cmap_button, XtNlabel, buf, NULL);
}

void x_update_value_label(double lon, double lat, float value) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Lon: %.2f  Lat: %.2f  Val: %.4g", lon, lat, value);
    XtVaSetValues(label_value, XtNlabel, buf, NULL);
}

void x_update_colorbar(float min_val, float max_val, size_t width) {
    (void)min_val; (void)max_val;
    if (!display || !colorbar_widget) return;

    colorbar_init(width);
    colorbar_render();

    size_t cbar_width, cbar_height;
    unsigned char *cbar_pixels = colorbar_get_pixels(&cbar_width, &cbar_height);
    if (!cbar_pixels) return;

    XtVaSetValues(colorbar_widget, XtNwidth, cbar_width, XtNheight, cbar_height, NULL);

    if (cbar_ximage) {
        cbar_ximage->data = NULL;
        XDestroyImage(cbar_ximage);
        cbar_ximage = NULL;
    }
    free(cbar_image_data);
    cbar_image_data = NULL;

    int depth = DefaultDepth(display, DefaultScreen(display));
    int bytes_per_pixel = (depth > 16) ? 4 : 2;
    size_t row_bytes = cbar_width * bytes_per_pixel;

    cbar_image_data = malloc(cbar_height * row_bytes);
    if (!cbar_image_data) return;

    for (size_t y = 0; y < cbar_height; y++) {
        for (size_t x = 0; x < cbar_width; x++) {
            size_t src_idx = (y * cbar_width + x) * 3;
            unsigned char r = cbar_pixels[src_idx + 0];
            unsigned char g = cbar_pixels[src_idx + 1];
            unsigned char b = cbar_pixels[src_idx + 2];

            unsigned long pixel;
            if (depth >= 24) {
                pixel = ((unsigned long)r << 16) | ((unsigned long)g << 8) | b;
            } else {
                pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }

            size_t dst_idx = y * row_bytes + x * bytes_per_pixel;
            if (bytes_per_pixel == 4) {
                *(uint32_t *)(cbar_image_data + dst_idx) = pixel;
            } else {
                *(uint16_t *)(cbar_image_data + dst_idx) = pixel;
            }
        }
    }

    Visual *visual = DefaultVisual(display, DefaultScreen(display));
    cbar_ximage = XCreateImage(display, visual, depth, ZPixmap, 0,
                               (char *)cbar_image_data, cbar_width, cbar_height, 32, 0);

    if (cbar_ximage && XtIsRealized(colorbar_widget)) {
        XPutImage(display, XtWindow(colorbar_widget), cbar_gc, cbar_ximage,
                  0, 0, 0, 0, cbar_width, cbar_height);
        XFlush(display);
    }
}

/* ========== Timer Functions ========== */

void x_set_timer(int delay_ms, void (*callback)(void)) {
    if (timer_id) XtRemoveTimeOut(timer_id);
    timer_callback_fn = callback;
    timer_id = XtAppAddTimeOut(app_context, delay_ms, timer_wrapper, NULL);
}

void x_clear_timer(void) {
    if (timer_id) {
        XtRemoveTimeOut(timer_id);
        timer_id = 0;
    }
    timer_callback_fn = NULL;
}

void x_main_loop(void) {
    XtAppMainLoop(app_context);
}

void x_cleanup(void) {
    x_clear_timer();

    if (ximage) {
        ximage->data = NULL;
        XDestroyImage(ximage);
    }
    free(image_data);

    if (image_gc != None) XFreeGC(display, image_gc);

    if (cbar_ximage) {
        cbar_ximage->data = NULL;
        XDestroyImage(cbar_ximage);
    }
    free(cbar_image_data);

    if (cbar_gc != None) XFreeGC(display, cbar_gc);

    colorbar_cleanup();
    free(var_toggles);
}
