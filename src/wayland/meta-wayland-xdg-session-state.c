/*
 * Copyright (C) 2024 Red Hat
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
 */

#include "config.h"

#include "wayland/meta-wayland-xdg-session-state.h"

#include <gio/gio.h>

#include "backends/meta-monitor-private.h"
#include "core/meta-context-private.h"
#include "core/window-private.h"
#include "wayland/meta-wayland.h"

#define STATE_FORMAT_VERSION 1

typedef enum _WindowState
{
  WINDOW_STATE_NONE = 0,
  /* floating */
  WINDOW_STATE_FLOATING = 1,
  /* tiling */
  WINDOW_STATE_MAXIMIZED = 2,
  WINDOW_STATE_TILED_LEFT = 3,
  WINDOW_STATE_TILED_RIGHT = 4,
  WINDOW_STATE_FULLSCREEN = 5,
} WindowState;

struct _MetaWaylandXdgToplevelState
{
  MetaWaylandXdgSessionState *session_state;

  WindowState window_state;
  struct {
    MtkRectangle rect;
  } floating;
  struct {
    MtkRectangle rect;
  } tiled;
  gboolean is_minimized;
  int workspace_idx;
};

struct _MetaWaylandXdgSessionState
{
  MetaSessionState parent;

  GHashTable *toplevels;
};

G_DEFINE_TYPE (MetaWaylandXdgSessionState,
               meta_wayland_xdg_session_state,
               META_TYPE_SESSION_STATE)

static GVariant *
new_rect_variant (const MtkRectangle *rect)
{
  return g_variant_new ("(iiii)",
                        rect->x,
                        rect->y,
                        rect->width,
                        rect->height);
}

static void
variant_to_rect (GVariant     *variant,
                 MtkRectangle *rect)
{
  g_variant_get (variant, "(iiii)",
                 &rect->x,
                 &rect->y,
                 &rect->width,
                 &rect->height);
}

static void
meta_wayland_xdg_toplevel_state_free (MetaWaylandXdgToplevelState *toplevel_state)
{
  g_free (toplevel_state);
}

static const char *
window_state_to_string (WindowState state)
{
  switch (state)
    {
    case WINDOW_STATE_NONE:
      return "none";
    case WINDOW_STATE_FLOATING:
      return "floating";
    case WINDOW_STATE_MAXIMIZED:
      return "maximized";
    case WINDOW_STATE_TILED_LEFT:
      return "tiled-left";
    case WINDOW_STATE_TILED_RIGHT:
      return "tiled-right";
    case WINDOW_STATE_FULLSCREEN:
      return "fullscreen";
    }

  g_assert_not_reached ();
}

static char *
meta_wayland_xdg_toplevel_state_to_string (MetaWaylandXdgToplevelState *state)
{
  GString *str = NULL;

  str = g_string_new (NULL);

  g_string_append (str, window_state_to_string (state->window_state));

  switch (state->window_state)
    {
    case WINDOW_STATE_NONE:
      break;
    case WINDOW_STATE_FLOATING:
      g_string_append_printf (str, " Rect [%d,%d +%d,%d]",
                              state->floating.rect.x,
                              state->floating.rect.y,
                              state->floating.rect.width,
                              state->floating.rect.height);
      break;
    case WINDOW_STATE_MAXIMIZED:
    case WINDOW_STATE_TILED_LEFT:
    case WINDOW_STATE_TILED_RIGHT:
    case WINDOW_STATE_FULLSCREEN:
      g_string_append_printf (str, " Rect [%d,%d +%d,%d]",
                              state->tiled.rect.x,
                              state->tiled.rect.y,
                              state->tiled.rect.width,
                              state->tiled.rect.height);
      break;
    }

  return g_string_free_and_steal (str);
}

static MetaWaylandXdgToplevelState *
meta_wayland_xdg_session_state_ensure_toplevel (MetaWaylandXdgSessionState *session_state,
                                                const char                 *name)
{
  MetaWaylandXdgToplevelState *toplevel_state;

  toplevel_state = g_hash_table_lookup (session_state->toplevels, name);
  if (!toplevel_state)
    {
      toplevel_state = g_new0 (MetaWaylandXdgToplevelState, 1);
      toplevel_state->session_state = session_state;
      g_hash_table_insert (session_state->toplevels,
                           g_strdup (name), toplevel_state);
    }

  return toplevel_state;
}

static void
meta_wayland_xdg_session_state_dispose (GObject *object)
{
  MetaWaylandXdgSessionState *session_state =
    META_WAYLAND_XDG_SESSION_STATE (object);

  g_clear_pointer (&session_state->toplevels, g_hash_table_unref);

  G_OBJECT_CLASS (meta_wayland_xdg_session_state_parent_class)->dispose (object);
}

static gboolean
meta_wayland_xdg_session_state_serialize (MetaSessionState *session_state,
                                          GHashTable       *gvdb_data)
{
  MetaWaylandXdgSessionState *xdg_session_state =
    META_WAYLAND_XDG_SESSION_STATE (session_state);
  GHashTable *toplevels;
  GHashTableIter iter;
  gpointer key, value;
  GvdbItem *item;

  item = gvdb_hash_table_insert (gvdb_data, "version");
  gvdb_item_set_value (item, g_variant_new ("i", STATE_FORMAT_VERSION));

  item = gvdb_hash_table_insert (gvdb_data, "last-used");
  gvdb_item_set_value (item, g_variant_new ("x", g_get_real_time ()));

  toplevels = gvdb_hash_table_new (gvdb_data, "toplevels");

  g_hash_table_iter_init (&iter, xdg_session_state->toplevels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      char *name = key;
      MetaWaylandXdgToplevelState *toplevel_state = value;
      GHashTable *toplevel;

      meta_topic (META_DEBUG_SESSION_MANAGEMENT,
                  "Serializing toplevel state %s", name);

      toplevel = gvdb_hash_table_new (toplevels, name);

      item = gvdb_hash_table_insert (toplevel, "state");
      gvdb_item_set_value (item, g_variant_new ("u", toplevel_state->window_state));

      switch (toplevel_state->window_state)
        {
        case WINDOW_STATE_NONE:
          break;
        case WINDOW_STATE_FLOATING:
          item = gvdb_hash_table_insert (toplevel, "floating-rect");
          gvdb_item_set_value (item, new_rect_variant (&toplevel_state->floating.rect));
          break;
        case WINDOW_STATE_MAXIMIZED:
        case WINDOW_STATE_TILED_LEFT:
        case WINDOW_STATE_TILED_RIGHT:
        case WINDOW_STATE_FULLSCREEN:
          item = gvdb_hash_table_insert (toplevel, "tiled-rect");
          gvdb_item_set_value (item, new_rect_variant (&toplevel_state->tiled.rect));
          break;
        }

      item = gvdb_hash_table_insert (toplevel, "is-minimized");
      gvdb_item_set_value (item, g_variant_new_boolean (toplevel_state->is_minimized));

      item = gvdb_hash_table_insert (toplevel, "workspace");
      gvdb_item_set_value (item,
                           g_variant_new_int32 (toplevel_state->workspace_idx));
    }

  return TRUE;
}

static gboolean
meta_wayland_xdg_session_state_parse (MetaSessionState  *session_state,
                                      GvdbTable         *data,
                                      GError           **error)
{
  MetaWaylandXdgSessionState *xdg_session_state =
    META_WAYLAND_XDG_SESSION_STATE (session_state);
  g_autoptr (GVariant) version = NULL;
  GvdbTable *toplevels, *toplevel;
  GStrv toplevel_names = NULL;
  int i;

  version = gvdb_table_get_value (data, "version");
  if (!g_variant_is_of_type (version, G_VARIANT_TYPE ("i")) ||
      g_variant_get_int32 (version) > STATE_FORMAT_VERSION)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Too new session-data version");
      return FALSE;
    }

  toplevels = gvdb_table_get_table (data, "toplevels");
  toplevel_names = gvdb_table_get_names (toplevels, NULL);

  for (i = 0; toplevel_names[i]; i++)
    {
      MetaWaylandXdgToplevelState * toplevel_state;
      g_autoptr (GVariant) state = NULL, floating_rect = NULL, tiled_rect = NULL;
      g_autoptr (GVariant) is_minimized = NULL, monitor = NULL, workspace = NULL;

      meta_topic (META_DEBUG_SESSION_MANAGEMENT,
                  "Parsing toplevel state %s", toplevel_names[i]);

      toplevel_state =
        meta_wayland_xdg_session_state_ensure_toplevel (xdg_session_state,
                                                        toplevel_names[i]);

      toplevel = gvdb_table_get_table (toplevels, toplevel_names[i]);

      state = gvdb_table_get_value (toplevel, "state");

      if (state && g_variant_is_of_type (state, G_VARIANT_TYPE ("u")))
        toplevel_state->window_state = g_variant_get_uint32 (state);

      floating_rect = gvdb_table_get_value (toplevel, "floating-rect");
      if (floating_rect && g_variant_is_of_type (floating_rect, G_VARIANT_TYPE ("(iiii)")))
        {
          variant_to_rect (floating_rect, &toplevel_state->floating.rect);
        }

      tiled_rect = gvdb_table_get_value (toplevel, "tiled-rect");
      if (tiled_rect && g_variant_is_of_type (tiled_rect, G_VARIANT_TYPE ("(iiii)")))
        {
          variant_to_rect (tiled_rect, &toplevel_state->tiled.rect);
        }

      is_minimized = gvdb_table_get_value (toplevel, "is-minimized");
      if (state && g_variant_is_of_type (state, G_VARIANT_TYPE ("b")))
        toplevel_state->is_minimized = g_variant_get_boolean (is_minimized);

      workspace = gvdb_table_get_value (toplevel, "workspace");
      if (workspace && g_variant_is_of_type (workspace, G_VARIANT_TYPE ("i")))
        toplevel_state->workspace_idx = g_variant_get_int32 (workspace);

      gvdb_table_free (toplevel);
    }

  g_clear_pointer (&toplevel_names, g_strfreev);
  gvdb_table_free (toplevels);

  return TRUE;
}

static void
meta_wayland_xdg_session_state_save_window (MetaSessionState *state,
                                            const char       *name,
                                            MetaWindow       *window)
{
  MetaWaylandXdgSessionState *xdg_session_state =
    META_WAYLAND_XDG_SESSION_STATE (state);
  MetaWaylandXdgToplevelState *toplevel_state;
  MtkRectangle rect;
  MetaTileMode tile_mode;

  toplevel_state =
    meta_wayland_xdg_session_state_ensure_toplevel (xdg_session_state,
                                                    name);
  rect = meta_window_config_get_rect (window->config);

  g_object_get (window,
                "minimized", &toplevel_state->is_minimized,
                NULL);

  tile_mode = meta_window_config_get_tile_mode (window->config);

  if (meta_window_is_maximized (window))
    {
      toplevel_state->window_state = WINDOW_STATE_MAXIMIZED;

      toplevel_state->tiled.rect = rect;
    }
  else if (tile_mode == META_TILE_LEFT ||
           tile_mode == META_TILE_RIGHT)
    {
      if (tile_mode == META_TILE_LEFT)
        toplevel_state->window_state = WINDOW_STATE_TILED_LEFT;
      else if (tile_mode == META_TILE_RIGHT)
        toplevel_state->window_state = WINDOW_STATE_TILED_RIGHT;

      toplevel_state->tiled.rect = rect;
    }
  else if (meta_window_is_fullscreen (window))
    {
      toplevel_state->window_state = WINDOW_STATE_FULLSCREEN;

      toplevel_state->tiled.rect = rect;
    }
  else
    {
      toplevel_state->window_state = WINDOW_STATE_FLOATING;

      toplevel_state->floating.rect = rect;
    }

  toplevel_state->workspace_idx = meta_workspace_index (window->workspace);

  if (meta_is_topic_enabled (META_DEBUG_SESSION_MANAGEMENT))
    {
      g_autofree char *state_str = NULL;

      state_str =
        meta_wayland_xdg_toplevel_state_to_string (toplevel_state);

      meta_topic (META_DEBUG_SESSION_MANAGEMENT,
                  "Saved window state %s: %s", name, state_str);
    }
}


static MetaLogicalMonitor *
determine_monitor_for_rect (MetaWindow   *window,
                            MtkRectangle *target_rect)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);

  return meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager,
                                                             target_rect);
}

static gboolean
meta_wayland_xdg_session_state_restore_window (MetaSessionState *state,
                                               const char       *name,
                                               MetaWindow       *window)
{
  MetaWaylandXdgSessionState *xdg_session_state =
    META_WAYLAND_XDG_SESSION_STATE (state);
  MetaWaylandXdgToplevelState *toplevel_state;
  MtkRectangle *rect = NULL;
  MetaLogicalMonitor *target_monitor = NULL;

  toplevel_state = g_hash_table_lookup (xdg_session_state->toplevels, name);
  if (!toplevel_state)
    return FALSE;
  if (toplevel_state->window_state == WINDOW_STATE_NONE)
    return FALSE;

  switch (toplevel_state->window_state)
    {
    case WINDOW_STATE_NONE:
      break;
    case WINDOW_STATE_FLOATING:
      rect = &toplevel_state->floating.rect;
      break;
    case WINDOW_STATE_MAXIMIZED:
    case WINDOW_STATE_FULLSCREEN:
      rect = &toplevel_state->tiled.rect;
      break;
    case WINDOW_STATE_TILED_LEFT:
    case WINDOW_STATE_TILED_RIGHT:
      rect = &toplevel_state->tiled.rect;
      target_monitor = determine_monitor_for_rect (window, rect);
      if (target_monitor)
        {
          meta_window_config_set_tile_monitor_number (window->config,
                                                      target_monitor->number);
        }
      break;
    }

  if (toplevel_state->workspace_idx >= 0)
    {
      meta_window_change_workspace_by_index (window,
                                             toplevel_state->workspace_idx,
                                             TRUE);
    }


  switch (toplevel_state->window_state)
    {
    case WINDOW_STATE_NONE:
      break;
    case WINDOW_STATE_FLOATING:
      window->placed = TRUE;

      meta_window_move_resize (window,
                               (META_MOVE_RESIZE_MOVE_ACTION |
                                META_MOVE_RESIZE_RESIZE_ACTION |
                                META_MOVE_RESIZE_CONSTRAIN),
                               *rect);
      break;
    case WINDOW_STATE_TILED_LEFT:
      meta_window_move_resize (window,
                               (META_MOVE_RESIZE_FORCE_MOVE |
                                META_MOVE_RESIZE_MOVE_ACTION |
                                META_MOVE_RESIZE_RESIZE_ACTION |
                                META_MOVE_RESIZE_CONSTRAIN),
                               *rect);
      meta_window_tile (window, META_TILE_LEFT);
      break;
    case WINDOW_STATE_TILED_RIGHT:
      meta_window_move_resize (window,
                               (META_MOVE_RESIZE_FORCE_MOVE |
                                META_MOVE_RESIZE_MOVE_ACTION |
                                META_MOVE_RESIZE_RESIZE_ACTION |
                                META_MOVE_RESIZE_CONSTRAIN),
                               *rect);
      meta_window_tile (window, META_TILE_RIGHT);
      break;
    case WINDOW_STATE_MAXIMIZED:
      meta_window_move_resize (window,
                               (META_MOVE_RESIZE_FORCE_MOVE |
                                META_MOVE_RESIZE_MOVE_ACTION |
                                META_MOVE_RESIZE_RESIZE_ACTION |
                                META_MOVE_RESIZE_CONSTRAIN),
                               *rect);
      meta_window_maximize (window);
      break;
    case WINDOW_STATE_FULLSCREEN:
      meta_window_move_resize (window,
                               (META_MOVE_RESIZE_FORCE_MOVE |
                                META_MOVE_RESIZE_MOVE_ACTION |
                                META_MOVE_RESIZE_RESIZE_ACTION |
                                META_MOVE_RESIZE_CONSTRAIN),
                               *rect);
      meta_window_make_fullscreen (window);
      break;
    }

  if (toplevel_state->is_minimized)
    meta_window_minimize (window);


  if (meta_is_topic_enabled (META_DEBUG_SESSION_MANAGEMENT))
    {
      g_autofree char *state_str = NULL;

      state_str =
        meta_wayland_xdg_toplevel_state_to_string (toplevel_state);

      meta_topic (META_DEBUG_SESSION_MANAGEMENT,
                  "Restored window state %s: %s", name, state_str);
    }

  return TRUE;
}

static void
meta_wayland_xdg_session_state_remove_window (MetaSessionState *state,
                                              const char       *name)
{
  MetaWaylandXdgSessionState *xdg_session_state =
    META_WAYLAND_XDG_SESSION_STATE (state);

  g_hash_table_remove (xdg_session_state->toplevels, name);
}

static void
meta_wayland_xdg_session_state_class_init (MetaWaylandXdgSessionStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaSessionStateClass *session_state_class = META_SESSION_STATE_CLASS (klass);

  object_class->dispose = meta_wayland_xdg_session_state_dispose;

  session_state_class->serialize = meta_wayland_xdg_session_state_serialize;
  session_state_class->parse = meta_wayland_xdg_session_state_parse;
  session_state_class->save_window = meta_wayland_xdg_session_state_save_window;
  session_state_class->restore_window =
    meta_wayland_xdg_session_state_restore_window;
  session_state_class->remove_window =
    meta_wayland_xdg_session_state_remove_window;
}

static void
meta_wayland_xdg_session_state_init (MetaWaylandXdgSessionState *session_state)
{
  session_state->toplevels =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free,
                           (GDestroyNotify) meta_wayland_xdg_toplevel_state_free);
}
