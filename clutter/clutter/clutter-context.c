/*
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "clutter/clutter-context-private.h"

#include "clutter/clutter-accessibility-private.h"
#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-color-manager.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-settings-private.h"

static gboolean clutter_show_fps = FALSE;
static gboolean clutter_enable_accessibility = TRUE;

#ifdef CLUTTER_ENABLE_DEBUG
static const GDebugKey clutter_debug_keys[] = {
  { "misc", CLUTTER_DEBUG_MISC },
  { "actor", CLUTTER_DEBUG_ACTOR },
  { "texture", CLUTTER_DEBUG_TEXTURE },
  { "event", CLUTTER_DEBUG_EVENT },
  { "paint", CLUTTER_DEBUG_PAINT },
  { "pick", CLUTTER_DEBUG_PICK },
  { "backend", CLUTTER_DEBUG_BACKEND },
  { "scheduler", CLUTTER_DEBUG_SCHEDULER },
  { "script", CLUTTER_DEBUG_SCRIPT },
  { "shader", CLUTTER_DEBUG_SHADER },
  { "animation", CLUTTER_DEBUG_ANIMATION },
  { "layout", CLUTTER_DEBUG_LAYOUT },
  { "clipping", CLUTTER_DEBUG_CLIPPING },
  { "oob-transforms", CLUTTER_DEBUG_OOB_TRANSFORMS },
  { "frame-timings", CLUTTER_DEBUG_FRAME_TIMINGS },
  { "detailed-trace", CLUTTER_DEBUG_DETAILED_TRACE },
  { "grabs", CLUTTER_DEBUG_GRABS },
  { "frame-clock", CLUTTER_DEBUG_FRAME_CLOCK },
  { "gestures", CLUTTER_DEBUG_GESTURES },
};
#endif /* CLUTTER_ENABLE_DEBUG */

static const GDebugKey clutter_pick_debug_keys[] = {
  { "nop-picking", CLUTTER_DEBUG_NOP_PICKING },
};

static const GDebugKey clutter_paint_debug_keys[] = {
  { "disable-swap-events", CLUTTER_DEBUG_DISABLE_SWAP_EVENTS },
  { "disable-clipped-redraws", CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS },
  { "redraws", CLUTTER_DEBUG_REDRAWS },
  { "paint-volumes", CLUTTER_DEBUG_PAINT_VOLUMES },
  { "disable-culling", CLUTTER_DEBUG_DISABLE_CULLING },
  { "disable-offscreen-redirect", CLUTTER_DEBUG_DISABLE_OFFSCREEN_REDIRECT },
  { "continuous-redraw", CLUTTER_DEBUG_CONTINUOUS_REDRAW },
  { "paint-deform-tiles", CLUTTER_DEBUG_PAINT_DEFORM_TILES },
  { "damage-region", CLUTTER_DEBUG_PAINT_DAMAGE_REGION },
  { "disable-dynamic-max-render-time", CLUTTER_DEBUG_DISABLE_DYNAMIC_MAX_RENDER_TIME },
};

typedef struct _ClutterContextPrivate
{
  ClutterTextDirection text_direction;

  ClutterColorManager *color_manager;
  ClutterPipelineCache *pipeline_cache;
} ClutterContextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterContext, clutter_context, G_TYPE_OBJECT)

static void
clutter_context_dispose (GObject *object)
{
  ClutterContext *context = CLUTTER_CONTEXT (object);
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  g_clear_object (&priv->pipeline_cache);
  g_clear_object (&priv->color_manager);
  g_clear_pointer (&context->events_queue, g_async_queue_unref);
  g_clear_pointer (&context->backend, clutter_backend_destroy);
  g_clear_object (&context->stage_manager);
  g_clear_object (&context->settings);

  G_OBJECT_CLASS (clutter_context_parent_class)->dispose (object);
}

static void
clutter_context_class_init (ClutterContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_context_dispose;

  clutter_interval_register_progress_funcs ();
}

static void
clutter_context_init (ClutterContext *context)
{
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  priv->text_direction = CLUTTER_TEXT_DIRECTION_LTR;
}

ClutterTextDirection
clutter_get_text_direction (void)
{
  ClutterTextDirection dir = CLUTTER_TEXT_DIRECTION_LTR;
  const char *direction, *locale;
  g_autofree char **locale_parts = NULL;

  direction = g_getenv ("CLUTTER_TEXT_DIRECTION");
  locale = g_getenv ("LC_CTYPE");
  if (!locale || *locale != '\0' )
    locale = g_getenv ("LANG");

  if (direction && *direction != '\0'  && strcmp (direction, "rtl") == 0)
    {
      dir = CLUTTER_TEXT_DIRECTION_RTL;
    }
  else if (locale && *locale != '\0')
    {
      // Extract the language code from the locale (e.g., "en_US.UTF-8" -> "en")
      locale_parts = g_strsplit (locale, "_", 2);

      // Determine the direction based on the language code
      if (g_ascii_strcasecmp (locale_parts[0], "ar") == 0 ||  // Arabic
          g_ascii_strcasecmp (locale_parts[0], "he") == 0 ||  // Hebrew
          g_ascii_strcasecmp (locale_parts[0], "fa") == 0 ||  // Persian
          g_ascii_strcasecmp (locale_parts[0], "ur") == 0) {  // Urdu
          dir = CLUTTER_TEXT_DIRECTION_RTL;
      }
    }

  CLUTTER_NOTE (MISC, "Text direction: %s",
                dir == CLUTTER_TEXT_DIRECTION_RTL ? "rtl" : "ltr");

  return dir;
}

static gboolean
clutter_context_init_real (ClutterContext       *context,
                           GError              **error)
{
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  /* If we are displaying the regions that would get redrawn with clipped
   * redraws enabled we actually have to disable the clipped redrawing
   * because otherwise we end up with nasty trails of rectangles everywhere.
   */
  if (clutter_paint_debug_flags & CLUTTER_DEBUG_REDRAWS)
    clutter_paint_debug_flags |= CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS;

  /* The same is true when drawing the outlines of paint volumes... */
  if (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_VOLUMES)
    {
      clutter_paint_debug_flags |=
        CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS | CLUTTER_DEBUG_DISABLE_CULLING;
    }

  if (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION)
    g_message ("Enabling damaged region");

  if (!_clutter_backend_create_context (context->backend, error))
    return FALSE;

  priv->text_direction = clutter_get_text_direction ();

  /* Initialize a11y */
  if (clutter_enable_accessibility)
    {
      _clutter_accessibility_override_atk_util ();
      CLUTTER_NOTE (MISC, "Clutter Accessibility initialized");
    }

  /* Initialize types required for paint nodes */
  clutter_paint_node_init_types (context->backend);

  return TRUE;
}

static void
init_clutter_debug (ClutterContext *context)
{
  const char *env_string;

#ifdef CLUTTER_ENABLE_DEBUG
  env_string = g_getenv ("CLUTTER_DEBUG");
  if (env_string != NULL)
    {
      clutter_debug_flags =
        g_parse_debug_string (env_string,
                              clutter_debug_keys,
                              G_N_ELEMENTS (clutter_debug_keys));
      env_string = NULL;
    }
#endif /* CLUTTER_ENABLE_DEBUG */

  env_string = g_getenv ("CLUTTER_PICK");
  if (env_string != NULL)
    {
      clutter_pick_debug_flags =
        g_parse_debug_string (env_string,
                              clutter_pick_debug_keys,
                              G_N_ELEMENTS (clutter_pick_debug_keys));
      env_string = NULL;
    }

  env_string = g_getenv ("CLUTTER_PAINT");
  if (env_string != NULL)
    {
      clutter_paint_debug_flags =
        g_parse_debug_string (env_string,
                              clutter_paint_debug_keys,
                              G_N_ELEMENTS (clutter_paint_debug_keys));
      env_string = NULL;
    }

  env_string = g_getenv ("CLUTTER_SHOW_FPS");
  if (env_string)
    clutter_show_fps = TRUE;

  env_string = g_getenv ("CLUTTER_DISABLE_ACCESSIBILITY");
  if (env_string)
    clutter_enable_accessibility = FALSE;
}

ClutterContext *
clutter_context_new (ClutterBackendConstructor   backend_constructor,
                     gpointer                    user_data,
                     GError                    **error)
{
  ClutterContext *context;
  ClutterContextPrivate *priv;

  context = g_object_new (CLUTTER_TYPE_CONTEXT, NULL);
  priv = clutter_context_get_instance_private (context);

  init_clutter_debug (context);
  context->show_fps = clutter_show_fps;

  context->backend = backend_constructor (context, user_data);
  context->settings = g_object_new (CLUTTER_TYPE_SETTINGS, NULL);
  _clutter_settings_set_backend (context->settings,
                                 context->backend);

  context->stage_manager = g_object_new (CLUTTER_TYPE_STAGE_MANAGER, NULL);

  context->events_queue =
    g_async_queue_new_full ((GDestroyNotify) clutter_event_free);
  context->last_repaint_id = 1;

  priv->color_manager = g_object_new (CLUTTER_TYPE_COLOR_MANAGER,
                                      "context", context,
                                      NULL);
  priv->pipeline_cache = g_object_new (CLUTTER_TYPE_PIPELINE_CACHE, NULL);

  if (!clutter_context_init_real (context, error))
    return NULL;

  return context;
}

void
clutter_context_destroy (ClutterContext *context)
{
  g_object_run_dispose (G_OBJECT (context));
  g_object_unref (context);
}

ClutterBackend *
clutter_context_get_backend (ClutterContext *context)
{
  return context->backend;
}

ClutterTextDirection
clutter_context_get_text_direction (ClutterContext *context)
{
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  return priv->text_direction;
}

/**
 * clutter_context_get_pipeline_cache: (skip)
 */
ClutterPipelineCache *
clutter_context_get_pipeline_cache (ClutterContext *context)
{
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  return priv->pipeline_cache;
}

ClutterColorManager *
clutter_context_get_color_manager (ClutterContext *context)
{
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  return priv->color_manager;
}

/**
 * clutter_get_accessibility_enabled:
 *
 * Returns whether Clutter has accessibility support enabled.
 *
 * Return value: %TRUE if Clutter has accessibility support enabled
 */
gboolean
clutter_get_accessibility_enabled (void)
{
  return clutter_enable_accessibility;
}

ClutterStageManager *
clutter_context_get_stage_manager (ClutterContext *context)
{
  return context->stage_manager;
}

gboolean
clutter_context_get_show_fps (ClutterContext *context)
{
  return context->show_fps;
}

ClutterSettings *
clutter_context_get_settings (ClutterContext *context)
{
  g_return_val_if_fail (CLUTTER_IS_CONTEXT (context), NULL);

  return context->settings;
}
