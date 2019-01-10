#include "clutter-xkb-utils.h"
#include "clutter-keymap.h"

#define CLUTTER_TYPE_KEYMAP_EVDEV (clutter_keymap_evdev_get_type ())
G_DECLARE_FINAL_TYPE (ClutterKeymapEvdev, clutter_keymap_evdev,
                      CLUTTER, KEYMAP_EVDEV,
                      ClutterKeymap)

void                clutter_keymap_evdev_set_keyboard_map (ClutterKeymapEvdev *keymap,
                                                           struct xkb_keymap  *xkb_keymap);
struct xkb_keymap * clutter_keymap_evdev_get_keyboard_map (ClutterKeymapEvdev *keymap);
