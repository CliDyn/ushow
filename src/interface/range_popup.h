/*
 * range_popup.h - Range popup dialog for setting min/max values
 *
 * Modal popup with editable min/max text fields,
 * "Symmetric about Zero" and "Reset to Global Values" buttons.
 */

#ifndef RANGE_POPUP_H
#define RANGE_POPUP_H

#include <X11/Intrinsic.h>
#include "range_utils.h"

/*
 * Initialize the range popup widgets.
 * Must be called after XtRealizeWidget(top_level).
 */
void range_popup_init(Widget parent, Display *dpy, XtAppContext app_ctx);

/*
 * Show the range popup dialog (modal).
 *
 * old_min/old_max: current display range
 * global_min/global_max: full data range for "Reset to Global Values"
 * new_min/new_max: output values set by user
 *
 * Returns RANGE_POPUP_OK or RANGE_POPUP_CANCEL.
 */
int range_popup_show(float old_min, float old_max,
                     float global_min, float global_max,
                     float *new_min, float *new_max);

/*
 * Cleanup range popup resources.
 */
void range_popup_cleanup(void);

#endif /* RANGE_POPUP_H */
