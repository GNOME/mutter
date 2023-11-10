#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_SETTINGS           (clutter_settings_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterSettings, clutter_settings,
                      CLUTTER, SETTINGS,
                      GObject)

CLUTTER_EXPORT
ClutterSettings *clutter_settings_get_default (void);

G_END_DECLS
