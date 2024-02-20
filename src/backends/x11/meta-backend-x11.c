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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/**
 * MetaBackendX11:
 *
 * A X11 MetaBackend
 *
 * MetaBackendX11 is an implementation of #MetaBackend using X and X
 * extensions, like XInput and XKB.
 */

#include "config.h"

#include "backends/x11/meta-backend-x11.h"

#include <X11/XKBlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/sync.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "backends/meta-color-manager.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-keymap-utils.h"
#include "backends/meta-stage-private.h"
#include "backends/x11/meta-barrier-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-color-manager-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-seat-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "backends/x11/meta-renderer-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "clutter/clutter.h"
#include "compositor/compositor-private.h"
#include "core/display-private.h"
#include "meta/meta-cursor-tracker.h"
#include "meta/util.h"
#include "mtk/mtk-x11.h"
#include "x11/window-x11.h"

struct _MetaBackendX11Private
{
  /* The host X11 display */
  Display *xdisplay;
  Screen *xscreen;
  xcb_connection_t *xcb;
  GSource *source;
  Window root_window;

  int xsync_event_base;
  int xsync_error_base;
  XSyncAlarm user_active_alarm;
  XSyncCounter counter;

  int current_touch_replay_sync_serial;
  int pending_touch_replay_sync_serial;
  Atom touch_replay_sync_atom;

  int xinput_opcode;
  int xinput_event_base;
  int xinput_error_base;
  Time latest_evtime;
  gboolean have_xinput_23;

  uint8_t xkb_event_base;
  uint8_t xkb_error_base;

  gulong keymap_state_changed_id;

  struct xkb_keymap *keymap;
  xkb_layout_index_t keymap_layout_group;

  MetaLogicalMonitor *cached_current_logical_monitor;

  MetaX11Barriers *barriers;
};
typedef struct _MetaBackendX11Private MetaBackendX11Private;

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackendX11, meta_backend_x11, META_TYPE_BACKEND,
                         G_ADD_PRIVATE (MetaBackendX11)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init));


static void
uint64_to_xsync_value (uint64_t    value,
                       XSyncValue *xsync_value)
{
  XSyncIntsToValue (xsync_value, value & 0xffffffff, value >> 32);
}

static XSyncAlarm
xsync_user_active_alarm_set (MetaBackendX11Private *priv)
{
  XSyncAlarmAttributes attr;
  XSyncValue delta;
  unsigned long flags;

  flags = (XSyncCACounter | XSyncCAValueType | XSyncCATestType |
           XSyncCAValue | XSyncCADelta | XSyncCAEvents);

  XSyncIntToValue (&delta, 0);
  attr.trigger.counter = priv->counter;
  attr.trigger.value_type = XSyncAbsolute;
  attr.delta = delta;
  attr.events = TRUE;

  uint64_to_xsync_value (1, &attr.trigger.wait_value);

  attr.trigger.test_type = XSyncNegativeTransition;
  return XSyncCreateAlarm (priv->xdisplay, flags, &attr);
}

static XSyncCounter
find_idletime_counter (MetaBackendX11Private *priv)
{
  int i;
  int n_counters;
  XSyncSystemCounter *counters;
  XSyncCounter counter = None;

  counters = XSyncListSystemCounters (priv->xdisplay, &n_counters);
  for (i = 0; i < n_counters; i++)
    {
      if (g_strcmp0 (counters[i].name, "IDLETIME") == 0)
        {
          counter = counters[i].counter;
          break;
        }
    }
  XSyncFreeSystemCounterList (counters);

  return counter;
}

static void
handle_alarm_notify (MetaBackend           *backend,
                     XSyncAlarmNotifyEvent *alarm_event)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  MetaIdleMonitor *idle_monitor;
  XSyncAlarmAttributes attr;
  ClutterBackend *clutter_backend;
  ClutterSeat *seat;
  ClutterInputDevice *pointer;

  if (alarm_event->state != XSyncAlarmActive ||
      alarm_event->alarm != priv->user_active_alarm)
    return;

  attr.events = TRUE;
  XSyncChangeAlarm (priv->xdisplay, priv->user_active_alarm,
                    XSyncCAEvents, &attr);

  clutter_backend = meta_backend_get_clutter_backend (backend);
  seat = clutter_backend_get_default_seat (clutter_backend);
  pointer = clutter_seat_get_pointer (seat);
  idle_monitor = meta_backend_get_idle_monitor (backend, pointer);
  meta_idle_monitor_reset_idletime (idle_monitor);
}

static void
meta_backend_x11_translate_device_event (MetaBackendX11 *x11,
                                         XIDeviceEvent  *device_event)
{
  MetaBackendX11Class *backend_x11_class =
    META_BACKEND_X11_GET_CLASS (x11);

  backend_x11_class->translate_device_event (x11, device_event);
}

static void
maybe_translate_touch_replay_pointer_event (MetaBackendX11 *x11,
                                            XIDeviceEvent  *device_event)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  if (!device_event->send_event &&
      device_event->time != META_CURRENT_TIME &&
      priv->current_touch_replay_sync_serial !=
      priv->pending_touch_replay_sync_serial &&
      XSERVER_TIME_IS_BEFORE (device_event->time, priv->latest_evtime))
    {
      /* Emulated pointer events received after XIRejectTouch is received
       * on a passive touch grab will contain older timestamps, update those
       * so we dont get InvalidTime at grabs.
       */
      device_event->time = priv->latest_evtime;
    }
}

static void
translate_device_event (MetaBackendX11 *x11,
                        XIDeviceEvent  *device_event)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  meta_backend_x11_translate_device_event (x11, device_event);

  if (!device_event->send_event && device_event->time != META_CURRENT_TIME)
    priv->latest_evtime = device_event->time;
}

static void
meta_backend_x11_translate_crossing_event (MetaBackendX11 *x11,
                                           XIEnterEvent   *enter_event)
{
  MetaBackendX11Class *backend_x11_class =
    META_BACKEND_X11_GET_CLASS (x11);

  if (backend_x11_class->translate_crossing_event)
    backend_x11_class->translate_crossing_event (x11, enter_event);
}

static void
translate_crossing_event (MetaBackendX11 *x11,
                          XIEnterEvent   *enter_event)
{
  /* Throw out weird events generated by grabs. */
  if (enter_event->mode == XINotifyGrab ||
      enter_event->mode == XINotifyUngrab)
    {
      enter_event->event = None;
      return;
    }

  meta_backend_x11_translate_crossing_event (x11, enter_event);
}

/* Clutter makes the assumption that there is only one X window
 * per stage, which is a valid assumption to make for a generic
 * application toolkit. As such, it will ignore any events sent
 * to the a stage that isn't its X window.
 *
 * When running as an X window manager, we need to respond to
 * events from lots of windows. Trick Clutter into translating
 * these events by pretending we got an event on the stage window.
 */
static void
maybe_spoof_event_as_stage_event (MetaBackendX11 *x11,
                                  XIEvent        *input_event)
{
  switch (input_event->evtype)
    {
    case XI_Motion:
    case XI_ButtonPress:
    case XI_ButtonRelease:
      maybe_translate_touch_replay_pointer_event (x11,
                                                  (XIDeviceEvent *) input_event);
      G_GNUC_FALLTHROUGH;
    case XI_KeyPress:
    case XI_KeyRelease:
    case XI_TouchBegin:
    case XI_TouchUpdate:
    case XI_TouchEnd:
      translate_device_event (x11, (XIDeviceEvent *) input_event);
      break;
    case XI_Enter:
    case XI_Leave:
      translate_crossing_event (x11, (XIEnterEvent *) input_event);
      break;
    default:
      break;
    }
}

static gboolean
handle_input_event (MetaBackendX11 *x11,
                    XEvent         *event)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  if (event->type == GenericEvent &&
      event->xcookie.extension == priv->xinput_opcode)
    {
      XIEvent *input_event = (XIEvent *) event->xcookie.data;
      MetaX11Barriers *barriers;

      barriers = meta_backend_x11_get_barriers (x11);
      if (barriers &&
          meta_x11_barriers_process_xevent (barriers, input_event))
        return TRUE;

      maybe_spoof_event_as_stage_event (x11, input_event);
    }

  return FALSE;
}

static void
keymap_changed (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  if (priv->keymap)
    {
      xkb_keymap_unref (priv->keymap);
      priv->keymap = NULL;
    }

  g_signal_emit_by_name (backend, "keymap-changed", 0);
}

static gboolean
meta_backend_x11_handle_host_xevent (MetaBackendX11 *backend_x11,
                                     XEvent         *event)
{
  MetaBackendX11Class *backend_x11_class =
    META_BACKEND_X11_GET_CLASS (backend_x11);

  return backend_x11_class->handle_host_xevent (backend_x11, event);
}

static void
handle_host_xevent (MetaBackend *backend,
                    XEvent      *event)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  gboolean bypass_clutter = FALSE;

  switch (event->type)
    {
    case ClientMessage:
      if (event->xclient.window == meta_backend_x11_get_xwindow (x11) &&
          event->xclient.message_type == priv->touch_replay_sync_atom)
        priv->current_touch_replay_sync_serial = event->xclient.data.l[0];
      break;
    default:
      break;
    }

  XGetEventData (priv->xdisplay, &event->xcookie);

  bypass_clutter = meta_backend_x11_handle_host_xevent (x11, event);

  if (event->type == (priv->xsync_event_base + XSyncAlarmNotify))
    handle_alarm_notify (backend, (XSyncAlarmNotifyEvent *) event);

  if (event->type == priv->xkb_event_base)
    {
      XkbEvent *xkb_ev = (XkbEvent *) event;

      if (xkb_ev->any.device == META_VIRTUAL_CORE_KEYBOARD_ID)
        {
          switch (xkb_ev->any.xkb_type)
            {
            case XkbNewKeyboardNotify:
            case XkbMapNotify:
              keymap_changed (backend);
              break;
            case XkbStateNotify:
              if (xkb_ev->state.changed & XkbGroupLockMask)
                {
                  int layout_group;
                  gboolean layout_group_changed;

                  layout_group = xkb_ev->state.locked_group;
                  layout_group_changed =
                    (int) priv->keymap_layout_group != layout_group;
                  priv->keymap_layout_group = layout_group;

                  if (layout_group_changed)
                    meta_backend_notify_keymap_layout_group_changed (backend,
                                                                     layout_group);
                }
              break;
            case XkbControlsNotify:
              /* 'event_type' is set to zero on notifying us of updates in
               * response to client requests (including our own) and non-zero
               * to notify us of key/mouse events causing changes (like
               * pressing shift 5 times to enable sticky keys).
               *
               * We only want to update our settings when it's in response to an
               * explicit user input event, so require a non-zero event_type.
               */
              if (xkb_ev->ctrls.event_type != 0)
                meta_seat_x11_check_xkb_a11y_settings_changed (seat);
            default:
              break;
            }
        }
    }

  if (!bypass_clutter)
    {
      if (handle_input_event (x11, event))
        goto done;

      meta_backend_x11_handle_event (backend, event);
    }

done:
  XFreeEventData (priv->xdisplay, &event->xcookie);
}

typedef struct {
  GSource base;
  GPollFD event_poll_fd;
  MetaBackend *backend;
} XEventSource;

static gboolean
x_event_source_prepare (GSource *source,
                        int     *timeout)
{
  XEventSource *x_source = (XEventSource *) source;
  MetaBackend *backend = x_source->backend;
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  *timeout = -1;

  return XPending (priv->xdisplay);
}

static gboolean
x_event_source_check (GSource *source)
{
  XEventSource *x_source = (XEventSource *) source;
  MetaBackend *backend = x_source->backend;
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  return XPending (priv->xdisplay);
}

static gboolean
x_event_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  XEventSource *x_source = (XEventSource *) source;
  MetaBackend *backend = x_source->backend;
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  while (XPending (priv->xdisplay))
    {
      XEvent event;

      XNextEvent (priv->xdisplay, &event);

      handle_host_xevent (backend, &event);
    }

  return TRUE;
}

static GSourceFuncs x_event_funcs = {
  x_event_source_prepare,
  x_event_source_check,
  x_event_source_dispatch,
};

static GSource *
x_event_source_new (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  GSource *source;
  XEventSource *x_source;

  source = g_source_new (&x_event_funcs, sizeof (XEventSource));
  g_source_set_name (source, "[mutter] X events");
  x_source = (XEventSource *) source;
  x_source->backend = backend;
  x_source->event_poll_fd.fd = ConnectionNumber (priv->xdisplay);
  x_source->event_poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &x_source->event_poll_fd);

  g_source_attach (source, NULL);
  return source;
}

static void
on_monitors_changed (MetaMonitorManager *manager,
                     MetaBackend        *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);

  meta_backend_x11_reset_cached_logical_monitor (x11);
}

static void
on_kbd_a11y_changed (MetaInputSettings   *input_settings,
                     MetaKbdA11ySettings *a11y_settings,
                     MetaBackend         *backend)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);

  meta_seat_x11_apply_kbd_a11y_settings (seat, a11y_settings);
}

static void
meta_backend_x11_post_init (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  MetaMonitorManager *monitor_manager;
  ClutterBackend *clutter_backend;
  ClutterSeat *seat;
  MetaInputSettings *input_settings;
  int major, minor;

  priv->source = x_event_source_new (backend);

  if (!XSyncQueryExtension (priv->xdisplay, &priv->xsync_event_base, &priv->xsync_error_base) ||
      !XSyncInitialize (priv->xdisplay, &major, &minor))
    meta_fatal ("Could not initialize XSync");

  priv->counter = find_idletime_counter (priv);
  if (priv->counter == None)
    meta_fatal ("Could not initialize XSync counter");

  priv->user_active_alarm = xsync_user_active_alarm_set (priv);

  if (!xkb_x11_setup_xkb_extension (priv->xcb,
                                    XKB_X11_MIN_MAJOR_XKB_VERSION,
                                    XKB_X11_MIN_MINOR_XKB_VERSION,
                                    XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                    NULL, NULL,
                                    &priv->xkb_event_base,
                                    &priv->xkb_error_base))
    meta_fatal ("X server doesn't have the XKB extension, version %d.%d or newer",
                XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION);

  META_BACKEND_CLASS (meta_backend_x11_parent_class)->post_init (backend);

  monitor_manager = meta_backend_get_monitor_manager (backend);
  g_signal_connect (monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed), backend);

  priv->touch_replay_sync_atom = XInternAtom (priv->xdisplay,
                                              "_MUTTER_TOUCH_SEQUENCE_SYNC",
                                              False);

  clutter_backend = meta_backend_get_clutter_backend (backend);
  seat = clutter_backend_get_default_seat (clutter_backend);
  meta_seat_x11_notify_devices (META_SEAT_X11 (seat),
                                CLUTTER_STAGE (meta_backend_get_stage (backend)));

  input_settings = meta_backend_get_input_settings (backend);

  if (input_settings)
    {
      g_signal_connect_object (meta_backend_get_input_settings (backend),
                               "kbd-a11y-changed",
                               G_CALLBACK (on_kbd_a11y_changed), backend, 0);

      if (meta_input_settings_maybe_restore_numlock_state (input_settings))
        {
          unsigned int num_mask;

          num_mask = XkbKeysymToModifiers (priv->xdisplay, XK_Num_Lock);
          XkbLockModifiers (priv->xdisplay, XkbUseCoreKbd, num_mask, num_mask);
        }
    }
}

static ClutterBackend *
meta_backend_x11_create_clutter_backend (MetaBackend *backend)
{
  return CLUTTER_BACKEND (meta_clutter_backend_x11_new (backend));
}

static MetaColorManager *
meta_backend_x11_create_color_manager (MetaBackend *backend)
{
  return g_object_new (META_TYPE_COLOR_MANAGER_X11,
                       "backend", backend,
                       NULL);
}

static ClutterSeat *
meta_backend_x11_create_default_seat (MetaBackend  *backend,
                                      GError      **error)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  int event_base, first_event, first_error;
  int major, minor;
  MetaSeatX11 *seat_x11;

  if (!XQueryExtension (priv->xdisplay,
                        "XInputExtension",
                        &event_base,
                        &first_event,
                        &first_error))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to query XInputExtension");
      return NULL;
    }

  major = 2;
  minor = 3;
  if (XIQueryVersion (priv->xdisplay, &major, &minor) == BadRequest)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Incompatible XInputExtension version");
      return NULL;
    }

  seat_x11 = meta_seat_x11_new (backend,
                                event_base,
                                META_VIRTUAL_CORE_POINTER_ID,
                                META_VIRTUAL_CORE_KEYBOARD_ID);
  return CLUTTER_SEAT (seat_x11);
}

static gboolean
meta_backend_x11_grab_device (MetaBackend *backend,
                              int          device_id,
                              uint32_t     timestamp)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };
  int ret;

  if (timestamp != META_CURRENT_TIME &&
      XSERVER_TIME_IS_BEFORE (timestamp, priv->latest_evtime))
    timestamp = priv->latest_evtime;

  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Enter);
  XISetMask (mask.mask, XI_Leave);
  XISetMask (mask.mask, XI_Motion);
  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  ret = XIGrabDevice (priv->xdisplay, device_id,
                      meta_backend_x11_get_xwindow (x11),
                      timestamp,
                      None,
                      XIGrabModeAsync, XIGrabModeAsync,
                      False, /* owner_events */
                      &mask);

  return (ret == Success);
}

static gboolean
meta_backend_x11_ungrab_device (MetaBackend *backend,
                                int          device_id,
                                uint32_t     timestamp)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  int ret;

  ret = XIUngrabDevice (priv->xdisplay, device_id, timestamp);
  XFlush (priv->xdisplay);

  return (ret == Success);
}

static void
meta_backend_x11_freeze_keyboard (MetaBackend *backend,
                                  uint32_t     timestamp)
{
  MetaBackendX11 *backend_x11;
  Window xwindow;
  Display *xdisplay;

  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  /* Grab the keyboard, so we get key releases and all key
   * presses
   */

  backend_x11 = META_BACKEND_X11 (backend);
  xwindow = meta_backend_x11_get_xwindow (backend_x11);
  xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  /* Strictly, we only need to set grab_mode on the keyboard device
   * while the pointer should always be XIGrabModeAsync. Unfortunately
   * there is a bug in the X server, only fixed (link below) in 1.15,
   * which swaps these arguments for keyboard devices. As such, we set
   * both the device and the paired device mode which works around
   * that bug and also works on fixed X servers.
   *
   * http://cgit.freedesktop.org/xorg/xserver/commit/?id=9003399708936481083424b4ff8f18a16b88b7b3
   */
  XIGrabDevice (xdisplay,
                META_VIRTUAL_CORE_KEYBOARD_ID,
                xwindow,
                timestamp,
                None,
                XIGrabModeSync, XIGrabModeSync,
                False, /* owner_events */
                &mask);
}

static void
meta_backend_x11_unfreeze_keyboard (MetaBackend *backend,
                                    uint32_t     timestamp)
{
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  XIAllowEvents (xdisplay, META_VIRTUAL_CORE_KEYBOARD_ID,
                 XIAsyncDevice, timestamp);
  /* We shouldn't need to unfreeze the pointer device here, however we
   * have to, due to the workaround we do in grab_keyboard().
   */
  XIAllowEvents (xdisplay, META_VIRTUAL_CORE_POINTER_ID,
                 XIAsyncDevice, timestamp);
}

static void
meta_backend_x11_ungrab_keyboard (MetaBackend *backend,
                                  uint32_t     timestamp)
{
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  XIUngrabDevice (xdisplay, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp);
}

static void
meta_backend_x11_finish_touch_sequence (MetaBackend          *backend,
                                        ClutterEventSequence *sequence,
                                        MetaSequenceState     state)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  int event_mode;
  int err;

  if (state == META_SEQUENCE_ACCEPTED)
    event_mode = XIAcceptTouch;
  else if (state == META_SEQUENCE_REJECTED)
    event_mode = XIRejectTouch;
  else
    g_return_if_reached ();

  mtk_x11_error_trap_push (priv->xdisplay);
  XIAllowTouchEvents (priv->xdisplay,
                      META_VIRTUAL_CORE_POINTER_ID,
                      clutter_event_sequence_get_slot (sequence),
                      DefaultRootWindow (priv->xdisplay), event_mode);
  err = mtk_x11_error_trap_pop_with_return (priv->xdisplay);
  if (err)
    {
      g_debug ("XIAllowTouchEvents failed event_mode %d with error %d",
               event_mode, err);
    }

  if (state == META_SEQUENCE_REJECTED)
    {
      XClientMessageEvent ev;

      ev = (XClientMessageEvent) {
        .type = ClientMessage,
        .window = meta_backend_x11_get_xwindow (x11),
        .message_type = priv->touch_replay_sync_atom,
        .format = 32,
        .data.l[0] = ++priv->pending_touch_replay_sync_serial,
      };
      XSendEvent (priv->xdisplay, meta_backend_x11_get_xwindow (x11),
                  False, 0, (XEvent *) &ev);
    }
}

static MetaLogicalMonitor *
meta_backend_x11_get_current_logical_monitor (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  MetaCursorTracker *cursor_tracker;
  graphene_point_t point;
  MetaMonitorManager *monitor_manager;
  MetaLogicalMonitor *logical_monitor;

  if (priv->cached_current_logical_monitor)
    return priv->cached_current_logical_monitor;

  cursor_tracker = meta_backend_get_cursor_tracker (backend);
  meta_cursor_tracker_get_pointer (cursor_tracker, &point, NULL);
  monitor_manager = meta_backend_get_monitor_manager (backend);
  logical_monitor =
    meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                 point.x, point.y);

  if (!logical_monitor && monitor_manager->logical_monitors)
    logical_monitor = monitor_manager->logical_monitors->data;

  priv->cached_current_logical_monitor = logical_monitor;
  return priv->cached_current_logical_monitor;
}

static struct xkb_keymap *
meta_backend_x11_get_keymap (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  if (priv->keymap == NULL)
    {
      struct xkb_context *context = meta_create_xkb_context ();
      priv->keymap = xkb_x11_keymap_new_from_device (context,
                                                     priv->xcb,
                                                     xkb_x11_get_core_keyboard_device_id (priv->xcb),
                                                     XKB_KEYMAP_COMPILE_NO_FLAGS);
      if (priv->keymap == NULL)
        priv->keymap = xkb_keymap_new_from_names (context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

      xkb_context_unref (context);
    }

  return priv->keymap;
}

static xkb_layout_index_t
meta_backend_x11_get_keymap_layout_group (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  return priv->keymap_layout_group;
}

void
meta_backend_x11_reset_cached_logical_monitor (MetaBackendX11 *backend_x11)
{
  MetaBackendX11Private *priv =
    meta_backend_x11_get_instance_private (backend_x11);

  priv->cached_current_logical_monitor = NULL;
}

uint8_t
meta_backend_x11_get_xkb_event_base (MetaBackendX11 *x11)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  return priv->xkb_event_base;
}

static void
init_xkb_state (MetaBackendX11 *x11)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  struct xkb_keymap *keymap;
  int32_t device_id;
  struct xkb_state *state;

  keymap = meta_backend_get_keymap (META_BACKEND (x11));

  device_id = xkb_x11_get_core_keyboard_device_id (priv->xcb);
  state = xkb_x11_state_new_from_device (keymap, priv->xcb, device_id);

  priv->keymap_layout_group =
    xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_LOCKED);

  xkb_state_unref (state);
}

static gboolean
init_xinput (MetaBackendX11  *backend_x11,
             GError         **error)
{
  MetaBackendX11Private *priv =
    meta_backend_x11_get_instance_private (backend_x11);
  gboolean has_xi = FALSE;

  if (XQueryExtension (priv->xdisplay,
                       "XInputExtension",
                       &priv->xinput_opcode,
                       &priv->xinput_error_base,
                       &priv->xinput_event_base))
    {
      int major, minor;

      major = 2; minor = 3;
      if (XIQueryVersion (priv->xdisplay, &major, &minor) == Success)
        {
          int version;

          version = (major * 10) + minor;
          if (version >= 22)
            has_xi = TRUE;

          if (version >= 23)
            priv->have_xinput_23 = TRUE;
        }
    }

  if (!has_xi)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "X server doesn't have the XInput extension, "
                   "version 2.2 or newer");
      return FALSE;
    }

  return TRUE;
}

static gboolean
meta_backend_x11_initable_init (GInitable    *initable,
                                GCancellable *cancellable,
                                GError      **error)
{
  MetaContext *context = meta_backend_get_context (META_BACKEND (initable));
  MetaBackendX11 *x11 = META_BACKEND_X11 (initable);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);
  Display *xdisplay;
  const char *xdisplay_name;

  xdisplay_name = g_getenv ("DISPLAY");
  if (!xdisplay_name)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to open display, DISPLAY not set");
      return FALSE;
    }

  xdisplay = XOpenDisplay (xdisplay_name);
  if (!xdisplay)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to open display '%s'", xdisplay_name);
      return FALSE;
    }

  XSynchronize (xdisplay, meta_context_is_x11_sync (context));

  priv->xdisplay = xdisplay;
  priv->xscreen = DefaultScreenOfDisplay (xdisplay);
  priv->xcb = XGetXCBConnection (priv->xdisplay);
  priv->root_window = DefaultRootWindow (xdisplay);

  init_xkb_state (x11);

  if (!init_xinput (x11, error))
    return FALSE;

  if (priv->have_xinput_23)
    priv->barriers = meta_x11_barriers_new (x11);

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_backend_x11_initable_init;
}

static void
meta_backend_x11_dispose (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendX11 *x11 = META_BACKEND_X11 (object);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  if (priv->keymap_state_changed_id)
    {
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
      ClutterKeymap *keymap;

      seat = clutter_backend_get_default_seat (clutter_backend);
      keymap = clutter_seat_get_keymap (seat);
      g_clear_signal_handler (&priv->keymap_state_changed_id, keymap);
    }

  if (priv->user_active_alarm != None)
    {
      XSyncDestroyAlarm (priv->xdisplay, priv->user_active_alarm);
      priv->user_active_alarm = None;
    }

  G_OBJECT_CLASS (meta_backend_x11_parent_class)->dispose (object);

  g_clear_pointer (&priv->barriers, meta_x11_barriers_free);
  g_clear_pointer (&priv->xdisplay, XCloseDisplay);
}

static void
meta_backend_x11_finalize (GObject *object)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (object);
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  g_clear_pointer (&priv->keymap, xkb_keymap_unref);

  mtk_x11_errors_deinit ();

  G_OBJECT_CLASS (meta_backend_x11_parent_class)->finalize (object);
}

static void
meta_backend_x11_class_init (MetaBackendX11Class *klass)
{
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_backend_x11_dispose;
  object_class->finalize = meta_backend_x11_finalize;
  backend_class->create_clutter_backend = meta_backend_x11_create_clutter_backend;
  backend_class->create_color_manager = meta_backend_x11_create_color_manager;
  backend_class->create_default_seat = meta_backend_x11_create_default_seat;
  backend_class->post_init = meta_backend_x11_post_init;
  backend_class->grab_device = meta_backend_x11_grab_device;
  backend_class->ungrab_device = meta_backend_x11_ungrab_device;
  backend_class->freeze_keyboard = meta_backend_x11_freeze_keyboard;
  backend_class->unfreeze_keyboard = meta_backend_x11_unfreeze_keyboard;
  backend_class->ungrab_keyboard = meta_backend_x11_ungrab_keyboard;
  backend_class->finish_touch_sequence = meta_backend_x11_finish_touch_sequence;
  backend_class->get_current_logical_monitor = meta_backend_x11_get_current_logical_monitor;
  backend_class->get_keymap = meta_backend_x11_get_keymap;
  backend_class->get_keymap_layout_group = meta_backend_x11_get_keymap_layout_group;
}

static void
meta_backend_x11_init (MetaBackendX11 *x11)
{
  XInitThreads ();
  mtk_x11_errors_init ();
}

Display *
meta_backend_x11_get_xdisplay (MetaBackendX11 *x11)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  return priv->xdisplay;
}

Screen *
meta_backend_x11_get_xscreen (MetaBackendX11 *x11)
{
  MetaBackendX11Private *priv = meta_backend_x11_get_instance_private (x11);

  return priv->xscreen;
}

Window
meta_backend_x11_get_root_xwindow (MetaBackendX11 *backend_x11)
{
  MetaBackendX11Private *priv =
    meta_backend_x11_get_instance_private (backend_x11);

  return priv->root_window;
}

Window
meta_backend_x11_get_xwindow (MetaBackendX11 *x11)
{
  ClutterActor *stage = meta_backend_get_stage (META_BACKEND (x11));
  return meta_x11_get_stage_window (CLUTTER_STAGE (stage));
}

void
meta_backend_x11_reload_cursor (MetaBackendX11 *x11)
{
  MetaBackend *backend = META_BACKEND (x11);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);

  meta_cursor_renderer_force_update (cursor_renderer);
}

void
meta_backend_x11_sync_pointer (MetaBackendX11 *backend_x11)
{
  MetaBackend *backend = META_BACKEND (backend_x11);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterInputDevice *pointer = clutter_seat_get_pointer (seat);
  ClutterModifierType modifiers;
  ClutterEvent *event;
  graphene_point_t pos;

  clutter_seat_query_state (seat, pointer, NULL, &pos, &modifiers);

  event = clutter_event_motion_new (CLUTTER_EVENT_FLAG_SYNTHETIC,
                                    CLUTTER_CURRENT_TIME,
                                    pointer,
                                    NULL,
                                    modifiers,
                                    pos,
                                    GRAPHENE_POINT_INIT (0, 0),
                                    GRAPHENE_POINT_INIT (0, 0),
                                    GRAPHENE_POINT_INIT (0, 0),
                                    NULL);

  clutter_event_put (event);
  clutter_event_free (event);
}

MetaX11Barriers *
meta_backend_x11_get_barriers (MetaBackendX11 *backend_x11)
{
  MetaBackendX11Private *priv =
    meta_backend_x11_get_instance_private (backend_x11);

  return priv->barriers;
}
