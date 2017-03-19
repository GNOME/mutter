/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include <stdlib.h>

#include <clutter/clutter-mutter.h>
#include <meta/meta-backend.h>
#include <meta/main.h>
#include "meta-backend-private.h"
#include "meta-input-settings-private.h"

#include "backends/x11/meta-backend-x11.h"
#include "meta-cursor-tracker-private.h"
#include "meta-stage.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-dummy.h"

static MetaBackend *_backend;

/**
 * meta_get_backend:
 *
 * Accessor for the singleton MetaBackend.
 *
 * Returns: (transfer none): The only #MetaBackend there is.
 */
MetaBackend *
meta_get_backend (void)
{
  return _backend;
}

struct _MetaBackendPrivate
{
  MetaMonitorManager *monitor_manager;
  MetaCursorTracker *cursor_tracker;
  MetaCursorRenderer *cursor_renderer;
  MetaInputSettings *input_settings;
  MetaRenderer *renderer;
  MetaEgl *egl;

  ClutterBackend *clutter_backend;
  ClutterActor *stage;

  guint device_update_idle_id;

  GHashTable *device_monitors;

  int current_device_id;

  MetaPointerConstraint *client_pointer_constraint;
  MetaDnd *dnd;
};
typedef struct _MetaBackendPrivate MetaBackendPrivate;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaBackend, meta_backend, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (MetaBackend)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init));

static void
meta_backend_finalize (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_clear_object (&priv->monitor_manager);
  g_clear_object (&priv->input_settings);

  if (priv->device_update_idle_id)
    g_source_remove (priv->device_update_idle_id);

  g_hash_table_destroy (priv->device_monitors);

  G_OBJECT_CLASS (meta_backend_parent_class)->finalize (object);
}

static void
meta_backend_sync_screen_size (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  int width, height;

  meta_monitor_manager_get_screen_size (priv->monitor_manager, &width, &height);

  META_BACKEND_GET_CLASS (backend)->update_screen_size (backend, width, height);
}

static void
center_pointer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaMonitorManager *monitor_manager = priv->monitor_manager;
  MetaLogicalMonitor *primary;

  primary =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

  if (!primary)
    {
      meta_fatal("Unable to get primary logical monitor\n");
    }

  meta_backend_warp_pointer (backend,
                             primary->rect.x + primary->rect.width / 2,
                             primary->rect.y + primary->rect.height / 2);
}

void
meta_backend_monitors_changed (MetaBackend *backend)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  ClutterInputDevice *device = clutter_device_manager_get_core_device (manager, CLUTTER_POINTER_DEVICE);
  ClutterPoint point;

  meta_backend_sync_screen_size (backend);

  if (clutter_input_device_get_coords (device, NULL, &point))
    {
      /* If we're outside all monitors, warp the pointer back inside */
      if (!meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                        point.x, point.y) &&
          !meta_monitor_manager_is_headless (monitor_manager))
        center_pointer (backend);
    }
}

void
meta_backend_foreach_device_monitor (MetaBackend *backend,
                                     GFunc        func,
                                     gpointer     user_data)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->device_monitors);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MetaIdleMonitor *device_monitor = META_IDLE_MONITOR (value);

      func (device_monitor, user_data);
    }
}

static MetaIdleMonitor *
meta_backend_create_idle_monitor (MetaBackend *backend,
                                  int          device_id)
{
  return META_BACKEND_GET_CLASS (backend)->create_idle_monitor (backend, device_id);
}

static void
create_device_monitor (MetaBackend *backend,
                       int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaIdleMonitor *idle_monitor;

  g_assert (g_hash_table_lookup (priv->device_monitors, &device_id) == NULL);

  idle_monitor = meta_backend_create_idle_monitor (backend, device_id);
  g_hash_table_insert (priv->device_monitors, &idle_monitor->device_id, idle_monitor);
}

static void
destroy_device_monitor (MetaBackend *backend,
                        int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_hash_table_remove (priv->device_monitors, &device_id);
}

static void
on_device_added (ClutterDeviceManager *device_manager,
                 ClutterInputDevice   *device,
                 gpointer              user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  int device_id = clutter_input_device_get_device_id (device);

  create_device_monitor (backend, device_id);
}

static inline gboolean
device_is_slave_touchscreen (ClutterInputDevice *device)
{
  return (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE);
}

static inline gboolean
check_has_pointing_device (ClutterDeviceManager *manager)
{
  const GSList *devices;

  devices = clutter_device_manager_peek_devices (manager);

  for (; devices; devices = devices->next)
    {
      ClutterInputDevice *device = devices->data;

      if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
        continue;
      if (clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE ||
          clutter_input_device_get_device_type (device) == CLUTTER_KEYBOARD_DEVICE)
        continue;

      return TRUE;
    }

  return FALSE;
}

static inline gboolean
check_has_slave_touchscreen (ClutterDeviceManager *manager)
{
  const GSList *devices;

  devices = clutter_device_manager_peek_devices (manager);

  for (; devices; devices = devices->next)
    {
      ClutterInputDevice *device = devices->data;

      if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static void
on_device_removed (ClutterDeviceManager *device_manager,
                   ClutterInputDevice   *device,
                   gpointer              user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  int device_id = clutter_input_device_get_device_id (device);

  destroy_device_monitor (backend, device_id);

  /* If the device the user last interacted goes away, check again pointer
   * visibility.
   */
  if (priv->current_device_id == device_id)
    {
      MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
      gboolean has_touchscreen, has_pointing_device;
      ClutterInputDeviceType device_type;

      device_type = clutter_input_device_get_device_type (device);
      has_touchscreen = check_has_slave_touchscreen (device_manager);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE && has_touchscreen)
        {
          /* There's more touchscreens left, keep the pointer hidden */
          meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
        }
      else if (device_type != CLUTTER_KEYBOARD_DEVICE)
        {
          has_pointing_device = check_has_pointing_device (device_manager);
          meta_cursor_tracker_set_pointer_visible (cursor_tracker,
                                                   has_pointing_device &&
                                                   !has_touchscreen);
        }
    }
}

static MetaMonitorManager *
create_monitor_manager (MetaBackend *backend)
{
  if (g_getenv ("META_DUMMY_MONITORS"))
    return g_object_new (META_TYPE_MONITOR_MANAGER_DUMMY, NULL);

  return META_BACKEND_GET_CLASS (backend)->create_monitor_manager (backend);
}

static void
meta_backend_real_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->stage = meta_stage_new ();
  clutter_actor_realize (priv->stage);
  META_BACKEND_GET_CLASS (backend)->select_stage_events (backend);

  priv->monitor_manager = create_monitor_manager (backend);

  meta_backend_sync_screen_size (backend);

  priv->cursor_renderer = META_BACKEND_GET_CLASS (backend)->create_cursor_renderer (backend);

  priv->device_monitors =
    g_hash_table_new_full (g_int_hash, g_int_equal,
                           NULL, (GDestroyNotify) g_object_unref);

  {
    MetaCursorTracker *cursor_tracker;
    ClutterDeviceManager *manager;
    gboolean has_touchscreen = FALSE;
    GSList *devices, *l;

    /* Create the core device monitor. */
    create_device_monitor (backend, 0);

    manager = clutter_device_manager_get_default ();
    g_signal_connect_object (manager, "device-added",
                             G_CALLBACK (on_device_added), backend, 0);
    g_signal_connect_object (manager, "device-removed",
                             G_CALLBACK (on_device_removed), backend, 0);

    devices = clutter_device_manager_list_devices (manager);

    for (l = devices; l != NULL; l = l->next)
      {
        ClutterInputDevice *device = l->data;
        on_device_added (manager, device, backend);
        has_touchscreen |= device_is_slave_touchscreen (device);
      }

    cursor_tracker = priv->cursor_tracker;
    meta_cursor_tracker_set_pointer_visible (cursor_tracker, !has_touchscreen);

    g_slist_free (devices);
  }

  priv->input_settings = meta_input_settings_create ();

  center_pointer (backend);
}

static MetaCursorRenderer *
meta_backend_real_create_cursor_renderer (MetaBackend *backend)
{
  return meta_cursor_renderer_new ();
}

static gboolean
meta_backend_real_grab_device (MetaBackend *backend,
                               int          device_id,
                               uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static gboolean
meta_backend_real_ungrab_device (MetaBackend *backend,
                                 int          device_id,
                                 uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static void
meta_backend_real_select_stage_events (MetaBackend *backend)
{
  /* Do nothing */
}

static gboolean
meta_backend_real_get_relative_motion_deltas (MetaBackend *backend,
                                             const         ClutterEvent *event,
                                             double        *dx,
                                             double        *dy,
                                             double        *dx_unaccel,
                                             double        *dy_unaccel)
{
  return FALSE;
}

static void
meta_backend_class_init (MetaBackendClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_backend_finalize;

  klass->post_init = meta_backend_real_post_init;
  klass->create_cursor_renderer = meta_backend_real_create_cursor_renderer;
  klass->grab_device = meta_backend_real_grab_device;
  klass->ungrab_device = meta_backend_real_ungrab_device;
  klass->select_stage_events = meta_backend_real_select_stage_events;
  klass->get_relative_motion_deltas = meta_backend_real_get_relative_motion_deltas;

  g_signal_new ("keymap-changed",
                G_TYPE_FROM_CLASS (object_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 0);
  g_signal_new ("keymap-layout-group-changed",
                G_TYPE_FROM_CLASS (object_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 1, G_TYPE_UINT);
  g_signal_new ("last-device-changed",
                G_TYPE_FROM_CLASS (object_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 1, G_TYPE_INT);
}

static gboolean
meta_backend_initable_init (GInitable     *initable,
                            GCancellable  *cancellable,
                            GError       **error)
{
  MetaBackend *backend = META_BACKEND (initable);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->egl = g_object_new (META_TYPE_EGL, NULL);

  priv->renderer = META_BACKEND_GET_CLASS (backend)->create_renderer (backend);
  if (!priv->renderer)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create MetaRenderer");
      return FALSE;
    }

  priv->cursor_tracker = g_object_new (META_TYPE_CURSOR_TRACKER, NULL);

  priv->dnd = g_object_new (META_TYPE_DND, NULL);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_backend_initable_init;
}

static void
meta_backend_init (MetaBackend *backend)
{
  _backend = backend;
}

static void
meta_backend_post_init (MetaBackend *backend)
{
  META_BACKEND_GET_CLASS (backend)->post_init (backend);
}

/**
 * meta_backend_get_idle_monitor: (skip)
 */
MetaIdleMonitor *
meta_backend_get_idle_monitor (MetaBackend *backend,
                               int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return g_hash_table_lookup (priv->device_monitors, &device_id);
}

/**
 * meta_backend_get_monitor_manager: (skip)
 */
MetaMonitorManager *
meta_backend_get_monitor_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->monitor_manager;
}

MetaCursorTracker *
meta_backend_get_cursor_tracker (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->cursor_tracker;
}

/**
 * meta_backend_get_cursor_renderer: (skip)
 */
MetaCursorRenderer *
meta_backend_get_cursor_renderer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->cursor_renderer;
}

/**
 * meta_backend_get_renderer: (skip)
 */
MetaRenderer * meta_backend_get_renderer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->renderer;
}

/**
 * meta_backend_get_egl: (skip)
 */
MetaEgl * meta_backend_get_egl (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->egl;
}

/**
 * meta_backend_grab_device: (skip)
 */
gboolean
meta_backend_grab_device (MetaBackend *backend,
                          int          device_id,
                          uint32_t     timestamp)
{
  return META_BACKEND_GET_CLASS (backend)->grab_device (backend, device_id, timestamp);
}

/**
 * meta_backend_ungrab_device: (skip)
 */
gboolean
meta_backend_ungrab_device (MetaBackend *backend,
                            int          device_id,
                            uint32_t     timestamp)
{
  return META_BACKEND_GET_CLASS (backend)->ungrab_device (backend, device_id, timestamp);
}

/**
 * meta_backend_warp_pointer: (skip)
 */
void
meta_backend_warp_pointer (MetaBackend *backend,
                           int          x,
                           int          y)
{
  META_BACKEND_GET_CLASS (backend)->warp_pointer (backend, x, y);
}

MetaLogicalMonitor *
meta_backend_get_current_logical_monitor (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_current_logical_monitor (backend);
}

void
meta_backend_set_keymap (MetaBackend *backend,
                         const char  *layouts,
                         const char  *variants,
                         const char  *options)
{
  META_BACKEND_GET_CLASS (backend)->set_keymap (backend, layouts, variants, options);
}

/**
 * meta_backend_get_keymap: (skip)
 */
struct xkb_keymap *
meta_backend_get_keymap (MetaBackend *backend)

{
  return META_BACKEND_GET_CLASS (backend)->get_keymap (backend);
}

void
meta_backend_lock_layout_group (MetaBackend *backend,
                                guint idx)
{
  META_BACKEND_GET_CLASS (backend)->lock_layout_group (backend, idx);
}

void
meta_backend_set_numlock (MetaBackend *backend,
                          gboolean     numlock_state)
{
  META_BACKEND_GET_CLASS (backend)->set_numlock (backend, numlock_state);
}


/**
 * meta_backend_get_stage:
 * @backend: A #MetaBackend
 *
 * Gets the global #ClutterStage that's managed by this backend.
 *
 * Returns: (transfer none): the #ClutterStage
 */
ClutterActor *
meta_backend_get_stage (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  return priv->stage;
}

static gboolean
update_last_device (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
  ClutterInputDeviceType device_type;
  ClutterDeviceManager *manager;
  ClutterInputDevice *device;

  priv->device_update_idle_id = 0;
  manager = clutter_device_manager_get_default ();
  device = clutter_device_manager_get_device (manager,
                                              priv->current_device_id);
  device_type = clutter_input_device_get_device_type (device);

  g_signal_emit_by_name (backend, "last-device-changed",
                         priv->current_device_id);

  switch (device_type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
      break;
    default:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, TRUE);
      break;
    }

  return G_SOURCE_REMOVE;
}

void
meta_backend_update_last_device (MetaBackend *backend,
                                 int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterDeviceManager *manager;
  ClutterInputDevice *device;

  if (priv->current_device_id == device_id)
    return;

  manager = clutter_device_manager_get_default ();
  device = clutter_device_manager_get_device (manager, device_id);

  if (!device ||
      clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
    return;

  priv->current_device_id = device_id;

  if (priv->device_update_idle_id == 0)
    {
      priv->device_update_idle_id =
        g_idle_add ((GSourceFunc) update_last_device, backend);
      g_source_set_name_by_id (priv->device_update_idle_id,
                               "[mutter] update_last_device");
    }
}

gboolean
meta_backend_get_relative_motion_deltas (MetaBackend *backend,
                                         const        ClutterEvent *event,
                                         double       *dx,
                                         double       *dy,
                                         double       *dx_unaccel,
                                         double       *dy_unaccel)
{
  MetaBackendClass *klass = META_BACKEND_GET_CLASS (backend);
  return klass->get_relative_motion_deltas (backend,
                                            event,
                                            dx, dy,
                                            dx_unaccel, dy_unaccel);
}

MetaPointerConstraint *
meta_backend_get_client_pointer_constraint (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->client_pointer_constraint;
}

void
meta_backend_set_client_pointer_constraint (MetaBackend           *backend,
                                            MetaPointerConstraint *constraint)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_assert (!constraint || !priv->client_pointer_constraint);

  g_clear_object (&priv->client_pointer_constraint);
  if (constraint)
    priv->client_pointer_constraint = g_object_ref (constraint);
}

/* Mutter is responsible for pulling events off the X queue, so Clutter
 * doesn't need (and shouldn't) run its normal event source which polls
 * the X fd, but we do have to deal with dispatching events that accumulate
 * in the clutter queue. This happens, for example, when clutter generate
 * enter/leave events on mouse motion - several events are queued in the
 * clutter queue but only one dispatched. It could also happen because of
 * explicit calls to clutter_event_put(). We add a very simple custom
 * event loop source which is simply responsible for pulling events off
 * of the queue and dispatching them before we block for new events.
 */

static gboolean
event_prepare (GSource    *source,
               gint       *timeout_)
{
  *timeout_ = -1;

  return clutter_events_pending ();
}

static gboolean
event_check (GSource *source)
{
  return clutter_events_pending ();
}

static gboolean
event_dispatch (GSource    *source,
                GSourceFunc callback,
                gpointer    user_data)
{
  ClutterEvent *event = clutter_event_get ();

  if (event)
    {
      clutter_do_event (event);
      clutter_event_free (event);
    }

  return TRUE;
}

static GSourceFuncs event_funcs = {
  event_prepare,
  event_check,
  event_dispatch
};

ClutterBackend *
meta_backend_get_clutter_backend (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (!priv->clutter_backend)
    {
      priv->clutter_backend =
        META_BACKEND_GET_CLASS (backend)->create_clutter_backend (backend);
    }

  return priv->clutter_backend;
}

static ClutterBackend *
meta_get_clutter_backend (void)
{
  MetaBackend *backend = meta_get_backend ();

  return meta_backend_get_clutter_backend (backend);
}

void
meta_init_backend (GType backend_gtype)
{
  MetaBackend *backend;
  GError *error = NULL;

  /* meta_backend_init() above install the backend globally so
   * so meta_get_backend() works even during initialization. */
  backend = g_object_new (backend_gtype, NULL);
  if (!g_initable_init (G_INITABLE (backend), NULL, &error))
    {
      g_warning ("Failed to create backend: %s", error->message);
      meta_exit (META_EXIT_ERROR);
    }
}

/**
 * meta_clutter_init: (skip)
 */
void
meta_clutter_init (void)
{
  ClutterSettings *clutter_settings;
  GSource *source;

  clutter_set_custom_backend_func (meta_get_clutter_backend);

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS)
    {
      g_warning ("Unable to initialize Clutter.\n");
      exit (1);
    }

  /*
   * XXX: We cannot handle high dpi scaling yet, so fix the scale to 1
   * for now.
   */
  clutter_settings = clutter_settings_get_default ();
  g_object_set (clutter_settings, "window-scaling-factor", 1, NULL);

  source = g_source_new (&event_funcs, sizeof (GSource));
  g_source_attach (source, NULL);
  g_source_unref (source);

  meta_backend_post_init (_backend);
}

gboolean
meta_is_stage_views_enabled (void)
{
  const gchar *mutter_stage_views;

  if (!meta_is_wayland_compositor ())
    return FALSE;

  mutter_stage_views = g_getenv ("MUTTER_STAGE_VIEWS");

  if (!mutter_stage_views)
    return TRUE;

  return !g_str_equal (mutter_stage_views, "0");
}

MetaInputSettings *
meta_backend_get_input_settings (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->input_settings;
}

/**
 * meta_backend_get_dnd:
 * @backend: A #MetaDnd
 *
 * Gets the global #MetaDnd that's managed by this backend.
 *
 * Returns: (transfer none): the #MetaDnd
 */
MetaDnd *
meta_backend_get_dnd (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->dnd;
}
