/*
 * range_popup.c - Range popup dialog for setting min/max values
 *
 * Inspired by ncview's range.c popup dialog.
 * Modal popup with editable min/max text fields,
 * "Symmetric about Zero" and "Reset to Global Values" buttons.
 */

#include "range_popup.h"
#include "range_utils.h"
#include <X11/Xlib.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MINMAX_TEXT_WIDTH 120

/* X11 handles (passed during init) */
static Display *popup_display = NULL;
static XtAppContext popup_app_context;

/* Popup widgets */
static Widget range_popup_shell = NULL;
static Widget range_form = NULL;
static Widget range_min_label = NULL;
static Widget range_min_text = NULL;
static Widget range_max_label = NULL;
static Widget range_max_text = NULL;
static Widget range_symmetric_btn = NULL;
static Widget range_reset_btn = NULL;
static Widget range_global_label = NULL;
static Widget range_ok_btn = NULL;
static Widget range_cancel_btn = NULL;

/* Modal loop state */
static int popup_done = 0;
static int popup_result = RANGE_POPUP_CANCEL;

/* Global range for "Reset to Global Values" */
static float stored_global_min = 0.0f;
static float stored_global_max = 1.0f;

/* ========== Callbacks ========== */

static void ok_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    popup_result = RANGE_POPUP_OK;
    popup_done = 1;
}

static void cancel_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    popup_result = RANGE_POPUP_CANCEL;
    popup_done = 1;
}

static void symmetric_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    char *sptr;
    float cur_min, cur_max, new_min, new_max;
    char tstr[128];

    XtVaGetValues(range_min_text, XtNstring, &sptr, NULL);
    if (sscanf(sptr, "%g", &cur_min) != 1) return;
    XtVaGetValues(range_max_text, XtNstring, &sptr, NULL);
    if (sscanf(sptr, "%g", &cur_max) != 1) return;

    range_compute_symmetric(cur_min, cur_max, &new_min, &new_max);

    snprintf(tstr, sizeof(tstr), "%g", new_min);
    XtVaSetValues(range_min_text, XtNstring, tstr, NULL);

    snprintf(tstr, sizeof(tstr), "%g", new_max);
    XtVaSetValues(range_max_text, XtNstring, tstr, NULL);
}

static void reset_global_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    (void)w; (void)client_data; (void)call_data;
    char tstr[128];

    snprintf(tstr, sizeof(tstr), "%g", stored_global_min);
    XtVaSetValues(range_min_text, XtNstring, tstr, NULL);

    snprintf(tstr, sizeof(tstr), "%g", stored_global_max);
    XtVaSetValues(range_max_text, XtNstring, tstr, NULL);
}

/* ========== Public API ========== */

void range_popup_init(Widget parent, Display *dpy, XtAppContext app_ctx) {
    popup_display = dpy;
    popup_app_context = app_ctx;

    /* Popup shell */
    range_popup_shell = XtVaCreatePopupShell(
        "Set Range",
        transientShellWidgetClass,
        parent,
        NULL);

    /* Form container */
    range_form = XtVaCreateManagedWidget(
        "rangeForm", formWidgetClass, range_popup_shell,
        XtNborderWidth, 0,
        NULL);

    /* Row 1: Minimum label + text */
    range_min_label = XtVaCreateManagedWidget(
        "range_min_label", labelWidgetClass, range_form,
        XtNlabel, "Minimum:",
        XtNwidth, 80,
        XtNborderWidth, 0,
        NULL);

    range_min_text = XtVaCreateManagedWidget(
        "range_min_text", asciiTextWidgetClass, range_form,
        XtNeditType, XawtextEdit,
        XtNfromHoriz, range_min_label,
        XtNwidth, MINMAX_TEXT_WIDTH,
        NULL);

    /* Row 2: Maximum label + text */
    range_max_label = XtVaCreateManagedWidget(
        "range_max_label", labelWidgetClass, range_form,
        XtNlabel, "Maximum:",
        XtNwidth, 80,
        XtNborderWidth, 0,
        XtNfromVert, range_min_label,
        NULL);

    range_max_text = XtVaCreateManagedWidget(
        "range_max_text", asciiTextWidgetClass, range_form,
        XtNeditType, XawtextEdit,
        XtNfromHoriz, range_max_label,
        XtNfromVert, range_min_text,
        XtNwidth, MINMAX_TEXT_WIDTH,
        NULL);

    /* Row 3: Symmetric about Zero */
    range_symmetric_btn = XtVaCreateManagedWidget(
        "Symmetric about Zero", commandWidgetClass, range_form,
        XtNfromVert, range_max_label,
        NULL);
    XtAddCallback(range_symmetric_btn, XtNcallback, symmetric_callback, NULL);

    /* Row 4: Reset to Global Values + label showing values */
    range_reset_btn = XtVaCreateManagedWidget(
        "Reset to Global Values", commandWidgetClass, range_form,
        XtNfromVert, range_symmetric_btn,
        NULL);
    XtAddCallback(range_reset_btn, XtNcallback, reset_global_callback, NULL);

    range_global_label = XtVaCreateManagedWidget(
        "range_global_label", labelWidgetClass, range_form,
        XtNborderWidth, 0,
        XtNfromVert, range_symmetric_btn,
        XtNfromHoriz, range_reset_btn,
        XtNwidth, 180,
        XtNlabel, "",
        NULL);

    /* Row 5: OK / Cancel */
    range_ok_btn = XtVaCreateManagedWidget(
        "OK", commandWidgetClass, range_form,
        XtNfromVert, range_reset_btn,
        NULL);
    XtAddCallback(range_ok_btn, XtNcallback, ok_callback, NULL);

    range_cancel_btn = XtVaCreateManagedWidget(
        "Cancel", commandWidgetClass, range_form,
        XtNfromHoriz, range_ok_btn,
        XtNfromVert, range_reset_btn,
        NULL);
    XtAddCallback(range_cancel_btn, XtNcallback, cancel_callback, NULL);
}

int range_popup_show(float old_min, float old_max,
                     float global_min, float global_max,
                     float *new_min, float *new_max) {
    if (!range_popup_shell) return RANGE_POPUP_CANCEL;

    char min_str[128], max_str[128], global_str[128];
    XEvent event;

    /* Store global range for reset button */
    stored_global_min = global_min;
    stored_global_max = global_max;

    /* Set current values in text fields */
    snprintf(min_str, sizeof(min_str), "%g", old_min);
    snprintf(max_str, sizeof(max_str), "%g", old_max);
    XtVaSetValues(range_min_text, XtNstring, min_str, NULL);
    XtVaSetValues(range_max_text, XtNstring, max_str, NULL);

    /* Update global values label */
    snprintf(global_str, sizeof(global_str), "%g to %g", global_min, global_max);
    XtVaSetValues(range_global_label, XtNlabel, global_str, NULL);

    /* Initialize output values */
    *new_min = old_min;
    *new_max = old_max;

    /* Show popup (modal) */
    XtPopup(range_popup_shell, XtGrabExclusive);

    /* Modal event loop */
    popup_done = 0;
    while (!popup_done) {
        XtAppNextEvent(popup_app_context, &event);
        XtDispatchEvent(&event);
    }

    /* Read values if OK was pressed */
    if (popup_result == RANGE_POPUP_OK) {
        char *sptr;
        XtVaGetValues(range_min_text, XtNstring, &sptr, NULL);
        sscanf(sptr, "%g", new_min);
        XtVaGetValues(range_max_text, XtNstring, &sptr, NULL);
        sscanf(sptr, "%g", new_max);
    }

    XtPopdown(range_popup_shell);

    return popup_result;
}

void range_popup_cleanup(void) {
    /* Widgets are destroyed as children of top_level */
    range_popup_shell = NULL;
}
