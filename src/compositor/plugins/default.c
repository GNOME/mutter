/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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

#include "meta/display.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <string.h>

#include "clutter/clutter.h"
#include "meta/meta-backend.h"
#include "meta/meta-background-actor.h"
#include "meta/meta-background-content.h"
#include "meta/meta-background-group.h"
#include "meta/meta-context.h"
#include "meta/meta-monitor-manager.h"
#include "meta/meta-plugin.h"
#include "meta/util.h"
#include "meta/window.h"

typedef enum
{
  ANIMATION_DESTROY,
  ANIMATION_MINIMIZE,
  ANIMATION_MAP,
  ANIMATION_SWITCH,
} Animation;

static unsigned int animation_durations[] = {
  100, /* destroy */
  250, /* minimize */
  250, /* map */
  500, /* switch */
};

#define ACTOR_DATA_KEY "MCCP-Default-actor-data"
#define DISPLAY_TILE_PREVIEW_DATA_KEY "MCCP-Default-display-tile-preview-data"

#define META_TYPE_DEFAULT_PLUGIN            (meta_default_plugin_get_type ())
#define META_DEFAULT_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEFAULT_PLUGIN, MetaDefaultPlugin))
#define META_DEFAULT_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))
#define META_IS_DEFAULT_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_DEFAULT_PLUGIN_TYPE))
#define META_IS_DEFAULT_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEFAULT_PLUGIN))
#define META_DEFAULT_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))

typedef struct _MetaDefaultPlugin        MetaDefaultPlugin;
typedef struct _MetaDefaultPluginClass   MetaDefaultPluginClass;
typedef struct _MetaDefaultPluginPrivate MetaDefaultPluginPrivate;

struct _MetaDefaultPlugin
{
  MetaPlugin parent;

  MetaDefaultPluginPrivate *priv;
};

struct _MetaDefaultPluginClass
{
  MetaPluginClass parent_class;
};

static GQuark actor_data_quark = 0;
static GQuark display_tile_preview_data_quark = 0;

static void start      (MetaPlugin      *plugin);
static void minimize   (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void map        (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void destroy    (MetaPlugin      *plugin,
                        MetaWindowActor *actor);

static void switch_workspace (MetaPlugin          *plugin,
                              gint                 from,
                              gint                 to,
                              MetaMotionDirection  direction);

static void kill_window_effects   (MetaPlugin      *plugin,
                                   MetaWindowActor *actor);
static void kill_switch_workspace (MetaPlugin      *plugin);

static void show_tile_preview (MetaPlugin   *plugin,
                               MetaWindow   *window,
                               MtkRectangle *tile_rect,
                               int           tile_monitor_number);
static void hide_tile_preview (MetaPlugin      *plugin);

static const MetaPluginInfo * plugin_info (MetaPlugin *plugin);

/*
 * Plugin private data that we store in the .plugin_private member.
 */
struct _MetaDefaultPluginPrivate
{
  /* Valid only when switch_workspace effect is in progress */
  ClutterTimeline       *tml_switch_workspace1;
  ClutterTimeline       *tml_switch_workspace2;
  ClutterActor          *desktop1;
  ClutterActor          *desktop2;

  ClutterActor          *background_group;

  MetaPluginInfo         info;
};

META_PLUGIN_DECLARE_WITH_CODE (MetaDefaultPlugin, meta_default_plugin,
                               G_ADD_PRIVATE_DYNAMIC (MetaDefaultPlugin));

/*
 * Per actor private data we attach to each actor.
 */
typedef struct _ActorPrivate
{
  ClutterActor *orig_parent;

  ClutterTimeline *tml_minimize;
  ClutterTimeline *tml_destroy;
  ClutterTimeline *tml_map;
} ActorPrivate;

/* callback data for when animations complete */
typedef struct
{
  ClutterActor *actor;
  MetaPlugin *plugin;
} EffectCompleteData;


typedef struct _DisplayTilePreview
{
  ClutterActor   *actor;

  MtkRectangle   tile_rect;
} DisplayTilePreview;

static void
meta_default_plugin_class_init (MetaDefaultPluginClass *klass)
{
  MetaPluginClass *plugin_class  = META_PLUGIN_CLASS (klass);

  plugin_class->start            = start;
  plugin_class->map              = map;
  plugin_class->minimize         = minimize;
  plugin_class->destroy          = destroy;
  plugin_class->switch_workspace = switch_workspace;
  plugin_class->show_tile_preview = show_tile_preview;
  plugin_class->hide_tile_preview = hide_tile_preview;
  plugin_class->plugin_info      = plugin_info;
  plugin_class->kill_window_effects   = kill_window_effects;
  plugin_class->kill_switch_workspace = kill_switch_workspace;
}

static void
meta_default_plugin_init (MetaDefaultPlugin *self)
{
  MetaDefaultPluginPrivate *priv;

  self->priv = priv = meta_default_plugin_get_instance_private (self);

  priv->info.name        = "Default Effects";
  priv->info.version     = "0.1";
  priv->info.author      = "Intel Corp.";
  priv->info.license     = "GPL";
  priv->info.description = "This is an example of a plugin implementation.";
}

/*
 * Actor private data accessor
 */
static void
free_actor_private (gpointer data)
{
  if (G_LIKELY (data != NULL))
    g_free (data);
}

static ActorPrivate *
get_actor_private (MetaWindowActor *actor)
{
  ActorPrivate *priv = g_object_get_qdata (G_OBJECT (actor), actor_data_quark);

  if (G_UNLIKELY (actor_data_quark == 0))
    actor_data_quark = g_quark_from_static_string (ACTOR_DATA_KEY);

  if (G_UNLIKELY (!priv))
    {
      priv = g_new0 (ActorPrivate, 1);

      g_object_set_qdata_full (G_OBJECT (actor),
                               actor_data_quark, priv,
                               free_actor_private);
    }

  return priv;
}

static gboolean
is_animations_disabled (void)
{
  static gboolean is_animations_disabled_set;
  static gboolean is_animations_disabled;

  if (!is_animations_disabled_set)
    {
      if (g_strcmp0 (getenv ("MUTTER_DEBUG_DISABLE_ANIMATIONS"), "1") == 0)
        is_animations_disabled = TRUE;
      else
        is_animations_disabled = FALSE;

      is_animations_disabled_set = TRUE;
    }

  return is_animations_disabled;
}

static unsigned int
get_animation_duration (Animation animation)
{
  if (is_animations_disabled ())
    return 0;

  return animation_durations[animation];
}

static ClutterTimeline *
actor_animate (ClutterActor         *actor,
               ClutterAnimationMode  mode,
               Animation             animation,
               const gchar          *first_property,
               ...)
{
  va_list args;
  ClutterTransition *transition;

  clutter_actor_save_easing_state (actor);
  clutter_actor_set_easing_mode (actor, mode);
  clutter_actor_set_easing_duration (actor, get_animation_duration (animation));

  va_start (args, first_property);
  g_object_set_valist (G_OBJECT (actor), first_property, args);
  va_end (args);

  transition = clutter_actor_get_transition (actor, first_property);

  clutter_actor_restore_easing_state (actor);

  return CLUTTER_TIMELINE (transition);
}

static void
on_switch_workspace_effect_stopped (ClutterTimeline *timeline,
                                    gboolean         is_finished,
                                    gpointer         data)
{
  MetaPlugin               *plugin  = META_PLUGIN (data);
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;
  MetaDisplay *display = meta_plugin_get_display (plugin);
  GList *l = meta_get_window_actors (display);

  while (l)
    {
      ClutterActor *a = l->data;
      MetaWindowActor *window_actor = META_WINDOW_ACTOR (a);
      ActorPrivate *apriv = get_actor_private (window_actor);

      if (apriv->orig_parent)
        {
          g_object_ref (a);
          clutter_actor_remove_child (clutter_actor_get_parent (a), a);
          clutter_actor_add_child (apriv->orig_parent, a);
          g_object_unref (a);
          apriv->orig_parent = NULL;
        }

      l = l->next;
    }

  clutter_actor_destroy (priv->desktop1);
  clutter_actor_destroy (priv->desktop2);

  priv->tml_switch_workspace1 = NULL;
  priv->tml_switch_workspace2 = NULL;
  priv->desktop1 = NULL;
  priv->desktop2 = NULL;

  meta_plugin_switch_workspace_completed (plugin);
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaPlugin         *plugin)
{
  MetaDefaultPlugin *self = META_DEFAULT_PLUGIN (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);

  int i, n;
  GRand *rand = g_rand_new_with_seed (123456);

  clutter_actor_destroy_all_children (self->priv->background_group);

  n = meta_display_get_n_monitors (display);
  for (i = 0; i < n; i++)
    {
      MetaBackgroundContent *background_content;
      ClutterContent *content;
      MtkRectangle rect;
      ClutterActor *background_actor;
      MetaBackground *background;
      uint8_t red;
      uint8_t green;
      uint8_t blue;
      ClutterColor color;

      meta_display_get_monitor_geometry (display, i, &rect);

      background_actor = meta_background_actor_new (display, i);
      content = clutter_actor_get_content (background_actor);
      background_content = META_BACKGROUND_CONTENT (content);

      clutter_actor_set_position (background_actor, rect.x, rect.y);
      clutter_actor_set_size (background_actor, rect.width, rect.height);

      /* Don't use rand() here, mesa calls srand() internally when
         parsing the driconf XML, but it's nice if the colors are
         reproducible.
      */

      blue = g_rand_int_range (rand, 0, 255);
      green = g_rand_int_range (rand, 0, 255);
      red = g_rand_int_range (rand, 0, 255);
      clutter_color_init (&color, red, green, blue, 255);

      background = meta_background_new (display);
      meta_background_set_color (background, &color);
      meta_background_content_set_background (background_content, background);
      g_object_unref (background);

      meta_background_content_set_vignette (background_content, TRUE, 0.5, 0.5);

      clutter_actor_add_child (self->priv->background_group, background_actor);
    }

  g_rand_free (rand);
}

static void
init_keymap (MetaDefaultPlugin *self,
             MetaBackend       *backend)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GVariant) props = NULL;
  g_autofree char *x11_layout = NULL;
  g_autofree char *x11_options = NULL;
  g_autofree char *x11_variant = NULL;
  g_autofree char *x11_model = NULL;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         (G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                          G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS),
                                         NULL,
                                         "org.freedesktop.locale1",
                                         "/org/freedesktop/locale1",
                                         "org.freedesktop.DBus.Properties",
                                         NULL,
                                         &error);
  if (!proxy)
    {
      g_warning ("Failed to acquire org.freedesktop.locale1 proxy: %s",
                 error->message);
      return;
    }

  result = g_dbus_proxy_call_sync (proxy,
                                   "GetAll",
                                   g_variant_new ("(s)",
                                                  "org.freedesktop.locale1"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   100,
                                   NULL,
                                   &error);
  if (!result)
    {
      g_warning ("Failed to retrieve locale properties: %s", error->message);
      return;
    }

  props = g_variant_get_child_value (result, 0);
  if (!props)
    {
      g_warning ("No locale properties found");
      return;
    }

  if (!g_variant_lookup (props, "X11Layout", "s", &x11_layout))
    x11_layout = g_strdup ("us");

  if (!g_variant_lookup (props, "X11Options", "s", &x11_options))
    x11_options = g_strdup ("");

  if (!g_variant_lookup (props, "X11Variant", "s", &x11_variant))
    x11_variant = g_strdup ("");

  if (!g_variant_lookup (props, "X11Model", "s", &x11_model))
    x11_model = g_strdup ("");

  meta_backend_set_keymap (backend,
                           x11_layout,
                           x11_variant,
                           x11_options,
                           x11_model);
}

static void
prepare_shutdown (MetaBackend       *backend,
                  MetaDefaultPlugin *plugin)
{
  kill_switch_workspace (META_PLUGIN (plugin));
}

static void
start (MetaPlugin *plugin)
{
  MetaDefaultPlugin *self = META_DEFAULT_PLUGIN (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  self->priv->background_group = meta_background_group_new ();
  clutter_actor_insert_child_below (meta_get_window_group_for_display (display),
                                    self->priv->background_group, NULL);

  g_signal_connect (monitor_manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), plugin);

  on_monitors_changed (monitor_manager, plugin);

  g_signal_connect (backend, "prepare-shutdown",
                    G_CALLBACK (prepare_shutdown),
                    self);

  if (meta_is_wayland_compositor ())
    init_keymap (self, backend);

  clutter_actor_show (meta_get_stage_for_display (display));
}

static void
switch_workspace (MetaPlugin *plugin,
                  gint from, gint to,
                  MetaMotionDirection direction)
{
  MetaDisplay *display;
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;
  GList        *l;
  ClutterActor *stage;
  ClutterActor *workspace1, *workspace2;
  int           screen_width, screen_height;

  if (from == to)
    {
      meta_plugin_switch_workspace_completed (plugin);
      return;
    }

  display = meta_plugin_get_display (plugin);
  stage = meta_get_stage_for_display (display);

  meta_display_get_size (display,
                         &screen_width,
                         &screen_height);

  workspace1 = clutter_actor_new ();
  workspace2 = clutter_actor_new ();

  clutter_actor_set_pivot_point (workspace1, 1.0, 1.0);
  clutter_actor_set_size (workspace1,
                          screen_width,
                          screen_height);
  clutter_actor_set_size (workspace2,
                          screen_width,
                          screen_height);

  clutter_actor_set_scale (workspace1, 0.0, 0.0);

  clutter_actor_add_child (stage, workspace1);
  clutter_actor_add_child (stage, workspace2);

  for (l = g_list_last (meta_get_window_actors (display)); l; l = l->prev)
    {
      MetaWindowActor *window_actor = l->data;
      ActorPrivate    *apriv	    = get_actor_private (window_actor);
      ClutterActor    *actor	    = CLUTTER_ACTOR (window_actor);
      MetaWindow      *window;
      MetaWorkspace   *workspace;
      gint             workspace_idx;

      window = meta_window_actor_get_meta_window (window_actor);
      workspace = meta_window_get_workspace (window);

      if (!workspace)
        {
          /* unmanaging window */
          clutter_actor_hide (actor);
          apriv->orig_parent = NULL;
          continue;
        }

      if (meta_window_is_on_all_workspaces (window))
        {
          /* Sticky window */
          apriv->orig_parent = NULL;
          continue;
        }

        workspace_idx = meta_workspace_index (workspace);

        if (workspace_idx == to || workspace_idx == from)
          {
            ClutterActor *parent = workspace_idx == to ? workspace1
                                                       : workspace2;
            apriv->orig_parent = clutter_actor_get_parent (actor);

            g_object_ref (actor);
            clutter_actor_remove_child (clutter_actor_get_parent (actor),
                                        actor);
            clutter_actor_add_child (parent, actor);
            clutter_actor_set_child_below_sibling (parent, actor, NULL);
            g_object_unref (actor);
            continue;
          }

        /* Window on some other desktop */
        clutter_actor_hide (actor);
        apriv->orig_parent = NULL;
    }

  priv->desktop1 = workspace1;
  priv->desktop2 = workspace2;

  priv->tml_switch_workspace1 = actor_animate (workspace1, CLUTTER_EASE_IN_SINE,
                                               ANIMATION_SWITCH,
                                               "scale-x", 1.0,
                                               "scale-y", 1.0,
                                               NULL);
  g_signal_connect (priv->tml_switch_workspace1,
                    "stopped",
                    G_CALLBACK (on_switch_workspace_effect_stopped),
                    plugin);

  priv->tml_switch_workspace2 = actor_animate (workspace2, CLUTTER_EASE_IN_SINE,
                                               ANIMATION_SWITCH,
                                               "scale-x", 0.0,
                                               "scale-y", 0.0,
                                               NULL);
}


/*
 * Minimize effect completion callback; this function restores actor state, and
 * calls the manager callback function.
 */
static void
on_minimize_effect_stopped (ClutterTimeline    *timeline,
                            gboolean            is_finished,
                            EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect; must hide it first to ensure
   * that the restoration will not be visible.
   */
  MetaPlugin *plugin = data->plugin;
  ActorPrivate *apriv;
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);

  apriv = get_actor_private (META_WINDOW_ACTOR (data->actor));
  apriv->tml_minimize = NULL;

  clutter_actor_hide (data->actor);

  /* FIXME - we shouldn't assume the original scale, it should be saved
   * at the start of the effect */
  clutter_actor_set_scale (data->actor, 1.0, 1.0);

  /* Now notify the manager that we are done with this effect */
  meta_plugin_minimize_completed (plugin, window_actor);

  g_free (data);
}

/*
 * Simple minimize handler: it applies a scale effect (which must be reversed on
 * completion).
 */
static void
minimize (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  MtkRectangle icon_geometry;
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);
  ClutterTimeline *timeline = NULL;
  ClutterActor *actor  = CLUTTER_ACTOR (window_actor);


  type = meta_window_get_window_type (meta_window);

  if (!meta_window_get_icon_geometry(meta_window, &icon_geometry))
    {
      icon_geometry.x = 0;
      icon_geometry.y = 0;
    }

  if (type == META_WINDOW_NORMAL)
    {
      timeline = actor_animate (actor,
                                CLUTTER_EASE_IN_SINE,
                                ANIMATION_MINIMIZE,
                                "scale-x", 0.0,
                                "scale-y", 0.0,
                                "x", (double)icon_geometry.x,
                                "y", (double)icon_geometry.y,
                                NULL);
    }

  if (timeline)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      apriv->tml_minimize = timeline;
      data->plugin = plugin;
      data->actor = actor;
      g_signal_connect (apriv->tml_minimize, "stopped",
                        G_CALLBACK (on_minimize_effect_stopped),
                        data);
    }
  else
    meta_plugin_minimize_completed (plugin, window_actor);
}

static void
on_map_effect_stopped (ClutterTimeline    *timeline,
                       gboolean            is_finished,
                       EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect.
   */
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor  *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate  *apriv = get_actor_private (window_actor);

  apriv->tml_map = NULL;

  /* Now notify the manager that we are done with this effect */
  meta_plugin_map_completed (plugin, window_actor);

  g_free (data);
}

/*
 * Simple map handler: it applies a scale effect which must be reversed on
 * completion).
 */
static void
map (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      clutter_actor_set_pivot_point (actor, 0.5, 0.5);
      clutter_actor_set_opacity (actor, 0);
      clutter_actor_set_scale (actor, 0.5, 0.5);
      clutter_actor_show (actor);

      apriv->tml_map = actor_animate (actor,
                                      CLUTTER_EASE_OUT_QUAD,
                                      ANIMATION_MAP,
                                      "opacity", 255,
                                      "scale-x", 1.0,
                                      "scale-y", 1.0,
                                      NULL);
      if (apriv->tml_map)
        {
          data->actor = actor;
          data->plugin = plugin;
          g_signal_connect (apriv->tml_map, "stopped",
                            G_CALLBACK (on_map_effect_stopped),
                            data);
        }
      else
        {
          g_free (data);
          meta_plugin_map_completed (plugin, window_actor);
        }
    }
  else
    meta_plugin_map_completed (plugin, window_actor);
}

/*
 * Destroy effect completion callback; this is a simple effect that requires no
 * further action than notifying the manager that the effect is completed.
 */
static void
on_destroy_effect_stopped (ClutterTimeline    *timeline,
                           gboolean            is_finished,
                           EffectCompleteData *data)
{
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate *apriv = get_actor_private (window_actor);

  apriv->tml_destroy = NULL;

  meta_plugin_destroy_completed (plugin, window_actor);
}

/*
 * Simple TV-out like effect.
 */
static void
destroy (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);
  ClutterTimeline *timeline = NULL;

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      timeline = actor_animate (actor,
                                CLUTTER_EASE_OUT_QUAD,
                                ANIMATION_DESTROY,
                                "opacity", 0,
                                "scale-x", 0.8,
                                "scale-y", 0.8,
                                NULL);
    }

  if (timeline)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      apriv->tml_destroy = timeline;
      data->plugin = plugin;
      data->actor = actor;
      g_signal_connect (apriv->tml_destroy, "stopped",
                        G_CALLBACK (on_destroy_effect_stopped),
                        data);
    }
  else
    meta_plugin_destroy_completed (plugin, window_actor);
}

/*
 * Tile preview private data accessor
 */
static void
free_display_tile_preview (DisplayTilePreview *preview)
{

  if (G_LIKELY (preview != NULL)) {
    clutter_actor_destroy (preview->actor);
    g_free (preview);
  }
}

static void
on_display_closing (MetaDisplay        *display,
                    DisplayTilePreview *preview)
{
  free_display_tile_preview (preview);
}

static DisplayTilePreview *
get_display_tile_preview (MetaDisplay *display)
{
  DisplayTilePreview *preview;

  if (!display_tile_preview_data_quark)
    {
      display_tile_preview_data_quark =
        g_quark_from_static_string (DISPLAY_TILE_PREVIEW_DATA_KEY);
    }

  preview = g_object_get_qdata (G_OBJECT (display),
                                display_tile_preview_data_quark);
  if (!preview)
    {
      preview = g_new0 (DisplayTilePreview, 1);

      preview->actor = clutter_actor_new ();
      clutter_actor_set_background_color (preview->actor, CLUTTER_COLOR_Blue);
      clutter_actor_set_opacity (preview->actor, 100);

      clutter_actor_add_child (meta_get_window_group_for_display (display), preview->actor);
      g_signal_connect (display,
                        "closing",
                        G_CALLBACK (on_display_closing),
                        preview);
      g_object_set_qdata (G_OBJECT (display),
                          display_tile_preview_data_quark,
                          preview);
    }

  return preview;
}

static void
show_tile_preview (MetaPlugin   *plugin,
                   MetaWindow   *window,
                   MtkRectangle *tile_rect,
                   int           tile_monitor_number)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  DisplayTilePreview *preview = get_display_tile_preview (display);
  ClutterActor *window_actor;

  if (clutter_actor_is_visible (preview->actor)
      && preview->tile_rect.x == tile_rect->x
      && preview->tile_rect.y == tile_rect->y
      && preview->tile_rect.width == tile_rect->width
      && preview->tile_rect.height == tile_rect->height)
    return; /* nothing to do */

  clutter_actor_set_position (preview->actor, tile_rect->x, tile_rect->y);
  clutter_actor_set_size (preview->actor, tile_rect->width, tile_rect->height);

  clutter_actor_show (preview->actor);

  window_actor = CLUTTER_ACTOR (meta_window_get_compositor_private (window));
  clutter_actor_set_child_below_sibling (clutter_actor_get_parent (preview->actor),
                                         preview->actor,
                                         window_actor);

  preview->tile_rect = *tile_rect;
}

static void
hide_tile_preview (MetaPlugin *plugin)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  DisplayTilePreview *preview = get_display_tile_preview (display);

  clutter_actor_hide (preview->actor);
}

static void
finish_timeline (ClutterTimeline *timeline)
{
  g_object_ref (timeline);
  clutter_timeline_stop (timeline);
  g_object_unref (timeline);
}

static void
kill_switch_workspace (MetaPlugin *plugin)
{
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;

  if (priv->tml_switch_workspace1)
    {
      g_autoptr (ClutterTimeline) timeline1 = NULL;
      g_autoptr (ClutterTimeline) timeline2 = NULL;

      timeline1 = g_object_ref (priv->tml_switch_workspace1);
      timeline2 = g_object_ref (priv->tml_switch_workspace2);

      finish_timeline (timeline1);
      finish_timeline (timeline2);
    }
}

static void
kill_window_effects (MetaPlugin      *plugin,
                     MetaWindowActor *window_actor)
{
  ActorPrivate *apriv;

  apriv = get_actor_private (window_actor);

  if (apriv->tml_minimize)
    finish_timeline (apriv->tml_minimize);

  if (apriv->tml_map)
    finish_timeline (apriv->tml_map);

  if (apriv->tml_destroy)
    finish_timeline (apriv->tml_destroy);
}

static const MetaPluginInfo *
plugin_info (MetaPlugin *plugin)
{
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;

  return &priv->info;
}
