/*
 * timeseries_popup.h - Time series plot popup window
 *
 * Non-modal popup that displays a time series plot (value vs time)
 * at a clicked spatial location.
 */

#ifndef TIMESERIES_POPUP_H
#define TIMESERIES_POPUP_H

#include <X11/Intrinsic.h>
#include "../ushow.defines.h"

/*
 * Initialize the timeseries popup widgets.
 * Must be called after XtRealizeWidget(top_level).
 */
void timeseries_popup_init(Widget parent, Display *dpy, XtAppContext app_ctx);

/*
 * Show (or update) the timeseries popup with new data.
 * Deep-copies TSData so caller can free immediately.
 */
void timeseries_popup_show(const TSData *data);

/*
 * Cleanup timeseries popup resources.
 */
void timeseries_popup_cleanup(void);

#endif /* TIMESERIES_POPUP_H */
