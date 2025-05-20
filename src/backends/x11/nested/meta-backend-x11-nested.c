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

#include "backends/x11/nested/meta-backend-x11-nested.h"

#include "backends/meta-input-settings-dummy.h"
#include "backends/meta-monitor-manager-dummy.h"
#include "backends/meta-stage-private.h"
#include "backends/x11/nested/meta-backend-x11-nested.h"
#include "backends/x11/nested/meta-cursor-renderer-x11-nested.h"
#include "backends/x11/nested/meta-renderer-x11-nested.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

struct _MetaBackendX11Nested
{
  MetaBackendX11 parent;

  MetaGpu *gpu;
  MetaCursorRenderer *cursor_renderer;
  MetaInputSettings *input_settings;
};

G_DEFINE_FINAL_TYPE (MetaBackendX11Nested,
                     meta_backend_x11_nested,
                     META_TYPE_BACKEND_X11)

static MetaRenderer *
meta_backend_x11_nested_create_renderer (MetaBackend *backend,
                                         GError     **error)
{
  return g_object_new (META_TYPE_RENDERER_X11_NESTED,
                       "backend", backend,
                       NULL);
}

static MetaMonitorManager *
meta_backend_x11_nested_create_monitor_manager (MetaBackend *backend,
                                                GError     **error)
{
  return g_object_new (META_TYPE_MONITOR_MANAGER_DUMMY,
                       "backend", backend,
                       NULL);
}

static MetaCursorRenderer *
meta_backend_x11_nested_get_cursor_renderer (MetaBackend   *backend,
                                             ClutterSprite *sprite)
{
  MetaBackendX11Nested *backend_x11_nested = META_BACKEND_X11_NESTED (backend);

  if (!backend_x11_nested->cursor_renderer)
    {
      backend_x11_nested->cursor_renderer =
        g_object_new (META_TYPE_CURSOR_RENDERER_X11_NESTED,
                      "backend", backend,
                      "sprite", sprite,
                      NULL);
    }

  return backend_x11_nested->cursor_renderer;
}

static MetaInputSettings *
meta_backend_x11_nested_get_input_settings (MetaBackend *backend)
{
  MetaBackendX11Nested *backend_x11_nested = META_BACKEND_X11_NESTED (backend);

  if (!backend_x11_nested->input_settings)
    {
      backend_x11_nested->input_settings =
        g_object_new (META_TYPE_INPUT_SETTINGS_DUMMY,
                      "backend", backend,
                      NULL);
    }

  return backend_x11_nested->input_settings;
}

static void
meta_backend_x11_nested_update_stage (MetaBackend *backend)
{
  ClutterActor *stage = meta_backend_get_stage (backend);

  meta_stage_rebuild_views (META_STAGE (stage));
}

static void
meta_backend_x11_nested_select_stage_events (MetaBackend *backend)
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

  /*
   * When we're an X11 compositor, we can't take these events or else replaying
   * events from our passive root window grab will cause them to come back to
   * us.
   *
   * When we're a nested application, we want to behave like any other
   * application, so select these events like normal apps do.
   */
  XISetMask (mask.mask, XI_TouchBegin); XISetMask (mask.mask, XI_TouchEnd);
  XISetMask (mask.mask, XI_TouchUpdate);

  XISelectEvents (xdisplay, xwin, &mask, 1);

  /*
   * We have no way of tracking key changes when the stage doesn't have focus,
   * so we select for KeymapStateMask so that we get a complete dump of the
   * keyboard state in a KeymapNotify event that immediately follows each
   * FocusIn (and EnterNotify, but we ignore that.)
   */
  XWindowAttributes xwa;

  XGetWindowAttributes(xdisplay, xwin, &xwa);
  XSelectInput(xdisplay, xwin,
               xwa.your_event_mask | FocusChangeMask | KeymapStateMask);
}

static void
meta_backend_x11_nested_set_keymap_async (MetaBackend *backend,
                                          const char  *layouts,
                                          const char  *variants,
                                          const char  *options,
                                          const char  *model,
                                          GTask       *task)
{
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static void
meta_backend_x11_nested_set_keymap_layout_group_async (MetaBackend        *backend,
                                                       xkb_layout_index_t  idx,
                                                       GTask              *task)
{
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gboolean
meta_backend_x11_nested_is_lid_closed (MetaBackend *backend)
{
  return FALSE;
}

static void
meta_backend_x11_nested_set_pointer_constraint (MetaBackend           *backend,
                                                MetaPointerConstraint *constraint)
{
  g_debug ("Ignored pointer constraint in nested backend");
}

static gboolean
meta_backend_x11_nested_handle_host_xevent (MetaBackendX11 *x11,
                                            XEvent         *event)
{
#ifdef HAVE_WAYLAND
  if (event->type == FocusIn)
    {
      Window xwin = meta_backend_x11_get_xwindow (x11);
      XEvent xev;

      if (event->xfocus.window == xwin)
        {
          MetaBackend *backend = META_BACKEND (x11);
          MetaContext *context = meta_backend_get_context (backend);
          MetaWaylandCompositor *compositor =
            meta_context_get_wayland_compositor (context);
          Display *xdisplay = meta_backend_x11_get_xdisplay (x11);

          /*
           * Since we've selected for KeymapStateMask, every FocusIn is
           * followed immediately by a KeymapNotify event.
           */
          XMaskEvent (xdisplay, KeymapStateMask, &xev);
          meta_wayland_compositor_update_key_state (compositor,
                                                    xev.xkeymap.key_vector,
                                                    32, 8);
        }
    }
#endif

  return FALSE;
}

static void
meta_backend_x11_nested_translate_device_event (MetaBackendX11 *x11,
                                                XIDeviceEvent  *device_event)
{
  /* This codepath should only ever trigger as an X11 compositor,
   * and never under nested, as under nested all backend events
   * should be reported with respect to the stage window.
   */
  g_assert (device_event->event == meta_backend_x11_get_xwindow (x11));
}

static void
init_gpus (MetaBackendX11Nested *backend_x11_nested)
{
  backend_x11_nested->gpu = g_object_new (META_TYPE_GPU_DUMMY,
                                          "backend", backend_x11_nested,
                                          NULL);
  meta_backend_add_gpu (META_BACKEND (backend_x11_nested),
                        backend_x11_nested->gpu);
}

static MetaBackendCapabilities
meta_backend_x11_nested_get_capabilities (MetaBackend *backend)
{
  return META_BACKEND_CAPABILITY_NONE;
}

static gboolean
meta_backend_x11_nested_init_basic (MetaBackend  *backend,
                                    GError      **error)
{
  MetaBackendX11Nested *backend_x11_nested = META_BACKEND_X11_NESTED (backend);
  MetaBackendClass *parent_backend_class =
    META_BACKEND_CLASS (meta_backend_x11_nested_parent_class);

  if (!parent_backend_class->init_basic (backend, error))
    return FALSE;

  init_gpus (backend_x11_nested);

  return TRUE;
}

static void
meta_backend_x11_nested_dispose (GObject *object)
{
  MetaBackendX11Nested *backend_x11_nested = META_BACKEND_X11_NESTED (object);

  g_clear_object (&backend_x11_nested->input_settings);
  g_clear_object (&backend_x11_nested->cursor_renderer);

  G_OBJECT_CLASS (meta_backend_x11_nested_parent_class)->dispose (object);
}

static void
meta_backend_x11_nested_init (MetaBackendX11Nested *backend_x11_nested)
{
}

static void
meta_backend_x11_nested_class_init (MetaBackendX11NestedClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  MetaBackendX11Class *backend_x11_class = META_BACKEND_X11_CLASS (klass);

  object_class->dispose = meta_backend_x11_nested_dispose;

  backend_class->init_basic = meta_backend_x11_nested_init_basic;
  backend_class->get_capabilities = meta_backend_x11_nested_get_capabilities;
  backend_class->create_renderer = meta_backend_x11_nested_create_renderer;
  backend_class->create_monitor_manager = meta_backend_x11_nested_create_monitor_manager;
  backend_class->get_cursor_renderer = meta_backend_x11_nested_get_cursor_renderer;
  backend_class->get_input_settings = meta_backend_x11_nested_get_input_settings;
  backend_class->update_stage = meta_backend_x11_nested_update_stage;
  backend_class->select_stage_events = meta_backend_x11_nested_select_stage_events;
  backend_class->set_keymap_async = meta_backend_x11_nested_set_keymap_async;
  backend_class->set_keymap_layout_group_async = meta_backend_x11_nested_set_keymap_layout_group_async;
  backend_class->is_lid_closed = meta_backend_x11_nested_is_lid_closed;
  backend_class->set_pointer_constraint = meta_backend_x11_nested_set_pointer_constraint;

  backend_x11_class->handle_host_xevent = meta_backend_x11_nested_handle_host_xevent;
  backend_x11_class->translate_device_event = meta_backend_x11_nested_translate_device_event;
}
