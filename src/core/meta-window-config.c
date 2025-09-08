/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include "core/meta-window-config-private.h"

#include "core/window-private.h"

/**
 * MetaWindowConfig:
 *
 * An object representing the configuration of a top-level window
 *
 */
struct _MetaWindowConfig
{
  GObject parent;

  /* Whether this is an initial window configuration, cannot be changed by the callee */
  gboolean is_initial;

  /* The window geometry */
  MtkRectangle rect;
  gboolean has_position;

  gboolean is_fullscreen;

  gboolean maximized_horizontally;
  gboolean maximized_vertically;

  MetaTileMode tile_mode;
  int tile_monitor_number;
  double tile_hfraction;
  MetaWindow *tile_match;
};

G_DEFINE_FINAL_TYPE (MetaWindowConfig, meta_window_config, G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_RECT,
  PROP_IS_FULLSCREEN,

  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

static void
meta_window_config_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaWindowConfig *window_config = META_WINDOW_CONFIG (object);

  switch (prop_id)
    {
    case PROP_RECT:
      g_value_set_boxed (value, &window_config->rect);
      break;
    case PROP_IS_FULLSCREEN:
      g_value_set_boolean (value, window_config->is_fullscreen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_config_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaWindowConfig *window_config = META_WINDOW_CONFIG (object);
  MtkRectangle *rect;

  switch (prop_id)
    {
    case PROP_RECT:
      rect = g_value_get_boxed (value);
      window_config->rect = *rect;
      break;
    case PROP_IS_FULLSCREEN:
      window_config->is_fullscreen = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_config_class_init (MetaWindowConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_window_config_get_property;
  object_class->set_property = meta_window_config_set_property;

  obj_props[PROP_RECT] =
    g_param_spec_boxed ("rect", NULL, NULL,
                        MTK_TYPE_RECTANGLE,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_IS_FULLSCREEN] =
    g_param_spec_boolean ("is-fullscreen", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

static void
meta_window_config_init (MetaWindowConfig *window_config)
{
  window_config->rect = MTK_RECTANGLE_INIT (0, 0, 0, 0);
  window_config->tile_monitor_number = -1;
  window_config->tile_hfraction = -1.0;
}

gboolean
meta_window_config_get_is_initial (MetaWindowConfig *window_config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (window_config), FALSE);

  return window_config->is_initial;
}

void
meta_window_config_set_rect (MetaWindowConfig *window_config,
                             MtkRectangle      rect)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (window_config));

  window_config->rect = rect;
  window_config->has_position = TRUE;
}

MtkRectangle
meta_window_config_get_rect (MetaWindowConfig *window_config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (window_config),
                        MTK_RECTANGLE_INIT (0,0,0,0));

  return window_config->rect;
}

void
meta_window_config_set_is_fullscreen (MetaWindowConfig *window_config,
                                      gboolean          is_fullscreen)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (window_config));

  window_config->is_fullscreen = is_fullscreen;
}

void
meta_window_config_get_position (MetaWindowConfig *window_config,
                                 int              *x,
                                 int              *y)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (window_config));

  if (x)
    *x = window_config->rect.x;
  if (y)
    *y = window_config->rect.y;
}

void
meta_window_config_set_position (MetaWindowConfig *window_config,
                                 int               x,
                                 int               y)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (window_config));

  window_config->rect.x = x;
  window_config->rect.y = y;
  window_config->has_position = TRUE;
}

void
meta_window_config_get_size (MetaWindowConfig *window_config,
                             int              *width,
                             int              *height)
{
  g_return_if_fail (window_config);

  if (width)
    *width = window_config->rect.width;
  if (height)
    *height = window_config->rect.height;
}

void
meta_window_config_set_size (MetaWindowConfig *window_config,
                             int               width,
                             int               height)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (window_config));

  window_config->rect.width = width;
  window_config->rect.height = height;
}

gboolean
meta_window_config_get_is_fullscreen (MetaWindowConfig *window_config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (window_config), FALSE);

  return window_config->is_fullscreen;
}

gboolean
meta_window_config_is_maximized (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);

  return config->maximized_horizontally && config->maximized_vertically;
}

gboolean
meta_window_config_is_any_maximized (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);

  return config->maximized_horizontally || config->maximized_vertically;
}

gboolean
meta_window_config_is_maximized_horizontally (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);

  return config->maximized_horizontally;
}

gboolean
meta_window_config_is_maximized_vertically (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);

  return config->maximized_vertically;
}

void
meta_window_config_set_maximized_directions (MetaWindowConfig *config,
                                             gboolean          horizontally,
                                             gboolean          vertically)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (config));

  config->maximized_horizontally = horizontally;
  config->maximized_vertically = vertically;
}

MetaTileMode
meta_window_config_get_tile_mode (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), META_TILE_NONE);

  return config->tile_mode;
}

int
meta_window_config_get_tile_monitor_number (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), -1);

  return config->tile_monitor_number;
}

double
meta_window_config_get_tile_hfraction (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), -1.0);

  return config->tile_hfraction;
}

MetaWindow *
meta_window_config_get_tile_match (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), NULL);

  return config->tile_match;
}

void
meta_window_config_set_tile_mode (MetaWindowConfig *config,
                                  MetaTileMode      tile_mode)
{
  config->tile_mode = tile_mode;
}

void
meta_window_config_set_tile_monitor_number (MetaWindowConfig *config,
                                            int               tile_monitor_number)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (config));

  config->tile_monitor_number = tile_monitor_number;
}

void
meta_window_config_set_tile_hfraction (MetaWindowConfig *config,
                                       double            hfraction)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (config));

  config->tile_hfraction = hfraction;
}

void
meta_window_config_set_tile_match (MetaWindowConfig *config,
                                   MetaWindow       *tile_match)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (config));

  config->tile_match = tile_match;
}

gboolean
meta_window_config_is_floating (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);

  if (config->is_fullscreen)
    return FALSE;

  if (meta_window_config_is_maximized (config))
    return FALSE;

  if (meta_window_config_get_tile_mode (config))
    return FALSE;

  return TRUE;
}

gboolean
meta_window_config_has_position (MetaWindowConfig *config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);

  return config->has_position;
}

MetaWindowConfig *
meta_window_config_new (void)
{
  return g_object_new (META_TYPE_WINDOW_CONFIG,
                       NULL);
}

MetaWindowConfig *
meta_window_config_initial_new (void)
{
  MetaWindowConfig *window_config;

  window_config = meta_window_config_new ();
  window_config->is_initial = TRUE;

  return window_config;
}

MetaWindowConfig *
meta_window_config_new_from (MetaWindowConfig *other_config)
{
  MetaWindowConfig *config;

  g_return_val_if_fail (META_IS_WINDOW_CONFIG (other_config), NULL);

  config = meta_window_config_new ();
  config->is_initial = other_config->is_initial;
  config->rect = meta_window_config_get_rect (other_config);
  config->is_fullscreen = other_config->is_fullscreen;
  config->maximized_horizontally = other_config->maximized_horizontally;
  config->maximized_vertically = other_config->maximized_vertically;
  config->tile_mode = other_config->tile_mode;
  config->tile_monitor_number = other_config->tile_monitor_number;
  config->tile_hfraction = other_config->tile_hfraction;
  config->tile_match = other_config->tile_match;

  return config;
}

void
meta_window_config_set_initial (MetaWindowConfig *config)
{
  g_return_if_fail (META_IS_WINDOW_CONFIG (config));

  config->is_initial = TRUE;
}

gboolean
meta_window_config_is_equivalent (MetaWindowConfig *config,
                                  MetaWindowConfig *other_config)
{
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (config), FALSE);
  g_return_val_if_fail (META_IS_WINDOW_CONFIG (other_config), FALSE);

  return (mtk_rectangle_equal (&config->rect, &other_config->rect) &&
          config->is_fullscreen == other_config->is_fullscreen &&
          config->maximized_horizontally == other_config->maximized_horizontally &&
          config->maximized_vertically == other_config->maximized_vertically &&
          config->tile_mode == other_config->tile_mode &&
          config->tile_monitor_number == other_config->tile_monitor_number &&
          config->tile_hfraction == other_config->tile_hfraction);
}

gboolean
meta_window_config_is_tiled_side_by_side (MetaWindowConfig *config)
{
  return (meta_window_config_is_maximized_vertically (config) &&
          !meta_window_config_is_maximized_horizontally (config) &&
          meta_window_config_get_tile_mode (config) != META_TILE_NONE);
}
