/*
 * Copyright 2026 Red Hat, Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/meta-cursor-theme.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-xcursor.h"

enum
{
  PROP_0,
  PROP_BACKEND,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

struct _MetaCursorTheme
{
  GObject parent_instance;

  GHashTable *cursors;

  ClutterColorState *color_state;
  MetaBackend *backend;
};

G_DEFINE_TYPE (MetaCursorTheme, meta_cursor_theme, G_TYPE_OBJECT)

static void
meta_cursor_theme_finalize (GObject *object)
{
  MetaCursorTheme *cursor_theme = META_CURSOR_THEME (object);

  g_clear_pointer (&cursor_theme->cursors, g_hash_table_unref);
  g_clear_object (&cursor_theme->color_state);

  G_OBJECT_CLASS (meta_cursor_theme_parent_class)->finalize (object);
}

static void
meta_cursor_theme_constructed (GObject *object)
{
  MetaCursorTheme *cursor_theme = META_CURSOR_THEME (object);
  ClutterContext *clutter_context =
    meta_backend_get_clutter_context (cursor_theme->backend);

  g_set_object (&cursor_theme->color_state,
                clutter_context_get_default_color_state (clutter_context));

  G_OBJECT_CLASS (meta_cursor_theme_parent_class)->constructed (object);
}

static void
meta_cursor_theme_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaCursorTheme *cursor_theme = META_CURSOR_THEME (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      cursor_theme->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_cursor_theme_class_init (MetaCursorThemeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_theme_finalize;
  object_class->constructed = meta_cursor_theme_constructed;
  object_class->set_property = meta_cursor_theme_set_property;

  props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_cursor_theme_init (MetaCursorTheme *cursor_theme)
{
  cursor_theme->cursors =
    g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) g_object_unref);

  /* Ensure GType of default cursor implementation, so it is registered
   * as an extension.
   */
  g_type_ensure (META_TYPE_CURSOR_XCURSOR);
}

MetaCursorTheme *
meta_cursor_theme_new (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CURSOR_THEME,
                       "backend", backend,
                       NULL);
}

ClutterCursor *
meta_cursor_theme_get_cursor (MetaCursorTheme   *cursor_theme,
                              ClutterCursorType  cursor_type)
{
  ClutterCursor *cursor;

  cursor = g_hash_table_lookup (cursor_theme->cursors,
                                GUINT_TO_POINTER (cursor_type));

  if (!cursor)
    {
      GIOExtensionPoint *ep;
      GList *l;

      ep = g_io_extension_point_lookup (META_CURSOR_EXTENSION_POINT_NAME);

      for (l = g_io_extension_point_get_extensions (ep); l; l = l->next)
        {
          GIOExtension *extension = l->data;

          cursor = g_object_new (g_io_extension_get_type (extension),
                                 "color-state", cursor_theme->color_state,
                                 "cursor-type", cursor_type,
                                 "backend", cursor_theme->backend,
                                 "theme-name", meta_prefs_get_cursor_theme (),
                                 "size", meta_prefs_get_cursor_size (),
                                 NULL);

          g_assert (cursor != NULL);
          break;
        }

      if (!cursor)
        return NULL;

      g_hash_table_insert (cursor_theme->cursors,
                           GUINT_TO_POINTER (cursor_type),
                           cursor);
    }

  return g_object_ref (cursor);
}

void
meta_cursor_theme_reset (MetaCursorTheme *cursor_theme)
{
  g_hash_table_remove_all (cursor_theme->cursors);
}
