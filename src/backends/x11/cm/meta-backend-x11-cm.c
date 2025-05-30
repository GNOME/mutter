/*
 * Copyright (C) 2017 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/x11/cm/meta-backend-x11-cm.h"

#include <stdlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dnd-private.h"
#include "backends/x11/meta-barrier-x11.h"
#include "backends/x11/meta-cursor-renderer-x11.h"
#include "backends/x11/meta-cursor-tracker-x11.h"
#include "backends/x11/meta-gpu-xrandr.h"
#include "backends/x11/meta-input-settings-x11.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"
#include "backends/x11/cm/meta-renderer-x11-cm.h"
#include "compositor/meta-compositor-x11.h"
#include "core/display-private.h"

enum
{
  PROP_0,

  PROP_DISPLAY_NAME,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaBackendX11Cm
{
  MetaBackendX11 parent;

  char *display_name;

  MetaCursorRenderer *cursor_renderer;
  char *keymap_layouts;
  char *keymap_variants;
  char *keymap_options;
  char *keymap_model;
  int locked_group;

  MetaInputSettings *input_settings;
};

G_DEFINE_FINAL_TYPE (MetaBackendX11Cm,
                     meta_backend_x11_cm,
                     META_TYPE_BACKEND_X11)

static void
apply_keymap (MetaBackendX11 *x11);

static void
take_touch_grab (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { META_VIRTUAL_CORE_POINTER_ID, sizeof (mask_bits), mask_bits };
  XIGrabModifiers mods = { XIAnyModifier, 0 };

  XISetMask (mask.mask, XI_TouchBegin);
  XISetMask (mask.mask, XI_TouchUpdate);
  XISetMask (mask.mask, XI_TouchEnd);

  XIGrabTouchBegin (xdisplay, META_VIRTUAL_CORE_POINTER_ID,
                    DefaultRootWindow (xdisplay),
                    False, &mask, 1, &mods);
}

static void
on_device_added (ClutterSeat        *seat,
                 ClutterInputDevice *device,
                 gpointer            user_data)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (user_data);

  if (clutter_input_device_get_device_type (device) == CLUTTER_KEYBOARD_DEVICE)
    apply_keymap (x11);
}

static gboolean
meta_backend_x11_cm_init_basic (MetaBackend  *backend,
                                GError      **error)
{
  MetaBackendClass *parent_backend_class =
    META_BACKEND_CLASS (meta_backend_x11_cm_parent_class);
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (backend);
  MetaGpuXrandr *gpu_xrandr;

  if (x11_cm->display_name)
    g_setenv ("DISPLAY", x11_cm->display_name, TRUE);

  /*
   * The X server deals with multiple GPUs for us, so we just see what the X
   * server gives us as one single GPU, even though it may actually be backed
   * by multiple.
   */
  gpu_xrandr = meta_gpu_xrandr_new (META_BACKEND_X11 (x11_cm));
  meta_backend_add_gpu (backend, META_GPU (gpu_xrandr));

  return parent_backend_class->init_basic (backend, error);
}

static gboolean
meta_backend_x11_cm_init_render (MetaBackend  *backend,
                                 GError      **error)
{
  MetaBackendClass *parent_backend_class =
    META_BACKEND_CLASS (meta_backend_x11_cm_parent_class);
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (meta_backend_get_clutter_backend (backend));
  g_signal_connect_object (seat, "device-added",
                           G_CALLBACK (on_device_added), backend, 0);

  x11_cm->input_settings = g_object_new (META_TYPE_INPUT_SETTINGS_X11,
                                         "backend", backend,
                                         NULL);

  if (!parent_backend_class->init_render (backend, error))
    return FALSE;

  take_touch_grab (backend);

  return TRUE;
}

static MetaBackendCapabilities
meta_backend_x11_cm_get_capabilities (MetaBackend *backend)
{
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  MetaBackendCapabilities capabilities = META_BACKEND_CAPABILITY_NONE;
  MetaX11Barriers *barriers;

  barriers = meta_backend_x11_get_barriers (backend_x11);
  if (barriers)
    capabilities |= META_BACKEND_CAPABILITY_BARRIERS;

  return capabilities;
}

static MetaRenderer *
meta_backend_x11_cm_create_renderer (MetaBackend *backend,
                                     GError     **error)
{
  return g_object_new (META_TYPE_RENDERER_X11_CM,
                       "backend", backend,
                       NULL);
}

static MetaMonitorManager *
meta_backend_x11_cm_create_monitor_manager (MetaBackend *backend,
                                            GError     **error)
{
  return g_object_new (META_TYPE_MONITOR_MANAGER_XRANDR,
                       "backend", backend,
                       NULL);
}

static MetaCursorRenderer *
meta_backend_x11_cm_get_cursor_renderer (MetaBackend        *backend,
                                         ClutterInputDevice *device)
{
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (backend);

  if (!x11_cm->cursor_renderer)
    {
      x11_cm->cursor_renderer =
        g_object_new (META_TYPE_CURSOR_RENDERER_X11,
                      "backend", backend,
                      "device", device,
                      NULL);
    }

  return x11_cm->cursor_renderer;
}

static MetaCursorTracker *
meta_backend_x11_cm_create_cursor_tracker (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CURSOR_TRACKER_X11,
                       "backend", backend,
                       NULL);
}

static MetaInputSettings *
meta_backend_x11_cm_get_input_settings (MetaBackend *backend)
{
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (backend);

  return x11_cm->input_settings;
}

static void
meta_backend_x11_cm_update_stage (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);
  Window xwin = meta_backend_x11_get_xwindow (x11);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  int width, height;

  meta_monitor_manager_get_screen_size (monitor_manager, &width, &height);
  XResizeWindow (xdisplay, xwin, width, height);
}

static void
meta_backend_x11_cm_select_stage_events (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);
  Window xwin = meta_backend_x11_get_xwindow (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);
  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Enter);
  XISetMask (mask.mask, XI_Leave);
  XISetMask (mask.mask, XI_FocusIn);
  XISetMask (mask.mask, XI_FocusOut);
  XISetMask (mask.mask, XI_Motion);

  XISelectEvents (xdisplay, xwin, &mask, 1);
}

static void
get_xkbrf_var_defs (Display           *xdisplay,
                    const char        *layouts,
                    const char        *variants,
                    const char        *options,
                    const char        *model,
                    char             **rules_p,
                    XkbRF_VarDefsRec  *var_defs)
{
  char *rules = NULL;

  /* Get it from the X property or fallback on defaults */
  if (!XkbRF_GetNamesProp (xdisplay, &rules, var_defs) || !rules)
    {
      rules = strdup (DEFAULT_XKB_RULES_FILE);
      var_defs->model = NULL;
      var_defs->layout = NULL;
      var_defs->variant = NULL;
      var_defs->options = NULL;
    }

  /* Swap in our new options... */
  free (var_defs->layout);
  var_defs->layout = strdup (layouts);
  free (var_defs->variant);
  var_defs->variant = strdup (variants);
  free (var_defs->options);
  var_defs->options = strdup (options);
  free (var_defs->model);
  var_defs->model = strdup (model);

  /* Sometimes, the property is a file path, and sometimes it's
     not. Normalize it so it's always a file path. */
  if (rules[0] == '/')
    *rules_p = g_strdup (rules);
  else
    *rules_p = g_build_filename (XKB_BASE, "rules", rules, NULL);

  free (rules);
}

static void
free_xkbrf_var_defs (XkbRF_VarDefsRec *var_defs)
{
  free (var_defs->model);
  free (var_defs->layout);
  free (var_defs->variant);
  free (var_defs->options);
}

static void
free_xkb_component_names (XkbComponentNamesRec *p)
{
  free (p->keymap);
  free (p->keycodes);
  free (p->types);
  free (p->compat);
  free (p->symbols);
  free (p->geometry);
}

static void
upload_xkb_description (Display              *xdisplay,
                        const gchar          *rules_file_path,
                        XkbRF_VarDefsRec     *var_defs,
                        XkbComponentNamesRec *comp_names)
{
  XkbDescRec *xkb_desc;
  gchar *rules_file;

  /* Upload it to the X server using the same method as setxkbmap */
  xkb_desc = XkbGetKeyboardByName (xdisplay,
                                   XkbUseCoreKbd,
                                   comp_names,
                                   XkbGBN_AllComponentsMask,
                                   XkbGBN_AllComponentsMask &
                                   (~XkbGBN_GeometryMask), True);
  if (!xkb_desc)
    {
      g_warning ("Couldn't upload new XKB keyboard description");
      return;
    }

  XkbFreeKeyboard (xkb_desc, 0, True);

  rules_file = g_path_get_basename (rules_file_path);

  if (!XkbRF_SetNamesProp (xdisplay, rules_file, var_defs))
    g_warning ("Couldn't update the XKB root window property");

  g_free (rules_file);
}

static void
apply_keymap (MetaBackendX11 *x11)
{
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (x11);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);
  XkbRF_RulesRec *xkb_rules;
  XkbRF_VarDefsRec xkb_var_defs = { 0 };
  char *rules_file_path;

  if (!x11_cm->keymap_layouts ||
      !x11_cm->keymap_variants ||
      !x11_cm->keymap_options ||
      !x11_cm->keymap_model)
    return;

  get_xkbrf_var_defs (xdisplay,
                      x11_cm->keymap_layouts,
                      x11_cm->keymap_variants,
                      x11_cm->keymap_options,
                      x11_cm->keymap_model,
                      &rules_file_path,
                      &xkb_var_defs);

  xkb_rules = XkbRF_Load (rules_file_path, NULL, True, True);
  if (xkb_rules)
    {
      XkbComponentNamesRec xkb_comp_names = { 0 };

      XkbRF_GetComponents (xkb_rules, &xkb_var_defs, &xkb_comp_names);
      upload_xkb_description (xdisplay, rules_file_path, &xkb_var_defs, &xkb_comp_names);

      free_xkb_component_names (&xkb_comp_names);
      XkbRF_Free (xkb_rules, True);
    }
  else
    {
      g_warning ("Couldn't load XKB rules");
    }

  free_xkbrf_var_defs (&xkb_var_defs);
  g_free (rules_file_path);
}

static void
meta_backend_x11_cm_set_keymap_async (MetaBackend *backend,
                                      const char  *layouts,
                                      const char  *variants,
                                      const char  *options,
                                      const char  *model,
                                      GTask       *task)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (x11);

  g_free (x11_cm->keymap_layouts);
  x11_cm->keymap_layouts = g_strdup (layouts);
  g_free (x11_cm->keymap_variants);
  x11_cm->keymap_variants = g_strdup (variants);
  g_free (x11_cm->keymap_options);
  x11_cm->keymap_options = g_strdup (options);
  g_free (x11_cm->keymap_model);
  x11_cm->keymap_model = g_strdup (model);

  apply_keymap (x11);

  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static void
meta_backend_x11_cm_set_keymap_layout_group_async (MetaBackend        *backend,
                                                   xkb_layout_index_t  idx,
                                                   GTask              *task)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (x11);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);

  x11_cm->locked_group = idx;
  XkbLockGroup (xdisplay, XkbUseCoreKbd, idx);

  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gboolean
meta_backend_x11_cm_handle_host_xevent (MetaBackendX11 *x11,
                                        XEvent         *event)
{
  MetaBackend *backend = META_BACKEND (x11);
  MetaContext *context = meta_backend_get_context (backend);
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (x11);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);
  MetaDisplay *display;

  display = meta_context_get_display (context);
  if (display)
    {
      MetaCompositor *compositor = display->compositor;
      MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);

      if (meta_dnd_handle_xdnd_event (backend, compositor_x11,
                                      xdisplay, event))
        return TRUE;
    }

  if (event->type == meta_backend_x11_get_xkb_event_base (x11))
    {
      XkbEvent *xkb_ev = (XkbEvent *) event;

      if (xkb_ev->any.device == META_VIRTUAL_CORE_KEYBOARD_ID)
        {
          switch (xkb_ev->any.xkb_type)
            {
            case XkbStateNotify:
              if (xkb_ev->state.changed & XkbGroupLockMask)
                {
                  if (x11_cm->locked_group != xkb_ev->state.locked_group)
                    XkbLockGroup (xdisplay, XkbUseCoreKbd,
                                  x11_cm->locked_group);
                }
              break;
            default:
              break;
            }
        }
    }

  if (meta_monitor_manager_xrandr_handle_xevent (monitor_manager_xrandr, event))
    return TRUE;

  return FALSE;
}

static void
meta_backend_x11_cm_translate_device_event (MetaBackendX11 *x11,
                                            XIDeviceEvent  *device_event)
{
  Window stage_window = meta_backend_x11_get_xwindow (x11);

  if (device_event->event != stage_window)
    {
      device_event->event = stage_window;

      /* As an X11 compositor, the stage window is always at 0,0, so
       * using root coordinates will give us correct stage coordinates
       * as well... */
      device_event->event_x = device_event->root_x;
      device_event->event_y = device_event->root_y;
    }
}

static void
meta_backend_x11_cm_translate_crossing_event (MetaBackendX11 *x11,
                                              XIEnterEvent   *enter_event)
{
  Window stage_window = meta_backend_x11_get_xwindow (x11);

  if (enter_event->event != stage_window)
    {
      enter_event->event = stage_window;
      enter_event->event_x = enter_event->root_x;
      enter_event->event_y = enter_event->root_y;
    }
}

static void
meta_backend_x11_cm_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaBackendX11Cm *backend_x11_cm = META_BACKEND_X11_CM (object);

  switch (prop_id)
    {
    case PROP_DISPLAY_NAME:
      backend_x11_cm->display_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_backend_x11_cm_finalize (GObject *object)
{
  MetaBackendX11Cm *x11_cm = META_BACKEND_X11_CM (object);

  g_clear_pointer (&x11_cm->display_name, g_free);

  G_OBJECT_CLASS (meta_backend_x11_cm_parent_class)->finalize (object);
}

static void
meta_backend_x11_cm_init (MetaBackendX11Cm *backend_x11_cm)
{
}

static void
meta_backend_x11_cm_class_init (MetaBackendX11CmClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  MetaBackendX11Class *backend_x11_class = META_BACKEND_X11_CLASS (klass);

  object_class->set_property = meta_backend_x11_cm_set_property;
  object_class->finalize = meta_backend_x11_cm_finalize;

  backend_class->init_basic = meta_backend_x11_cm_init_basic;
  backend_class->init_render = meta_backend_x11_cm_init_render;
  backend_class->get_capabilities = meta_backend_x11_cm_get_capabilities;
  backend_class->create_renderer = meta_backend_x11_cm_create_renderer;
  backend_class->create_monitor_manager = meta_backend_x11_cm_create_monitor_manager;
  backend_class->get_cursor_renderer = meta_backend_x11_cm_get_cursor_renderer;
  backend_class->create_cursor_tracker = meta_backend_x11_cm_create_cursor_tracker;
  backend_class->get_input_settings = meta_backend_x11_cm_get_input_settings;
  backend_class->update_stage = meta_backend_x11_cm_update_stage;
  backend_class->select_stage_events = meta_backend_x11_cm_select_stage_events;
  backend_class->set_keymap_async = meta_backend_x11_cm_set_keymap_async;
  backend_class->set_keymap_layout_group_async = meta_backend_x11_cm_set_keymap_layout_group_async;

  backend_x11_class->handle_host_xevent = meta_backend_x11_cm_handle_host_xevent;
  backend_x11_class->translate_device_event = meta_backend_x11_cm_translate_device_event;
  backend_x11_class->translate_crossing_event = meta_backend_x11_cm_translate_crossing_event;

  obj_props[PROP_DISPLAY_NAME] =
    g_param_spec_string ("display-name", NULL, NULL,
                         NULL,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

