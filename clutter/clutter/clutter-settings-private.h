#ifndef __CLUTTER_SETTINGS_PRIVATE_H__
#define __CLUTTER_SETTINGS_PRIVATE_H__

#include <clutter/clutter-backend-private.h>
#include <clutter/clutter-settings.h>

G_BEGIN_DECLS

void    _clutter_settings_set_backend           (ClutterSettings *settings,
                                                 ClutterBackend  *backend);

void    clutter_settings_set_property_internal (ClutterSettings *settings,
                                                const char *property,
                                                GValue *value);

void clutter_settings_ensure_pointer_a11y_settings (ClutterSettings *settings,
                                                    ClutterSeat     *seat);

G_END_DECLS

#endif /* __CLUTTER_SETTINGS_PRIVATE_H__ */
