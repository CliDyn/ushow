/*
 * x_interface.c - X11/Xaw display interface implementation
 *
 * Two-window architecture inspired by ncview:
 * - Control window: all controls, colorbar, variable selector
 * - Image window: separate popup for data display
 */

#include "x_interface.h"
#include "colorbar.h"
#include <X11/Xlib.h>
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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Layout constants (like ncview's app_data) */
#define LABEL_WIDTH      350
#define BUTTON_WIDTH     50
#define CBAR_HEIGHT      16
#define CBAR_LABEL_HEIGHT 14
#define CBAR_PAD         2
#define DIM_COL_WIDTH    50
#define NAME_COL_WIDTH   80
#define VALUE_COL_WIDTH  80
#define UNITS_COL_WIDTH  80

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
static Widget varsel_form = NULL;      /* Container for variable selector */
static Widget *varsel_boxes = NULL;    /* Array of box widgets for variable rows */
static int n_varsel_boxes = 0;
static Widget *var_toggles = NULL;
static int n_var_toggles = 0;
#define VARS_PER_ROW 5
#define VAR_BUTTON_WIDTH 65

/* Dimension info panel */
static Widget diminfo_form = NULL;
static Widget diminfo_labels_row = NULL;  /* Header row: Dim: Name: Min: Current: Max: Units: */
static Widget *diminfo_rows = NULL;       /* One row per dimension */
static Widget *diminfo_dim_labels = NULL;
static Widget *diminfo_name_labels = NULL;
static Widget *diminfo_min_labels = NULL;
static Widget *diminfo_cur_buttons = NULL;  /* Clickable buttons for navigation */
static Widget *diminfo_max_labels = NULL;
static Widget *diminfo_units_labels = NULL;
static int n_diminfo_rows = 0;

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
static float cbar_min_val = 0.0f;
static float cbar_max_val = 1.0f;

/* Callbacks */
static VarSelectCallback var_select_cb = NULL;
static TimeChangeCallback time_change_cb = NULL;
static DepthChangeCallback depth_change_cb = NULL;
static AnimationCallback animation_cb = NULL;
static ColormapCallback colormap_cb = NULL;
static ColormapCallback colormap_back_cb = NULL;

typedef void (*MouseMotionCallback)(int x, int y);
static MouseMotionCallback mouse_motion_cb = NULL;

typedef void (*RangeAdjustCallback)(int action);
static RangeAdjustCallback range_adjust_cb = NULL;

typedef void (*ZoomCallback)(int delta);
static ZoomCallback zoom_cb = NULL;

typedef void (*SaveCallback)(void);
static SaveCallback save_cb = NULL;

typedef void (*DimNavCallback)(int dim_index, int direction);
static DimNavCallback dim_nav_cb = NULL;

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

static void cmap_back_callback(Widget w, XEvent *event, String *params, Cardinal *num_params) {
    (void)w; (void)event; (void)params; (void)num_params;
    if (colormap_back_cb) colormap_back_cb();
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

/* Dimension current button click - advance forward */
static void diminfo_cur_forward_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)call_data;
    int dim_idx = (int)(intptr_t)client_data;
    if (dim_nav_cb) dim_nav_cb(dim_idx, 1);
}

/* Dimension current button right-click - go backward */
static void diminfo_cur_backward_action(Widget w, XEvent *event, String *params, Cardinal *num_params) {
    (void)event; (void)params; (void)num_params;
    /* Find which dimension this button belongs to */
    for (int i = 0; i < n_diminfo_rows; i++) {
        if (diminfo_cur_buttons && diminfo_cur_buttons[i] == w) {
            if (dim_nav_cb) dim_nav_cb(i, -1);
            return;
        }
    }
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

static void draw_colorbar(Widget w) {
    if (!cbar_ximage || cbar_gc == None) return;

    size_t cbar_width, cbar_height;
    colorbar_get_pixels(&cbar_width, &cbar_height);
    Dimension widget_w = 0, widget_h = 0;
    XtVaGetValues(w, XtNwidth, &widget_w, XtNheight, &widget_h, NULL);

    /* Black background */
    XSetForeground(display, cbar_gc, BlackPixel(display, DefaultScreen(display)));
    XFillRectangle(display, XtWindow(w), cbar_gc, 0, 0, widget_w, widget_h);

    /* Draw colorbar at top */
    XPutImage(display, XtWindow(w), cbar_gc, cbar_ximage, 0, 0, 0, 0,
              cbar_width, cbar_height);

    /* Draw labels below the colorbar in white */
    XSetForeground(display, cbar_gc, WhitePixel(display, DefaultScreen(display)));
    XFontStruct *font = XQueryFont(display, XGContextFromGC(cbar_gc));
    int ascent = font ? font->ascent : 10;

    const int n_labels = 5;
    for (int i = 0; i < n_labels; i++) {
        float t = (float)i / (float)(n_labels - 1);
        float val = cbar_min_val + t * (cbar_max_val - cbar_min_val);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g", val);

        int text_w = font ? XTextWidth(font, buf, (int)strlen(buf)) : 0;
        int x = (int)(t * (float)(cbar_width - 1)) - text_w / 2;
        if (x < 2) x = 2;
        if (x > (int)cbar_width - text_w - 2) x = (int)cbar_width - text_w - 2;

        int y = (int)cbar_height + CBAR_PAD + ascent;
        XDrawString(display, XtWindow(w), cbar_gc, x, y, buf, (int)strlen(buf));
    }

    if (font) {
        XFreeFontInfo(NULL, font, 1);
    }
}

static void colorbar_expose_callback(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data; (void)cont;
    if (event->type == Expose && XtIsRealized(w)) {
        draw_colorbar(w);
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

/* Action procedure for backward navigation */
static void diminfo_cur_backward_action_proc(Widget w, XEvent *event, String *params, Cardinal *num_params) {
    diminfo_cur_backward_action(w, event, params, num_params);
}

static XtActionsRec actions[] = {
    {"diminfo_cur_backward", diminfo_cur_backward_action_proc},
    {"cmap_back", cmap_back_callback},
};

int x_init(int *argc, char **argv, const char **var_names, int n_vars,
           const USDimInfo *dims, int n_dims) {
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

    /* Register custom actions */
    XtAppAddActions(app_context, actions, XtNumber(actions));

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

    /* Label 4: Value at cursor */
    label_value = XtVaCreateManagedWidget(
        "labelValue", labelWidgetClass, main_form,
        XtNlabel, "Lon: -  Lat: -  Val: -",
        XtNwidth, LABEL_WIDTH,
        XtNfromVert, label_dims,
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
    XtAugmentTranslations(cmap_button,
        XtParseTranslationTable("<Btn3Down>,<Btn3Up>: cmap_back()"));

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
        XtNheight, CBAR_HEIGHT + CBAR_LABEL_HEIGHT + CBAR_PAD,
        XtNborderWidth, 1,
        NULL
    );

    /* ===== Variable Selector ===== */
    varsel_form = XtVaCreateManagedWidget(
        "varselForm", boxWidgetClass, main_form,
        XtNfromVert, colorbar_form,
        XtNorientation, XtorientVertical,
        XtNwidth, LABEL_WIDTH,
        XtNresizable, True,
        NULL
    );
    /* Variable toggle buttons are created before realization */
    x_setup_var_selector(var_names, n_vars);

    /* ===== Dimension Info Panel ===== */
    diminfo_form = XtVaCreateManagedWidget(
        "diminfoForm", boxWidgetClass, main_form,
        XtNfromVert, varsel_form,
        XtNorientation, XtorientVertical,
        XtNwidth, LABEL_WIDTH,
        XtNresizable, True,
        NULL
    );

    /* Create header row: Dim: Name: Min: Current: Max: Units: */
    diminfo_labels_row = XtVaCreateManagedWidget(
        "diminfoLabelsRow", boxWidgetClass, diminfo_form,
        XtNorientation, XtorientHorizontal,
        XtNwidth, LABEL_WIDTH,
        NULL
    );

    XtVaCreateManagedWidget(
        "Dim:", labelWidgetClass, diminfo_labels_row,
        XtNwidth, DIM_COL_WIDTH,
        XtNresize, False,
        XtNinternalWidth, 0,
        XtNinternalHeight, 0,
        XtNborderWidth, 0,
        NULL
    );

    XtVaCreateManagedWidget(
        "Name:", labelWidgetClass, diminfo_labels_row,
        XtNwidth, NAME_COL_WIDTH,
        XtNresize, False,
        XtNinternalWidth, 0,
        XtNinternalHeight, 0,
        XtNborderWidth, 0,
        NULL
    );

    XtVaCreateManagedWidget(
        "Min:", labelWidgetClass, diminfo_labels_row,
        XtNwidth, VALUE_COL_WIDTH,
        XtNresize, False,
        XtNinternalWidth, 0,
        XtNinternalHeight, 0,
        XtNborderWidth, 0,
        NULL
    );

    XtVaCreateManagedWidget(
        "Current:", labelWidgetClass, diminfo_labels_row,
        XtNwidth, VALUE_COL_WIDTH,
        XtNresize, False,
        XtNinternalWidth, 0,
        XtNinternalHeight, 0,
        XtNborderWidth, 0,
        NULL
    );

    XtVaCreateManagedWidget(
        "Max:", labelWidgetClass, diminfo_labels_row,
        XtNwidth, VALUE_COL_WIDTH,
        XtNresize, False,
        XtNinternalWidth, 0,
        XtNinternalHeight, 0,
        XtNborderWidth, 0,
        NULL
    );

    XtVaCreateManagedWidget(
        "Units:", labelWidgetClass, diminfo_labels_row,
        XtNwidth, UNITS_COL_WIDTH,
        XtNresize, False,
        XtNinternalWidth, 0,
        XtNinternalHeight, 0,
        XtNborderWidth, 0,
        NULL
    );

    /* Create initial dimension info rows before realization */
    x_setup_dim_info(dims, n_dims);

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
    colorbar_init(LABEL_WIDTH, CBAR_HEIGHT);
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
void x_set_colormap_back_callback(ColormapCallback cb) { colormap_back_cb = cb; }
void x_set_mouse_callback(void (*cb)(int, int)) { mouse_motion_cb = cb; }
void x_set_range_callback(void (*cb)(int)) { range_adjust_cb = cb; }
void x_set_zoom_callback(void (*cb)(int)) { zoom_cb = cb; }
void x_set_save_callback(void (*cb)(void)) { save_cb = cb; }
void x_set_dim_nav_callback(DimNavCallback cb) { dim_nav_cb = cb; }

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

    /* Free old boxes */
    if (varsel_boxes) {
        for (int i = 0; i < n_varsel_boxes; i++) {
            XtDestroyWidget(varsel_boxes[i]);
        }
        free(varsel_boxes);
        varsel_boxes = NULL;
    }

    if (n_vars == 0 || !varsel_form) {
        n_var_toggles = 0;
        n_varsel_boxes = 0;
        return;
    }

    /* Calculate number of rows needed */
    int num_boxes = (n_vars + VARS_PER_ROW - 1) / VARS_PER_ROW;
    varsel_boxes = malloc(num_boxes * sizeof(Widget));
    n_varsel_boxes = num_boxes;

    /* Create toggle buttons in radio group, arranged in rows */
    var_toggles = malloc(n_vars * sizeof(Widget));
    n_var_toggles = n_vars;

    int current_box = -1;
    for (int i = 0; i < n_vars; i++) {
        /* Create new box row if needed */
        if (i % VARS_PER_ROW == 0) {
            current_box++;
            char box_name[32];
            snprintf(box_name, sizeof(box_name), "varselBox%d", current_box);

            varsel_boxes[current_box] = XtVaCreateManagedWidget(
                box_name, boxWidgetClass, varsel_form,
                XtNorientation, XtorientHorizontal,
                XtNwidth, LABEL_WIDTH,
                XtNborderWidth, 0,
                NULL
            );

            /* Add "Var:" label to first row only */
            if (current_box == 0) {
                XtVaCreateManagedWidget(
                    "Var:", labelWidgetClass, varsel_boxes[current_box],
                    XtNwidth, 35,
                    XtNborderWidth, 0,
                    NULL
                );
            }
        }

        /* Create toggle button */
        var_toggles[i] = XtVaCreateManagedWidget(
            var_names[i],
            toggleWidgetClass,
            varsel_boxes[current_box],
            XtNlabel, var_names[i],
            XtNwidth, VAR_BUTTON_WIDTH,
            XtNstate, (i == 0) ? True : False,
            XtNradioGroup, (i > 0) ? var_toggles[0] : NULL,
            NULL
        );
        XtAddCallback(var_toggles[i], XtNcallback, var_toggle_callback,
                      (XtPointer)(intptr_t)i);
    }

    /* Handle single variable case - needs to be its own radio group */
    if (n_vars == 1) {
        XtVaSetValues(var_toggles[0], XtNradioGroup, var_toggles[0], NULL);
    }

    current_var_index = 0;
}

/* ========== Dimension Info Panel ========== */

static void x_clear_dim_info(void) {
    /* Destroy existing dimension rows (in reverse order for safety) */
    if (diminfo_rows) {
        for (int i = n_diminfo_rows - 1; i >= 0; i--) {
            if (diminfo_rows[i]) {
                XtDestroyWidget(diminfo_rows[i]);
            }
        }
        free(diminfo_rows);
        diminfo_rows = NULL;
    }
    free(diminfo_dim_labels);
    diminfo_dim_labels = NULL;
    free(diminfo_name_labels);
    diminfo_name_labels = NULL;
    free(diminfo_min_labels);
    diminfo_min_labels = NULL;
    free(diminfo_cur_buttons);
    diminfo_cur_buttons = NULL;
    free(diminfo_max_labels);
    diminfo_max_labels = NULL;
    free(diminfo_units_labels);
    diminfo_units_labels = NULL;
    n_diminfo_rows = 0;
}

void x_setup_dim_info(const USDimInfo *dims, int n_dims) {
    if (!diminfo_form || !diminfo_labels_row) return;

    x_clear_dim_info();

    if (n_dims <= 0 || !dims) return;

    /* Allocate widget arrays */
    diminfo_rows = calloc(n_dims, sizeof(Widget));
    diminfo_dim_labels = calloc(n_dims, sizeof(Widget));
    diminfo_name_labels = calloc(n_dims, sizeof(Widget));
    diminfo_min_labels = calloc(n_dims, sizeof(Widget));
    diminfo_cur_buttons = calloc(n_dims, sizeof(Widget));
    diminfo_max_labels = calloc(n_dims, sizeof(Widget));
    diminfo_units_labels = calloc(n_dims, sizeof(Widget));
    n_diminfo_rows = n_dims;

    if (!diminfo_rows || !diminfo_dim_labels || !diminfo_name_labels ||
        !diminfo_min_labels || !diminfo_cur_buttons || !diminfo_max_labels ||
        !diminfo_units_labels) {
        x_clear_dim_info();
        return;
    }

    /* Create a row for each dimension */
    for (int i = 0; i < n_dims; i++) {
        const USDimInfo *di = &dims[i];
        char buf[64];

        /* Create row box widget */
        diminfo_rows[i] = XtVaCreateManagedWidget(
            "diminfoRow", boxWidgetClass, diminfo_form,
            XtNorientation, XtorientHorizontal,
            XtNwidth, LABEL_WIDTH,
            NULL
        );

        /* Dimension number label */
        snprintf(buf, sizeof(buf), "%d", i);
        diminfo_dim_labels[i] = XtVaCreateManagedWidget(
            "diminfoDim", labelWidgetClass, diminfo_rows[i],
            XtNlabel, buf,
            XtNwidth, DIM_COL_WIDTH,
            XtNresize, False,
            XtNinternalWidth, 0,
            XtNinternalHeight, 0,
            XtNborderWidth, 0,
            NULL
        );

        /* Dimension name label */
        diminfo_name_labels[i] = XtVaCreateManagedWidget(
            "diminfoName", labelWidgetClass, diminfo_rows[i],
            XtNlabel, di->name,
            XtNwidth, NAME_COL_WIDTH,
            XtNresize, False,
            XtNinternalWidth, 0,
            XtNinternalHeight, 0,
            XtNborderWidth, 0,
            NULL
        );

        /* Min value label */
        snprintf(buf, sizeof(buf), "%.4g", di->min_val);
        diminfo_min_labels[i] = XtVaCreateManagedWidget(
            "diminfoMin", labelWidgetClass, diminfo_rows[i],
            XtNlabel, buf,
            XtNwidth, VALUE_COL_WIDTH,
            XtNresize, False,
            XtNinternalWidth, 0,
            XtNinternalHeight, 0,
            XtNborderWidth, 0,
            NULL
        );

        /* Current value button (clickable) */
        if (di->values && di->size > 0) {
            snprintf(buf, sizeof(buf), "%.4g", di->values[di->current]);
        } else {
            snprintf(buf, sizeof(buf), "%zu", di->current);
        }
        diminfo_cur_buttons[i] = XtVaCreateManagedWidget(
            "diminfoCur", commandWidgetClass, diminfo_rows[i],
            XtNlabel, buf,
            XtNwidth, VALUE_COL_WIDTH,
            XtNresize, False,
            XtNinternalWidth, 0,
            XtNinternalHeight, 0,
            XtNborderWidth, 0,
            XtNhighlightThickness, 0,
            XtNsensitive, di->is_scannable ? True : False,
            NULL
        );
        XtAddCallback(diminfo_cur_buttons[i], XtNcallback,
                      diminfo_cur_forward_callback, (XtPointer)(intptr_t)i);
        /* Add right-click for backward navigation */
        XtAugmentTranslations(diminfo_cur_buttons[i],
            XtParseTranslationTable("<Btn3Down>,<Btn3Up>: diminfo_cur_backward()"));

        /* Max value label */
        snprintf(buf, sizeof(buf), "%.4g", di->max_val);
        diminfo_max_labels[i] = XtVaCreateManagedWidget(
            "diminfoMax", labelWidgetClass, diminfo_rows[i],
            XtNlabel, buf,
            XtNwidth, VALUE_COL_WIDTH,
            XtNresize, False,
            XtNinternalWidth, 0,
            XtNinternalHeight, 0,
            XtNborderWidth, 0,
            NULL
        );

        /* Units label */
        diminfo_units_labels[i] = XtVaCreateManagedWidget(
            "diminfoUnits", labelWidgetClass, diminfo_rows[i],
            XtNlabel, di->units[0] ? di->units : "-",
            XtNwidth, UNITS_COL_WIDTH,
            XtNresize, False,
            XtNinternalWidth, 0,
            XtNinternalHeight, 0,
            XtNborderWidth, 0,
            NULL
        );
    }
}

void x_update_dim_info(const USDimInfo *dims, int n_dims) {
    if (!diminfo_rows || n_diminfo_rows <= 0) return;

    for (int i = 0; i < n_diminfo_rows; i++) {
        if (i < n_dims && dims) {
            const USDimInfo *di = &dims[i];
            char buf[64];

            XtManageChild(diminfo_rows[i]);

            snprintf(buf, sizeof(buf), "%d", i);
            XtVaSetValues(diminfo_dim_labels[i], XtNlabel, buf, NULL);
            XtVaSetValues(diminfo_name_labels[i], XtNlabel, di->name, NULL);
            snprintf(buf, sizeof(buf), "%.4g", di->min_val);
            XtVaSetValues(diminfo_min_labels[i], XtNlabel, buf, NULL);
            if (di->values && di->size > 0) {
                snprintf(buf, sizeof(buf), "%.4g", di->values[di->current]);
            } else {
                snprintf(buf, sizeof(buf), "%zu", di->current);
            }
            XtVaSetValues(diminfo_cur_buttons[i], XtNlabel, buf, NULL);
            XtVaSetValues(diminfo_cur_buttons[i], XtNsensitive,
                          di->is_scannable ? True : False, NULL);
            snprintf(buf, sizeof(buf), "%.4g", di->max_val);
            XtVaSetValues(diminfo_max_labels[i], XtNlabel, buf, NULL);
            XtVaSetValues(diminfo_units_labels[i], XtNlabel,
                          di->units[0] ? di->units : "-", NULL);
        } else {
            XtUnmanageChild(diminfo_rows[i]);
        }
    }
}

void x_update_dim_current(int dim_index, size_t current_idx, double current_val) {
    (void)current_idx;  /* May be used in future for index display */
    if (dim_index < 0 || dim_index >= n_diminfo_rows) return;
    if (!diminfo_cur_buttons || !diminfo_cur_buttons[dim_index]) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "%.4g", current_val);
    XtVaSetValues(diminfo_cur_buttons[dim_index], XtNlabel, buf, NULL);
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

void x_update_dim_label(const char *text) {
    if (!label_dims || !text) return;
    XtVaSetValues(label_dims, XtNlabel, text, NULL);
}

void x_update_range_label(float min_val, float max_val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Range: [%.4g, %.4g]", min_val, max_val);
    (void)min_val; (void)max_val; (void)buf;
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
    (void)width;
    if (!display || !colorbar_widget) return;

    cbar_min_val = min_val;
    cbar_max_val = max_val;

    Dimension w = 0, h = 0;
    if (XtIsRealized(colorbar_widget)) {
        XtVaGetValues(colorbar_widget, XtNwidth, &w, XtNheight, &h, NULL);
    }
    if (w == 0) {
        w = LABEL_WIDTH;
    }

    colorbar_init((size_t)w, CBAR_HEIGHT);
    colorbar_render();

    size_t cbar_width, cbar_height;
    unsigned char *cbar_pixels = colorbar_get_pixels(&cbar_width, &cbar_height);
    if (!cbar_pixels) return;

    XtVaSetValues(colorbar_widget, XtNwidth, cbar_width,
                  XtNheight, (Dimension)(CBAR_HEIGHT + CBAR_LABEL_HEIGHT + CBAR_PAD), NULL);

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

    if (XtIsRealized(colorbar_widget)) {
        draw_colorbar(colorbar_widget);
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
    free(varsel_boxes);
    x_clear_dim_info();
}
