/*
 * x_interface.c - X11/Xaw display interface implementation
 */

#include "x_interface.h"
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Simple.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Global X11 resources */
static Display *display = NULL;
static Widget top_level = NULL;
static XtAppContext app_context;

/* Widgets */
static Widget main_form;
static Widget image_widget;
static Widget var_box;
static Widget control_box;
static Widget info_label;
static Widget time_label;
static Widget depth_label;
static Widget range_label;
static Widget cmap_label;
static Widget value_label;
static Widget time_scrollbar;
static Widget depth_scrollbar;

static Widget *var_buttons = NULL;
static int n_var_buttons = 0;

/* Image data */
static XImage *ximage = NULL;
static unsigned char *image_data = NULL;
static size_t image_width = 0;
static size_t image_height = 0;
static GC gc = None;

/* Callbacks */
static VarSelectCallback var_select_cb = NULL;
static TimeChangeCallback time_change_cb = NULL;
static DepthChangeCallback depth_change_cb = NULL;
static AnimationCallback animation_cb = NULL;
static ColormapCallback colormap_cb = NULL;

/* Animation state */
static XtIntervalId timer_id = 0;
static void (*timer_callback)(void) = NULL;

/* Current state */
static size_t current_n_times = 1;
static size_t current_n_depths = 1;

/* Forward declarations */
static void redraw_image(Widget w, XtPointer client_data, XtPointer call_data);

/* Mouse motion callback */
typedef void (*MouseMotionCallback)(int x, int y);
static MouseMotionCallback mouse_motion_cb = NULL;

/* Button callbacks */
static void var_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)call_data;
    int idx = (int)(intptr_t)client_data;
    if (var_select_cb) var_select_cb(idx);
}

static void rewind_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(-2);  /* -2 = rewind to start */
}

static void back_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(-1);
}

static void pause_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(0);
}

static void forward_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(1);
}

static void ffwd_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (animation_cb) animation_cb(2);  /* 2 = fast forward (continuous) */
}

static void cmap_button_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    if (colormap_cb) colormap_cb();
}

/* Scrollbar callbacks */
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

/* Timer callback wrapper */
static void timer_callback_wrapper(XtPointer client_data, XtIntervalId *id) {
    (void)client_data; (void)id;
    timer_id = 0;
    if (timer_callback) timer_callback();
}

/* Expose callback for image widget */
static void expose_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data; (void)cont;
    if (event->type == Expose && ximage && gc != None) {
        XPutImage(display, XtWindow(w), gc, ximage, 0, 0, 0, 0,
                  image_width, image_height);
    }
}

/* Motion callback for mouse position tracking */
static void motion_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)w; (void)client_data; (void)cont;
    if (event->type == MotionNotify && mouse_motion_cb) {
        mouse_motion_cb(event->xmotion.x, event->xmotion.y);
    }
}

int x_init(int *argc, char **argv) {
    /* Initialize Xt */
    top_level = XtVaAppInitialize(
        &app_context,
        "Ushow",
        NULL, 0,
        argc, argv,
        NULL,
        XtNwidth, 800,
        XtNheight, 600,
        NULL
    );

    if (!top_level) {
        fprintf(stderr, "Failed to initialize X toolkit\n");
        return -1;
    }

    display = XtDisplay(top_level);

    /* Create main form */
    main_form = XtVaCreateManagedWidget(
        "mainForm", formWidgetClass, top_level,
        NULL
    );

    /* Variable selector box (top) */
    var_box = XtVaCreateManagedWidget(
        "varBox", boxWidgetClass, main_form,
        XtNorientation, XtorientHorizontal,
        XtNtop, XtChainTop,
        XtNbottom, XtChainTop,
        XtNleft, XtChainLeft,
        XtNright, XtChainRight,
        NULL
    );

    /* Info label */
    info_label = XtVaCreateManagedWidget(
        "infoLabel", labelWidgetClass, main_form,
        XtNlabel, "No variable selected",
        XtNfromVert, var_box,
        XtNtop, XtChainTop,
        XtNbottom, XtChainTop,
        XtNleft, XtChainLeft,
        XtNborderWidth, 0,
        NULL
    );

    /* Range label */
    range_label = XtVaCreateManagedWidget(
        "rangeLabel", labelWidgetClass, main_form,
        XtNlabel, "Range: -",
        XtNfromVert, var_box,
        XtNfromHoriz, info_label,
        XtNtop, XtChainTop,
        XtNbottom, XtChainTop,
        XtNborderWidth, 0,
        NULL
    );

    /* Colormap label/button */
    cmap_label = XtVaCreateManagedWidget(
        "cmapBtn", commandWidgetClass, main_form,
        XtNlabel, "Colormap: jet",
        XtNfromVert, var_box,
        XtNfromHoriz, range_label,
        XtNtop, XtChainTop,
        XtNbottom, XtChainTop,
        NULL
    );
    XtAddCallback(cmap_label, XtNcallback, cmap_button_callback, NULL);

    /* Image display area */
    image_widget = XtVaCreateManagedWidget(
        "imageWidget", simpleWidgetClass, main_form,
        XtNwidth, 720,
        XtNheight, 360,
        XtNfromVert, info_label,
        XtNtop, XtChainTop,
        XtNbottom, XtChainBottom,
        XtNleft, XtChainLeft,
        XtNright, XtChainRight,
        NULL
    );
    XtAddEventHandler(image_widget, ExposureMask, False, expose_callback, NULL);
    XtAddEventHandler(image_widget, PointerMotionMask, False, motion_callback, NULL);

    /* Control box (bottom) */
    control_box = XtVaCreateManagedWidget(
        "controlBox", boxWidgetClass, main_form,
        XtNorientation, XtorientHorizontal,
        XtNfromVert, image_widget,
        XtNtop, XtChainBottom,
        XtNbottom, XtChainBottom,
        XtNleft, XtChainLeft,
        NULL
    );

    /* Animation buttons */
    Widget rewind_btn = XtVaCreateManagedWidget(
        "rewindBtn", commandWidgetClass, control_box,
        XtNlabel, "|<",
        NULL
    );
    XtAddCallback(rewind_btn, XtNcallback, rewind_button_callback, NULL);

    Widget back_btn = XtVaCreateManagedWidget(
        "backBtn", commandWidgetClass, control_box,
        XtNlabel, "<",
        NULL
    );
    XtAddCallback(back_btn, XtNcallback, back_button_callback, NULL);

    Widget pause_btn = XtVaCreateManagedWidget(
        "pauseBtn", commandWidgetClass, control_box,
        XtNlabel, "||",
        NULL
    );
    XtAddCallback(pause_btn, XtNcallback, pause_button_callback, NULL);

    Widget fwd_btn = XtVaCreateManagedWidget(
        "fwdBtn", commandWidgetClass, control_box,
        XtNlabel, ">",
        NULL
    );
    XtAddCallback(fwd_btn, XtNcallback, forward_button_callback, NULL);

    Widget ffwd_btn = XtVaCreateManagedWidget(
        "ffwdBtn", commandWidgetClass, control_box,
        XtNlabel, ">>",
        NULL
    );
    XtAddCallback(ffwd_btn, XtNcallback, ffwd_button_callback, NULL);

    /* Time controls */
    time_label = XtVaCreateManagedWidget(
        "timeLabel", labelWidgetClass, control_box,
        XtNlabel, "Time: 0/0",
        XtNborderWidth, 0,
        NULL
    );

    time_scrollbar = XtVaCreateManagedWidget(
        "timeScroll", scrollbarWidgetClass, control_box,
        XtNorientation, XtorientHorizontal,
        XtNlength, 150,
        XtNthickness, 15,
        NULL
    );
    XtAddCallback(time_scrollbar, XtNjumpProc, time_scroll_callback, NULL);

    /* Depth controls */
    depth_label = XtVaCreateManagedWidget(
        "depthLabel", labelWidgetClass, control_box,
        XtNlabel, "Depth: 0/0",
        XtNborderWidth, 0,
        NULL
    );

    depth_scrollbar = XtVaCreateManagedWidget(
        "depthScroll", scrollbarWidgetClass, control_box,
        XtNorientation, XtorientHorizontal,
        XtNlength, 100,
        XtNthickness, 15,
        NULL
    );
    XtAddCallback(depth_scrollbar, XtNjumpProc, depth_scroll_callback, NULL);

    /* Value display label (shows mouse position and data value) */
    value_label = XtVaCreateManagedWidget(
        "valueLabel", labelWidgetClass, control_box,
        XtNlabel, "                                        ",
        XtNborderWidth, 0,
        XtNwidth, 200,
        NULL
    );

    /* Realize the widget hierarchy */
    XtRealizeWidget(top_level);

    /* Create graphics context */
    gc = XCreateGC(display, XtWindow(image_widget), 0, NULL);

    return 0;
}

void x_set_var_callback(VarSelectCallback cb) { var_select_cb = cb; }
void x_set_time_callback(TimeChangeCallback cb) { time_change_cb = cb; }
void x_set_depth_callback(DepthChangeCallback cb) { depth_change_cb = cb; }
void x_set_animation_callback(AnimationCallback cb) { animation_cb = cb; }
void x_set_colormap_callback(ColormapCallback cb) { colormap_cb = cb; }
void x_set_mouse_callback(void (*cb)(int, int)) { mouse_motion_cb = cb; }

void x_setup_var_selector(const char **var_names, int n_vars) {
    /* Free old buttons */
    if (var_buttons) {
        for (int i = 0; i < n_var_buttons; i++) {
            XtDestroyWidget(var_buttons[i]);
        }
        free(var_buttons);
    }

    /* Create new buttons */
    var_buttons = malloc(n_vars * sizeof(Widget));
    n_var_buttons = n_vars;

    for (int i = 0; i < n_vars; i++) {
        var_buttons[i] = XtVaCreateManagedWidget(
            var_names[i], commandWidgetClass, var_box,
            XtNlabel, var_names[i],
            NULL
        );
        XtAddCallback(var_buttons[i], XtNcallback, var_button_callback,
                      (XtPointer)(intptr_t)i);
    }
}

void x_update_image(const unsigned char *pixels, size_t width, size_t height) {
    if (!display || !image_widget) return;

    /* Check if we need to recreate the image */
    if (width != image_width || height != image_height) {
        if (ximage) {
            XDestroyImage(ximage);
            ximage = NULL;
        }
        free(image_data);

        image_width = width;
        image_height = height;

        /* Resize widget */
        XtVaSetValues(image_widget,
                      XtNwidth, width,
                      XtNheight, height,
                      NULL);
    }

    /* Allocate image data buffer (32-bit aligned) */
    int depth = DefaultDepth(display, DefaultScreen(display));
    int bytes_per_pixel = (depth > 16) ? 4 : 2;
    size_t row_bytes = width * bytes_per_pixel;

    if (!image_data) {
        image_data = malloc(height * row_bytes);
    }

    /* Convert RGB to X11 format */
    Visual *visual = DefaultVisual(display, DefaultScreen(display));
    int screen = DefaultScreen(display);

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
                /* 16-bit color (5-6-5) */
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
                              (char *)image_data, width, height,
                              32, 0);
        if (!ximage) {
            fprintf(stderr, "Failed to create XImage\n");
            return;
        }
    }

    /* Draw to window */
    if (XtIsRealized(image_widget)) {
        XPutImage(display, XtWindow(image_widget), gc, ximage, 0, 0, 0, 0,
                  width, height);
        XFlush(display);
    }
}

void x_update_time(size_t time_idx, size_t n_times) {
    current_n_times = n_times;
    char buf[64];
    snprintf(buf, sizeof(buf), "Time: %zu/%zu", time_idx + 1, n_times);
    XtVaSetValues(time_label, XtNlabel, buf, NULL);

    /* Update scrollbar position */
    if (n_times > 1) {
        float pos = (float)time_idx / (n_times - 1);
        XawScrollbarSetThumb(time_scrollbar, pos, 1.0f / n_times);
    }
}

void x_update_depth(size_t depth_idx, size_t n_depths) {
    current_n_depths = n_depths;
    char buf[64];
    snprintf(buf, sizeof(buf), "Depth: %zu/%zu", depth_idx + 1, n_depths);
    XtVaSetValues(depth_label, XtNlabel, buf, NULL);

    /* Update scrollbar position */
    if (n_depths > 1) {
        float pos = (float)depth_idx / (n_depths - 1);
        XawScrollbarSetThumb(depth_scrollbar, pos, 1.0f / n_depths);
    }
}

void x_update_var_name(const char *name) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Variable: %s", name);
    XtVaSetValues(info_label, XtNlabel, buf, NULL);
}

void x_update_range_label(float min_val, float max_val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Range: [%.4g, %.4g]", min_val, max_val);
    XtVaSetValues(range_label, XtNlabel, buf, NULL);
}

void x_update_colormap_label(const char *name) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Colormap: %s", name);
    XtVaSetValues(cmap_label, XtNlabel, buf, NULL);
}

void x_update_value_label(double lon, double lat, float value) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Lon: %.2f  Lat: %.2f  Val: %.4g", lon, lat, value);
    XtVaSetValues(value_label, XtNlabel, buf, NULL);
}

void x_set_timer(int delay_ms, void (*callback)(void)) {
    if (timer_id) {
        XtRemoveTimeOut(timer_id);
    }
    timer_callback = callback;
    timer_id = XtAppAddTimeOut(app_context, delay_ms, timer_callback_wrapper, NULL);
}

void x_clear_timer(void) {
    if (timer_id) {
        XtRemoveTimeOut(timer_id);
        timer_id = 0;
    }
    timer_callback = NULL;
}

void x_main_loop(void) {
    XtAppMainLoop(app_context);
}

void x_cleanup(void) {
    x_clear_timer();

    if (ximage) {
        ximage->data = NULL;  /* Don't let XDestroyImage free our buffer */
        XDestroyImage(ximage);
        ximage = NULL;
    }
    free(image_data);
    image_data = NULL;

    if (gc != None) {
        XFreeGC(display, gc);
        gc = None;
    }

    free(var_buttons);
    var_buttons = NULL;
    n_var_buttons = 0;
}
