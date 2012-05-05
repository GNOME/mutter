/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Metacity window frame manager widget */

/* 
 * Copyright (C) 2001 Havoc Pennington
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_UIFRAME_H
#define META_UIFRAME_H

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <meta/common.h>
#include "theme-private.h"

typedef enum
{
  META_FRAME_CONTROL_NONE,
  META_FRAME_CONTROL_TITLE,
  META_FRAME_CONTROL_DELETE,
  META_FRAME_CONTROL_MENU,
  META_FRAME_CONTROL_MINIMIZE,
  META_FRAME_CONTROL_MAXIMIZE,
  META_FRAME_CONTROL_UNMAXIMIZE,
  META_FRAME_CONTROL_SHADE,
  META_FRAME_CONTROL_UNSHADE,
  META_FRAME_CONTROL_ABOVE,
  META_FRAME_CONTROL_UNABOVE,
  META_FRAME_CONTROL_STICK,
  META_FRAME_CONTROL_UNSTICK,
  META_FRAME_CONTROL_RESIZE_SE,
  META_FRAME_CONTROL_RESIZE_S,
  META_FRAME_CONTROL_RESIZE_SW,
  META_FRAME_CONTROL_RESIZE_N,
  META_FRAME_CONTROL_RESIZE_NE,
  META_FRAME_CONTROL_RESIZE_NW,
  META_FRAME_CONTROL_RESIZE_W,
  META_FRAME_CONTROL_RESIZE_E,
  META_FRAME_CONTROL_CLIENT_AREA
} MetaFrameControl;

/* This is one widget that manages all the window frames
 * as subwindows.
 */

#define META_TYPE_UIFRAME            (meta_uiframe_get_type ())
#define META_UIFRAME(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_UIFRAME, MetaUIFrame))
#define META_UIFRAME_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_UIFRAME, MetaUIFrameClass))
#define META_IS_UIFRAME(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_UIFRAME))
#define META_IS_UIFRAME_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_UIFRAME))
#define META_UIFRAME_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_UIFRAME, MetaUIFrameClass))

typedef struct _MetaUIFrame        MetaUIFrame;
typedef struct _MetaUIFrameClass   MetaUIFrameClass;

struct _MetaUIFrame
{
  GtkWindow parent_instance;

  Window xwindow;
  GdkWindow *window;
  MetaThemeVariant *tv;
  MetaFrameControl prelit_control;

  GtkWidget *label;
  GtkWidget *container;
};

struct _MetaUIFrameClass
{
  GtkWindowClass parent_class;
};

GType        meta_uiframe_get_type (void) G_GNUC_CONST;

void meta_uiframe_set_title (MetaUIFrame *frame,
                             const char  *title);

void meta_uiframe_attach_style (MetaUIFrame *frame);

void meta_uiframe_calc_geometry (MetaUIFrame       *frame,
                                 MetaFrameGeometry *fgeom);

#endif
