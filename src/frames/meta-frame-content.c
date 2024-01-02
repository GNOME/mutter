/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "meta-frame-content.h"

struct _MetaFrameContent
{
  GtkWidget parent_instance;
  Window window;
  GtkBorder border;
  gboolean border_initialized;
};

enum {
  PROP_0,
  PROP_XWINDOW,
  PROP_BORDER,
  N_PROPS
};

static GParamSpec *props[N_PROPS] = { 0, };

G_DEFINE_TYPE (MetaFrameContent, meta_frame_content, GTK_TYPE_WIDGET)

static void
meta_frame_content_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaFrameContent *frame_content = META_FRAME_CONTENT (object);

  switch (prop_id)
    {
    case PROP_XWINDOW:
      frame_content->window = (Window) g_value_get_ulong (value);
      break;
    case PROP_BORDER:
      frame_content->border = *(GtkBorder*) g_value_get_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_frame_content_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaFrameContent *frame_content = META_FRAME_CONTENT (object);

  switch (prop_id)
    {
    case PROP_XWINDOW:
      g_value_set_ulong (value, (gulong) frame_content->window);
      break;
    case PROP_BORDER:
      g_value_set_boxed (value, &frame_content->border);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_frame_content_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *minimum_baseline,
                            int            *natural_baseline)
{
  *minimum_baseline = *natural_baseline = -1;
  *minimum = *natural = 1;
}

static void
meta_frame_content_update_border (MetaFrameContent *content,
                                  GtkBorder         border)
{
  if (content->border_initialized &&
      content->border.left == border.left &&
      content->border.right == border.right &&
      content->border.top == border.top &&
      content->border.bottom == border.bottom)
    return;

  content->border = border;
  content->border_initialized = TRUE;
  g_object_notify (G_OBJECT (content), "border");
}

static void
meta_frame_content_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  MetaFrameContent *content = META_FRAME_CONTENT (widget);
  GtkWindow *window = GTK_WINDOW (gtk_widget_get_root (widget));
  graphene_point_t point = {};
  double scale;

  if (!gtk_widget_compute_point (widget,
                                 GTK_WIDGET (window),
                                 &point, &point))
    return;

  scale = gdk_surface_get_scale_factor (gtk_native_get_surface (GTK_NATIVE (window)));

  meta_frame_content_update_border (content,
                                    /* FIXME: right/bottom are broken, if they
                                     * are ever other than 0.
                                     */
                                    (GtkBorder) {
                                      point.x * scale, 0,
                                      point.y * scale, 0,
                                    });
}

static void
meta_frame_content_class_init (MetaFrameContentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = meta_frame_content_set_property;
  object_class->get_property = meta_frame_content_get_property;

  widget_class->measure = meta_frame_content_measure;
  widget_class->size_allocate = meta_frame_content_size_allocate;

  props[PROP_XWINDOW] = g_param_spec_ulong ("xwindow", NULL, NULL,
                                            0, G_MAXULONG, 0,
                                            G_PARAM_READWRITE |
                                            G_PARAM_CONSTRUCT_ONLY |
                                            G_PARAM_STATIC_NAME |
                                            G_PARAM_STATIC_NICK |
                                            G_PARAM_STATIC_BLURB);
  props[PROP_BORDER] = g_param_spec_boxed ("border", NULL, NULL,
                                           GTK_TYPE_BORDER,
                                           G_PARAM_READABLE |
                                           G_PARAM_EXPLICIT_NOTIFY |
                                           G_PARAM_STATIC_NAME |
                                           G_PARAM_STATIC_NICK |
                                           G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (object_class,
                                     G_N_ELEMENTS (props),
                                     props);
}

static void
meta_frame_content_init (MetaFrameContent *content)
{
}

GtkWidget *
meta_frame_content_new (Window window)
{
  return g_object_new (META_TYPE_FRAME_CONTENT,
                       "xwindow", window,
                       NULL);
}

Window
meta_frame_content_get_window (MetaFrameContent *content)
{
  return content->window;
}

GtkBorder
meta_frame_content_get_border (MetaFrameContent *content)
{
  return content->border;
}
