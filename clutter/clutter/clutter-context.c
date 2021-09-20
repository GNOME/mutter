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

#include <hb-glib.h>

#include "cally/cally.h"
#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-graphene.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-settings-private.h"

static gboolean clutter_disable_mipmap_text = FALSE;
static gboolean clutter_show_fps = FALSE;

#ifdef CLUTTER_ENABLE_DEBUG
static const GDebugKey clutter_debug_keys[] = {
  { "misc", CLUTTER_DEBUG_MISC },
  { "actor", CLUTTER_DEBUG_ACTOR },
  { "texture", CLUTTER_DEBUG_TEXTURE },
  { "event", CLUTTER_DEBUG_EVENT },
  { "paint", CLUTTER_DEBUG_PAINT },
  { "pick", CLUTTER_DEBUG_PICK },
  { "pango", CLUTTER_DEBUG_PANGO },
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
  { "max-render-time", CLUTTER_DEBUG_PAINT_MAX_RENDER_TIME },
};

typedef struct _ClutterContextPrivate
{
  ClutterTextDirection text_direction;
} ClutterContextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterContext, clutter_context, G_TYPE_OBJECT)

static void
clutter_context_dispose (GObject *object)
{
  ClutterContext *context = CLUTTER_CONTEXT (object);

  g_clear_pointer (&context->events_queue, g_async_queue_unref);
  g_clear_pointer (&context->backend, clutter_backend_destroy);

  G_OBJECT_CLASS (clutter_context_parent_class)->dispose (object);
}

static void
clutter_context_class_init (ClutterContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_context_dispose;

  clutter_graphene_init ();
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
  const gchar *direction;

  direction = g_getenv ("CLUTTER_TEXT_DIRECTION");
  if (direction && *direction != '\0')
    {
      if (strcmp (direction, "rtl") == 0)
        dir = CLUTTER_TEXT_DIRECTION_RTL;
      else if (strcmp (direction, "ltr") == 0)
        dir = CLUTTER_TEXT_DIRECTION_LTR;
    }
  else
    {
      PangoLanguage *language;
      const PangoScript *scripts;
      int n_scripts, i;

      language = pango_language_get_default ();
      scripts = pango_language_get_scripts (language, &n_scripts);

      for (i = 0; i < n_scripts; i++)
        {
          hb_script_t script;
          hb_direction_t text_dir;

          script = hb_glib_script_to_script ((GUnicodeScript) scripts[i]);
          text_dir = hb_script_get_horizontal_direction (script);

          if (text_dir == HB_DIRECTION_LTR)
            dir = CLUTTER_TEXT_DIRECTION_LTR;
          else if (text_dir == HB_DIRECTION_RTL)
            dir = CLUTTER_TEXT_DIRECTION_RTL;
          else
            continue;
        }
    }

  CLUTTER_NOTE (MISC, "Text direction: %s",
                dir == CLUTTER_TEXT_DIRECTION_RTL ? "rtl" : "ltr");

  return dir;
}

static gboolean
clutter_context_init_real (ClutterContext       *context,
                           ClutterContextFlags   flags,
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

  context->is_initialized = TRUE;

  /* Initialize a11y */
  if (!(flags & CLUTTER_CONTEXT_FLAG_NO_A11Y))
    cally_accessibility_init ();

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

  env_string = g_getenv ("CLUTTER_DISABLE_MIPMAPPED_TEXT");
  if (env_string)
    clutter_disable_mipmap_text = TRUE;
}

ClutterContext *
clutter_context_new (ClutterContextFlags         flags,
                     ClutterBackendConstructor   backend_constructor,
                     gpointer                    user_data,
                     GError                    **error)
{
  ClutterContext *context;

  context = g_object_new (CLUTTER_TYPE_CONTEXT, NULL);

  init_clutter_debug (context);
  context->show_fps = clutter_show_fps;
  context->is_initialized = FALSE;

  context->backend = backend_constructor (user_data);
  context->settings = clutter_settings_get_default ();
  _clutter_settings_set_backend (context->settings,
                                 context->backend);

  context->events_queue =
    g_async_queue_new_full ((GDestroyNotify) clutter_event_free);
  context->last_repaint_id = 1;

  if (!clutter_context_init_real (context, flags, error))
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

CoglPangoFontMap *
clutter_context_get_pango_fontmap (ClutterContext *context)
{
  CoglPangoFontMap *font_map;
  gdouble resolution;
  gboolean use_mipmapping;

  if (G_LIKELY (context->font_map != NULL))
    return context->font_map;

  font_map = COGL_PANGO_FONT_MAP (cogl_pango_font_map_new ());

  resolution = clutter_backend_get_resolution (context->backend);
  cogl_pango_font_map_set_resolution (font_map, resolution);

  use_mipmapping = !clutter_disable_mipmap_text;
  cogl_pango_font_map_set_use_mipmapping (font_map, use_mipmapping);

  context->font_map = font_map;

  return context->font_map;
}

ClutterTextDirection
clutter_context_get_text_direction (ClutterContext *context)
{
  ClutterContextPrivate *priv = clutter_context_get_instance_private (context);

  return priv->text_direction;
}
