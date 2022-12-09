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

#include "meta-frame-header.h"

struct _MetaFrameHeader
{
  GtkWidget parent_instance;
};

G_DEFINE_TYPE (MetaFrameHeader, meta_frame_header, GTK_TYPE_WIDGET)

static void
meta_frame_header_dispose (GObject *object)
{
  GtkWidget *widget = GTK_WIDGET (object);
  GtkWidget *child;

  child = gtk_widget_get_first_child (widget);
  if (child)
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (meta_frame_header_parent_class)->dispose (object);
}

static void
meta_frame_header_measure (GtkWidget      *widget,
                           GtkOrientation  orientation,
                           int             for_size,
                           int            *minimum,
                           int            *natural,
                           int            *minimum_baseline,
                           int            *natural_baseline)
{
  *minimum_baseline = *natural_baseline = -1;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = *natural = 1;
    }
  else
    {
      GtkWidget *child;

      child = gtk_widget_get_first_child (widget);
      gtk_widget_measure (child,
                          orientation, for_size,
                          minimum, natural,
                          minimum_baseline,
                          natural_baseline);
    }
}

static void
meta_frame_header_size_allocate (GtkWidget *widget,
                                 int        width,
                                 int        height,
                                 int        baseline)
{
  GtkWidget *child;
  int minimum;
  gboolean shrunk;
  GtkAllocation child_allocation;

  child = gtk_widget_get_first_child (widget);

  gtk_widget_measure (child,
                      GTK_ORIENTATION_HORIZONTAL,
                      height,
                      &minimum, NULL, NULL, NULL);

  shrunk = width < minimum;

  child_allocation.x = shrunk ? width - minimum : 0;
  child_allocation.y = 0;
  child_allocation.width = shrunk ? minimum : width;
  child_allocation.height = height;

  gtk_widget_size_allocate (child, &child_allocation, baseline);
}

static void
meta_frame_header_class_init (MetaFrameHeaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = meta_frame_header_dispose;

  widget_class->measure = meta_frame_header_measure;
  widget_class->size_allocate = meta_frame_header_size_allocate;
}

static void
meta_frame_header_init (MetaFrameHeader *content)
{
  GtkWidget *header_bar;

  header_bar = gtk_header_bar_new ();
  gtk_widget_add_css_class (header_bar, "titlebar");
  gtk_widget_add_css_class (header_bar, "default-decoration");
  gtk_widget_insert_before (header_bar, GTK_WIDGET (content), NULL);

  gtk_widget_add_css_class (GTK_WIDGET (content), "default-decoration");
}

GtkWidget *
meta_frame_header_new (void)
{
  return g_object_new (META_TYPE_FRAME_HEADER, NULL);
}
