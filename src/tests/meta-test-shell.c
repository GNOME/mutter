/*
 * Copyright (c) 2008 Intel Corp.
 * Copyright (c) 2023 Red Hat
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

#include "tests/meta-test-shell.h"

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

#define ACTOR_DATA_KEY "-test-shell-actor-data"
#define DISPLAY_TILE_PREVIEW_DATA_KEY "-test-shell-display-tile-preview-data"

struct _MetaTestShell
{
  MetaPlugin parent;

  ClutterTimeline *switch_workspace1_timeline;
  ClutterTimeline *switch_workspace2_timeline;
  ClutterActor *desktop1;
  ClutterActor *desktop2;

  ClutterActor *background_group;

  MetaPluginInfo info;

  struct {
    ClutterGrab *grab;
    ClutterActor *prev_focus;
  } overview;
};

typedef struct _ActorPrivate
{
  ClutterActor *orig_parent;

  ClutterTimeline *minimize_timeline;
  ClutterTimeline *destroy_timeline;
  ClutterTimeline *map_timeline;

  guint minimize_stopped_id;
} ActorPrivate;

typedef struct
{
  ClutterActor *actor;
  MetaPlugin *plugin;
  gpointer effect_data;
} EffectCompleteData;

typedef struct _DisplayTilePreview
{
  ClutterActor *actor;

  MtkRectangle tile_rect;
} DisplayTilePreview;

G_DEFINE_TYPE (MetaTestShell, meta_test_shell, META_TYPE_PLUGIN)

static GQuark actor_data_quark = 0;
static GQuark display_tile_preview_data_quark = 0;

static void
free_actor_private (gpointer data)
{
  ActorPrivate *actor_priv = data;

  g_clear_handle_id (&actor_priv->minimize_stopped_id, g_source_remove);
  g_free (data);
}

static ActorPrivate *
get_actor_private (MetaWindowActor *actor)
{
  ActorPrivate *actor_priv = g_object_get_qdata (G_OBJECT (actor), actor_data_quark);

  if (G_UNLIKELY (actor_data_quark == 0))
    actor_data_quark = g_quark_from_static_string (ACTOR_DATA_KEY);

  if (G_UNLIKELY (!actor_priv))
    {
      actor_priv = g_new0 (ActorPrivate, 1);

      g_object_set_qdata_full (G_OBJECT (actor),
                               actor_data_quark, actor_priv,
                               free_actor_private);
    }

  return actor_priv;
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
               const char           *first_property,
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
finish_timeline (ClutterTimeline *timeline)
{
  g_object_ref (timeline);
  clutter_timeline_stop (timeline);
  g_object_unref (timeline);
}

static void
kill_workspace_switch_animation (MetaTestShell *test_shell)
{
  if (test_shell->switch_workspace1_timeline)
    {
      g_autoptr (ClutterTimeline) timeline1 = NULL;
      g_autoptr (ClutterTimeline) timeline2 = NULL;

      timeline1 = g_object_ref (test_shell->switch_workspace1_timeline);
      timeline2 = g_object_ref (test_shell->switch_workspace2_timeline);

      finish_timeline (timeline1);
      finish_timeline (timeline2);
    }
}

static void
on_switch_workspace_effect_stopped (ClutterTimeline *timeline,
                                    gboolean         is_finished,
                                    gpointer         data)
{
  MetaPlugin *plugin  = META_PLUGIN (data);
  MetaTestShell *test_shell = META_TEST_SHELL (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);
  GList *l = meta_get_window_actors (display);

  while (l)
    {
      ClutterActor *a = l->data;
      MetaWindowActor *window_actor = META_WINDOW_ACTOR (a);
      ActorPrivate *actor_priv = get_actor_private (window_actor);

      if (actor_priv->orig_parent)
        {
          g_object_ref (a);
          clutter_actor_remove_child (clutter_actor_get_parent (a), a);
          clutter_actor_add_child (actor_priv->orig_parent, a);
          g_object_unref (a);
          actor_priv->orig_parent = NULL;
        }

      l = l->next;
    }

  clutter_actor_destroy (test_shell->desktop1);
  clutter_actor_destroy (test_shell->desktop2);

  test_shell->switch_workspace1_timeline = NULL;
  test_shell->switch_workspace2_timeline = NULL;
  test_shell->desktop1 = NULL;
  test_shell->desktop2 = NULL;

  meta_plugin_switch_workspace_completed (plugin);
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaPlugin         *plugin)
{
  MetaTestShell *test_shell = META_TEST_SHELL (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);
  GRand *rand;
  int i, n;

  rand = g_rand_new_with_seed (123456);
  clutter_actor_destroy_all_children (test_shell->background_group);

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

      blue = g_rand_int_range (rand, 0, 255);
      green = g_rand_int_range (rand, 0, 255);
      red = g_rand_int_range (rand, 0, 255);
      clutter_color_init (&color, red, green, blue, 255);

      background = meta_background_new (display);
      meta_background_set_color (background, &color);
      meta_background_content_set_background (background_content, background);
      g_object_unref (background);

      meta_background_content_set_vignette (background_content, TRUE, 0.5, 0.5);

      clutter_actor_add_child (test_shell->background_group, background_actor);
    }

  g_rand_free (rand);
}

static void
on_overlay_key (MetaDisplay   *display,
                MetaTestShell *test_shell)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  if (!test_shell->overview.grab)
    {
      test_shell->overview.grab = clutter_stage_grab (stage, CLUTTER_ACTOR (stage));
      test_shell->overview.prev_focus = clutter_stage_get_key_focus (stage);
      clutter_stage_set_key_focus (stage, CLUTTER_ACTOR (stage));
    }
  else
    {
      g_clear_pointer (&test_shell->overview.grab, clutter_grab_dismiss);
      clutter_stage_set_key_focus (stage,
                                   g_steal_pointer (&test_shell->overview.prev_focus));
    }
}

static void
prepare_shutdown (MetaBackend   *backend,
                  MetaTestShell *test_shell)
{
  kill_workspace_switch_animation (test_shell);
}

static void
meta_test_shell_start (MetaPlugin *plugin)
{
  MetaTestShell *test_shell = META_TEST_SHELL (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_shell->background_group = meta_background_group_new ();
  clutter_actor_insert_child_below (meta_get_window_group_for_display (display),
                                    test_shell->background_group, NULL);

  g_signal_connect (monitor_manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), plugin);
  on_monitors_changed (monitor_manager, plugin);

  g_signal_connect (display, "overlay-key",
                    G_CALLBACK (on_overlay_key), plugin);

  g_signal_connect (backend, "prepare-shutdown",
                    G_CALLBACK (prepare_shutdown),
                    test_shell);

  clutter_actor_show (meta_get_stage_for_display (display));
}

static void
meta_test_shell_switch_workspace (MetaPlugin          *plugin,
                                  int                  from,
                                  int                  to,
                                  MetaMotionDirection  direction)
{
  MetaTestShell *test_shell = META_TEST_SHELL (plugin);
  MetaDisplay *display;
  ClutterActor *stage;
  ClutterActor *workspace1, *workspace2;
  int screen_width, screen_height;
  GList *l;

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
      ActorPrivate *actor_priv = get_actor_private (window_actor);
      ClutterActor *actor = CLUTTER_ACTOR (window_actor);
      MetaWindow *window;
      MetaWorkspace *workspace;
      int workspace_idx;

      window = meta_window_actor_get_meta_window (window_actor);
      workspace = meta_window_get_workspace (window);

      if (!workspace)
        {
          clutter_actor_hide (actor);
          actor_priv->orig_parent = NULL;
          continue;
        }

      if (meta_window_is_on_all_workspaces (window))
        {
          actor_priv->orig_parent = NULL;
          continue;
        }

        workspace_idx = meta_workspace_index (workspace);

        if (workspace_idx == to || workspace_idx == from)
          {
            ClutterActor *parent = workspace_idx == to ? workspace1
                                                       : workspace2;
            actor_priv->orig_parent = clutter_actor_get_parent (actor);

            g_object_ref (actor);
            clutter_actor_remove_child (clutter_actor_get_parent (actor),
                                        actor);
            clutter_actor_add_child (parent, actor);
            clutter_actor_set_child_below_sibling (parent, actor, NULL);
            g_object_unref (actor);
            continue;
          }

        clutter_actor_hide (actor);
        actor_priv->orig_parent = NULL;
    }

  test_shell->desktop1 = workspace1;
  test_shell->desktop2 = workspace2;

  test_shell->switch_workspace1_timeline =
    actor_animate (workspace1, CLUTTER_EASE_IN_SINE,
                   ANIMATION_SWITCH,
                   "scale-x", 1.0,
                   "scale-y", 1.0,
                   NULL);
  g_signal_connect (test_shell->switch_workspace1_timeline,
                    "stopped",
                    G_CALLBACK (on_switch_workspace_effect_stopped),
                    plugin);

  test_shell->switch_workspace2_timeline =
    actor_animate (workspace2, CLUTTER_EASE_IN_SINE,
                   ANIMATION_SWITCH,
                   "scale-x", 0.0,
                   "scale-y", 0.0,
                   NULL);
}

static void
restore_scale_idle (gpointer user_data)
{
  EffectCompleteData *data = user_data;
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);
  double original_scale = *(double *) data->effect_data;
  ActorPrivate *actor_priv;

  actor_priv = get_actor_private (META_WINDOW_ACTOR (data->actor));
  actor_priv->minimize_timeline = NULL;
  actor_priv->minimize_stopped_id = 0;

  clutter_actor_hide (data->actor);

  clutter_actor_set_scale (data->actor, original_scale, original_scale);

  meta_plugin_minimize_completed (plugin, window_actor);

  g_free (data->effect_data);
  g_free (data);
}

static void
on_minimize_effect_stopped (ClutterTimeline    *timeline,
                            gboolean            is_finished,
                            EffectCompleteData *data)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate *actor_priv = get_actor_private (window_actor);

  actor_priv->minimize_stopped_id =
    g_idle_add_once (restore_scale_idle, data);
}

static void
meta_test_shell_minimize (MetaPlugin      *plugin,
                          MetaWindowActor *window_actor)
{
  MetaWindowType type;
  MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
  ClutterTimeline *timeline = NULL;
  ClutterActor *actor  = CLUTTER_ACTOR (window_actor);

  type = meta_window_get_window_type (window);

  if (type == META_WINDOW_NORMAL)
    {
      timeline = actor_animate (actor,
                                CLUTTER_EASE_IN_SINE,
                                ANIMATION_MINIMIZE,
                                "scale-x", 0.0,
                                "scale-y", 0.0,
                                "x", (double) 0,
                                "y", (double) 0,
                                NULL);
    }

  if (timeline)
    {
      EffectCompleteData *data;
      ActorPrivate *actor_priv = get_actor_private (window_actor);
      double scale_x, scale_y;

      data = g_new0 (EffectCompleteData, 1);
      actor_priv->minimize_timeline = timeline;
      data->plugin = plugin;
      data->actor = actor;
      data->effect_data = g_new0 (double, 1);
      clutter_actor_get_scale (actor, &scale_x, &scale_y);
      g_assert (scale_x == scale_y);
      *((double *) data->effect_data) = scale_x;
      g_signal_connect (actor_priv->minimize_timeline, "stopped",
                        G_CALLBACK (on_minimize_effect_stopped),
                        data);
      g_clear_handle_id (&actor_priv->minimize_stopped_id, g_source_remove);
    }
  else
    {
      meta_plugin_minimize_completed (plugin, window_actor);
    }
}

static void
on_map_effect_stopped (ClutterTimeline    *timeline,
                       gboolean            is_finished,
                       EffectCompleteData *data)
{
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor  *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate  *actor_priv = get_actor_private (window_actor);

  actor_priv->map_timeline = NULL;

  meta_plugin_map_completed (plugin, window_actor);

  g_free (data);
}

static void
meta_test_shell_map (MetaPlugin      *plugin,
                     MetaWindowActor *window_actor)
{
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
  MetaWindowType type;

  type = meta_window_get_window_type (window);

  if (type == META_WINDOW_NORMAL)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *actor_priv = get_actor_private (window_actor);

      clutter_actor_set_pivot_point (actor, 0.5, 0.5);
      clutter_actor_set_opacity (actor, 0);
      clutter_actor_set_scale (actor, 0.5, 0.5);
      clutter_actor_show (actor);

      actor_priv->map_timeline = actor_animate (actor,
                                                CLUTTER_EASE_OUT_QUAD,
                                                ANIMATION_MAP,
                                                "opacity", 255,
                                                "scale-x", 1.0,
                                                "scale-y", 1.0,
                                                NULL);
      if (actor_priv->map_timeline)
        {
          data->actor = actor;
          data->plugin = plugin;
          g_signal_connect (actor_priv->map_timeline, "stopped",
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
    {
      meta_plugin_map_completed (plugin, window_actor);
    }
}

static void
on_destroy_effect_stopped (ClutterTimeline    *timeline,
                           gboolean            is_finished,
                           EffectCompleteData *data)
{
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate *actor_priv = get_actor_private (window_actor);

  actor_priv->destroy_timeline = NULL;

  meta_plugin_destroy_completed (plugin, window_actor);
}

static void
meta_test_shell_destroy (MetaPlugin      *plugin,
                         MetaWindowActor *window_actor)
{
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
  MetaWindowType type;
  ClutterTimeline *timeline = NULL;

  type = meta_window_get_window_type (window);

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
      ActorPrivate *actor_priv = get_actor_private (window_actor);

      actor_priv->destroy_timeline = timeline;
      data->plugin = plugin;
      data->actor = actor;
      g_signal_connect (actor_priv->destroy_timeline, "stopped",
                        G_CALLBACK (on_destroy_effect_stopped),
                        data);
    }
  else
    {
      meta_plugin_destroy_completed (plugin, window_actor);
    }
}

static void
free_display_tile_preview (DisplayTilePreview *preview)
{

  if (preview)
    {
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
      clutter_actor_set_background_color (preview->actor, &CLUTTER_COLOR_INIT (0, 0, 255, 255));
      clutter_actor_set_opacity (preview->actor, 100);

      clutter_actor_add_child (meta_get_window_group_for_display (display),
                               preview->actor);
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
meta_test_shell_show_tile_preview (MetaPlugin    *plugin,
                                   MetaWindow    *window,
                                   MtkRectangle  *tile_rect,
                                   int            tile_monitor_number)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  DisplayTilePreview *preview = get_display_tile_preview (display);
  ClutterActor *window_actor;

  if (clutter_actor_is_visible (preview->actor) &&
      preview->tile_rect.x == tile_rect->x &&
      preview->tile_rect.y == tile_rect->y &&
      preview->tile_rect.width == tile_rect->width &&
      preview->tile_rect.height == tile_rect->height)
    return;

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
meta_test_shell_hide_tile_preview (MetaPlugin *plugin)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  DisplayTilePreview *preview = get_display_tile_preview (display);

  clutter_actor_hide (preview->actor);
}

static void
meta_test_shell_kill_switch_workspace (MetaPlugin *plugin)
{
  MetaTestShell *test_shell = META_TEST_SHELL (plugin);

  kill_workspace_switch_animation (test_shell);
}

static void
meta_test_shell_kill_window_effects (MetaPlugin      *plugin,
                                     MetaWindowActor *window_actor)
{
  ActorPrivate *actor_priv;

  actor_priv = get_actor_private (window_actor);

  if (actor_priv->minimize_timeline)
    finish_timeline (actor_priv->minimize_timeline);

  if (actor_priv->map_timeline)
    finish_timeline (actor_priv->map_timeline);

  if (actor_priv->destroy_timeline)
    finish_timeline (actor_priv->destroy_timeline);
}

static const MetaPluginInfo *
meta_test_shell_plugin_info (MetaPlugin *plugin)
{
  MetaTestShell *test_shell = META_TEST_SHELL (plugin);

  return &test_shell->info;
}

static void
meta_test_shell_class_init (MetaTestShellClass *klass)
{
  MetaPluginClass *plugin_class  = META_PLUGIN_CLASS (klass);

  plugin_class->start = meta_test_shell_start;
  plugin_class->map = meta_test_shell_map;
  plugin_class->minimize = meta_test_shell_minimize;
  plugin_class->destroy = meta_test_shell_destroy;
  plugin_class->switch_workspace = meta_test_shell_switch_workspace;
  plugin_class->show_tile_preview = meta_test_shell_show_tile_preview;
  plugin_class->hide_tile_preview = meta_test_shell_hide_tile_preview;
  plugin_class->kill_window_effects = meta_test_shell_kill_window_effects;
  plugin_class->kill_switch_workspace = meta_test_shell_kill_switch_workspace;
  plugin_class->plugin_info = meta_test_shell_plugin_info;
}

static void
meta_test_shell_init (MetaTestShell *test_shell)
{
  test_shell->info.name = "Test Shell";
  test_shell->info.version = VERSION;
  test_shell->info.author = "Mutter developers";
  test_shell->info.license = "GPL";
  test_shell->info.description = "This is test shell plugin implementation.";
}
