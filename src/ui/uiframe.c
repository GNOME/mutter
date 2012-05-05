/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Metacity window frame manager widget */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 2005, 2006 Elijah Newren
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

#include <config.h>
#include <math.h>
#include <string.h>
#include <meta/boxes.h>
#include "uiframe.h"
#include <meta/util.h>
#include "core.h"
#include "menu.h"
#include <meta/prefs.h>
#include "ui.h"

#ifdef HAVE_SHAPE
#include <X11/extensions/shape.h>
#endif

static MetaFrameControl get_control  (MetaUIFrame       *frame,
                                      int                x,
                                      int                y);

G_DEFINE_TYPE (MetaUIFrame, meta_uiframe, GTK_TYPE_WINDOW);


static void
initialize_style_context (MetaUIFrame *frame)
{
  GtkWidget *widget;
  GtkCssProvider *provider;
  GdkScreen *screen;
  char *theme_name, *variant;

  if (G_LIKELY (frame->style_context_initialized))
    return;

  widget = GTK_WIDGET (frame);

  screen = gtk_widget_get_screen (widget);
  g_object_get (gtk_settings_get_for_screen (screen),
                "gtk-theme-name", &theme_name,
                NULL);

  meta_core_get (GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (frame))),
                 frame->xwindow,
                 META_CORE_GET_THEME_VARIANT, &variant,
                 META_CORE_GET_END);

  provider = gtk_css_provider_get_named (theme_name, variant);
  gtk_style_context_add_provider (gtk_widget_get_style_context (widget),
                                  GTK_STYLE_PROVIDER (provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_THEME);

  g_free (theme_name);

  frame->style_context_initialized = TRUE;
}

static void
sync_state_flags (MetaUIFrame *frame)
{
  MetaFrameFlags flags;
  GtkStateFlags gtk_flags;

  initialize_style_context (frame);

  meta_core_get (GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (frame))),
                 frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_END);

  gtk_flags = GTK_STATE_FLAG_NORMAL;

  if ((flags & META_FRAME_HAS_FOCUS) == 0)
    gtk_flags |= GTK_STATE_FLAG_BACKDROP;

  gtk_widget_set_state_flags (GTK_WIDGET (frame), gtk_flags, TRUE);
}

void
meta_uiframe_get_frame_borders (MetaUIFrame      *frame,
                                MetaFrameBorders *borders)
{
  GtkWidget *widget = GTK_WIDGET (frame);
  GtkBorder padding;
  GtkStyleContext *style_context;
  MetaFrameType type;
  MetaFrameFlags flags;
  int draggable_borders;

  meta_core_get (GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (frame))),
                 frame->xwindow,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_END);

  /* For a full-screen window, we don't have any borders, visible or not. */
  if (flags & META_FRAME_FULLSCREEN)
    return;

  sync_state_flags (frame);

  meta_frame_borders_clear (borders);

  style_context = gtk_widget_get_style_context (widget);

  gtk_style_context_get_border (style_context,
                                gtk_widget_get_state_flags (widget),
                                &borders->visible);

  gtk_style_context_get_padding (style_context,
                                 gtk_widget_get_state_flags (widget),
                                 &padding);

  borders->visible.left += padding.left;
  borders->visible.right += padding.right;
  borders->visible.top += padding.top;
  borders->visible.bottom += padding.bottom;

  draggable_borders = meta_prefs_get_draggable_border_width ();

  if (flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE)
    {
      borders->invisible.left   = MAX (0, draggable_borders - borders->visible.left);
      borders->invisible.right  = MAX (0, draggable_borders - borders->visible.right);
    }

  if (flags & META_FRAME_ALLOWS_VERTICAL_RESIZE)
    {
      borders->invisible.bottom = MAX (0, draggable_borders - borders->visible.bottom);

      /* borders.visible.top is the height of the *title bar*. We can't do the same
       * algorithm here, titlebars are expectedly much bigger. Just subtract a couple
       * pixels to get a proper feel. */
      if (type != META_FRAME_TYPE_ATTACHED)
        borders->invisible.top    = MAX (0, draggable_borders - 2);
    }

  borders->total.left   = borders->invisible.left   + borders->visible.left;
  borders->total.right  = borders->invisible.right  + borders->visible.right;
  borders->total.bottom = borders->invisible.bottom + borders->visible.bottom;
  borders->total.top    = borders->invisible.top    + borders->visible.top;
}

static void
meta_uiframe_finalize (GObject *obj)
{
  MetaUIFrame *frame = META_UIFRAME (obj);

  if (frame->window)
    g_object_unref (frame->window);
}

static void
meta_uiframe_init (MetaUIFrame *frame)
{
  GtkWidget *container, *label;

  frame->container = container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  frame->label = label = gtk_label_new ("");
  frame->style_context_initialized = FALSE;

  gtk_container_add (GTK_CONTAINER (frame), container);
  gtk_container_add (GTK_CONTAINER (container), frame->label);

  gtk_widget_set_hexpand (container, TRUE);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (label, GTK_ALIGN_START);

  gtk_widget_show_all (GTK_WIDGET (container));
}

/* The client rectangle surrounds client window; it subtracts both
 * the visible and invisible borders from the frame window's size.
 */
static void
get_client_rect (MetaFrameBorders      *borders,
                 int                    window_width,
                 int                    window_height,
                 cairo_rectangle_int_t *rect)
{
  rect->x = borders->total.left;
  rect->y = borders->total.top;
  rect->width = window_width - borders->total.right - rect->x;
  rect->height = window_height - borders->total.bottom - rect->y;
}

void
meta_uiframe_set_title (MetaUIFrame *frame,
                        const char  *title)
{
  gtk_label_set_text (GTK_LABEL (frame->label), title);
}

static gboolean
meta_frame_titlebar_event (MetaUIFrame    *frame,
                           GdkEventButton *event,
                           int            action)
{
  MetaFrameFlags flags;
  Display *display;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  switch (action)
    {
    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_SHADE:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_SHADE)
          {
            if (flags & META_FRAME_SHADED)
              meta_core_unshade (display,
                                 frame->xwindow,
                                 event->time);
            else
              meta_core_shade (display,
                               frame->xwindow,
                               event->time);
          }
      }
      break;          
      
    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_HORIZONTALLY:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize_horizontally (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_VERTICALLY:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize_vertically (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MINIMIZE:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MINIMIZE)
          {
            meta_core_minimize (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_NONE:
      /* Yaay, a sane user that doesn't use that other weird crap! */
      break;
    
    case G_DESKTOP_TITLEBAR_ACTION_LOWER:
      meta_core_user_lower_and_unfocus (display,
                                        frame->xwindow,
                                        event->time);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MENU:
      meta_core_show_window_menu (display,
                                  frame->xwindow,
                                  event->x_root,
                                  event->y_root,
                                  event->button,
                                  event->time);
      break;
    }
  
  return TRUE;
}

static gboolean
meta_frame_double_click_event (MetaUIFrame    *frame,
                               GdkEventButton *event)
{
  int action = meta_prefs_get_action_double_click_titlebar ();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_middle_click_event (MetaUIFrame    *frame,
                               GdkEventButton *event)
{
  int action = meta_prefs_get_action_middle_click_titlebar();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_right_click_event(MetaUIFrame     *frame,
                             GdkEventButton  *event)
{
  int action = meta_prefs_get_action_right_click_titlebar();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_uiframe_button_press_event (GtkWidget      *widget,
                                 GdkEventButton *event)
{
  MetaUIFrame *frame;
  MetaFrameControl control;
  Display *display;

  frame = META_UIFRAME (widget);

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  /* Remember that the display may have already done something with this event.
   * If so there's probably a GrabOp in effect.
   */
  control = get_control (frame, event->x, event->y);

  /* focus on click, even if click was on client area */
  if (event->button == 1 &&
      !(control == META_FRAME_CONTROL_MINIMIZE ||
        control == META_FRAME_CONTROL_DELETE ||
        control == META_FRAME_CONTROL_MAXIMIZE))
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Focusing window with frame 0x%lx due to button 1 press\n",
                  frame->xwindow);
      meta_core_user_focus (display,
                            frame->xwindow,
                            event->time);      
    }

  /* don't do the rest of this if on client area */
  if (control == META_FRAME_CONTROL_CLIENT_AREA)
    return FALSE; /* not on the frame, just passed through from client */
  
  /* We want to shade even if we have a GrabOp, since we'll have a move grab
   * if we double click the titlebar.
   */
  if (control == META_FRAME_CONTROL_TITLE &&
      event->button == 1 &&
      event->type == GDK_2BUTTON_PRESS)
    {
      meta_core_end_grab_op (display, event->time);
      return meta_frame_double_click_event (frame, event);
    }

  if (meta_core_get_grab_op (display) !=
      META_GRAB_OP_NONE)
    return FALSE; /* already up to something */  

  if (event->button == 1 &&
      (control == META_FRAME_CONTROL_MAXIMIZE ||
       control == META_FRAME_CONTROL_UNMAXIMIZE ||
       control == META_FRAME_CONTROL_MINIMIZE ||
       control == META_FRAME_CONTROL_DELETE ||
       control == META_FRAME_CONTROL_SHADE ||
       control == META_FRAME_CONTROL_UNSHADE ||
       control == META_FRAME_CONTROL_ABOVE ||
       control == META_FRAME_CONTROL_UNABOVE ||
       control == META_FRAME_CONTROL_STICK ||
       control == META_FRAME_CONTROL_UNSTICK ||
       control == META_FRAME_CONTROL_MENU))
    {
      MetaGrabOp op = META_GRAB_OP_NONE;

      switch (control)
        {
        case META_FRAME_CONTROL_MINIMIZE:
          op = META_GRAB_OP_CLICKING_MINIMIZE;
          break;
        case META_FRAME_CONTROL_MAXIMIZE:
          op = META_GRAB_OP_CLICKING_MAXIMIZE;
          break;
        case META_FRAME_CONTROL_UNMAXIMIZE:
          op = META_GRAB_OP_CLICKING_UNMAXIMIZE;
          break;
        case META_FRAME_CONTROL_DELETE:
          op = META_GRAB_OP_CLICKING_DELETE;
          break;
        case META_FRAME_CONTROL_MENU:
          op = META_GRAB_OP_CLICKING_MENU;
          break;
        case META_FRAME_CONTROL_SHADE:
          op = META_GRAB_OP_CLICKING_SHADE;
          break;
        case META_FRAME_CONTROL_UNSHADE:
          op = META_GRAB_OP_CLICKING_UNSHADE;
          break;
        case META_FRAME_CONTROL_ABOVE:
          op = META_GRAB_OP_CLICKING_ABOVE;
          break;
        case META_FRAME_CONTROL_UNABOVE:
          op = META_GRAB_OP_CLICKING_UNABOVE;
          break;
        case META_FRAME_CONTROL_STICK:
          op = META_GRAB_OP_CLICKING_STICK;
          break;
        case META_FRAME_CONTROL_UNSTICK:
          op = META_GRAB_OP_CLICKING_UNSTICK;
          break;
        default:
          g_assert_not_reached ();
          break;
        }

      meta_core_begin_grab_op (display,
                               frame->xwindow,
                               op,
                               TRUE,
                               TRUE,
                               event->button,
                               0,
                               event->time,
                               event->x_root,
                               event->y_root);      
    }
  else if (event->button == 1 &&
           (control == META_FRAME_CONTROL_RESIZE_SE ||
            control == META_FRAME_CONTROL_RESIZE_S ||
            control == META_FRAME_CONTROL_RESIZE_SW ||
            control == META_FRAME_CONTROL_RESIZE_NE ||
            control == META_FRAME_CONTROL_RESIZE_N ||
            control == META_FRAME_CONTROL_RESIZE_NW ||
            control == META_FRAME_CONTROL_RESIZE_E ||
            control == META_FRAME_CONTROL_RESIZE_W))
    {
      MetaGrabOp op;
      
      op = META_GRAB_OP_NONE;
      
      switch (control)
        {
        case META_FRAME_CONTROL_RESIZE_SE:
          op = META_GRAB_OP_RESIZING_SE;
          break;
        case META_FRAME_CONTROL_RESIZE_S:
          op = META_GRAB_OP_RESIZING_S;
          break;
        case META_FRAME_CONTROL_RESIZE_SW:
          op = META_GRAB_OP_RESIZING_SW;
          break;
        case META_FRAME_CONTROL_RESIZE_NE:
          op = META_GRAB_OP_RESIZING_NE;
          break;
        case META_FRAME_CONTROL_RESIZE_N:
          op = META_GRAB_OP_RESIZING_N;
          break;
        case META_FRAME_CONTROL_RESIZE_NW:
          op = META_GRAB_OP_RESIZING_NW;
          break;
        case META_FRAME_CONTROL_RESIZE_E:
          op = META_GRAB_OP_RESIZING_E;
          break;
        case META_FRAME_CONTROL_RESIZE_W:
          op = META_GRAB_OP_RESIZING_W;
          break;
        default:
          g_assert_not_reached ();
          break;
        }

      meta_core_begin_grab_op (display,
                               frame->xwindow,
                               op,
                               TRUE,
                               TRUE,
                               event->button,
                               0,
                               event->time,
                               event->x_root,
                               event->y_root);
    }
  else if (control == META_FRAME_CONTROL_TITLE &&
           event->button == 1)
    {
      MetaFrameFlags flags;

      meta_core_get (display, frame->xwindow,
                     META_CORE_GET_FRAME_FLAGS, &flags,
                     META_CORE_GET_END);

      if (flags & META_FRAME_ALLOWS_MOVE)
        {          
          meta_core_begin_grab_op (display,
                                   frame->xwindow,
                                   META_GRAB_OP_MOVING,
                                   TRUE,
                                   TRUE,
                                   event->button,
                                   0,
                                   event->time,
                                   event->x_root,
                                   event->y_root);
        }
    }
  else if (event->button == 2)
    {
      return meta_frame_middle_click_event (frame, event);
    }
  else if (event->button == 3)
    {
      return meta_frame_right_click_event (frame, event);
    }
  
  return TRUE;
}

static gboolean
meta_uiframe_button_release_event    (GtkWidget           *widget,
                                      GdkEventButton      *event)
{
  MetaUIFrame *frame;
  MetaGrabOp op;
  Display *display;
  
  frame = META_UIFRAME (widget);
  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  op = meta_core_get_grab_op (display);

  if (op == META_GRAB_OP_NONE)
    return FALSE;

  /* We only handle the releases we handled the presses for (things
   * involving frame controls). Window ops that don't require a
   * frame are handled in the Xlib part of the code, display.c/window.c
   */
  if (frame->xwindow == meta_core_get_grab_frame (display) &&
      ((int) event->button) == meta_core_get_grab_button (display))
    {
      MetaFrameControl control;

      control = get_control (frame, event->x, event->y);
      
      switch (op)
        {
        case META_GRAB_OP_CLICKING_MINIMIZE:
          if (control == META_FRAME_CONTROL_MINIMIZE)
            meta_core_minimize (display, frame->xwindow);
          
          meta_core_end_grab_op (display, event->time);
          break;

        case META_GRAB_OP_CLICKING_MAXIMIZE:
          if (control == META_FRAME_CONTROL_MAXIMIZE)
          {
            /* Focus the window on the maximize */
            meta_core_user_focus (display,
                            frame->xwindow,
                            event->time);      
            meta_core_maximize (display, frame->xwindow);
          }
          meta_core_end_grab_op (display, event->time);
          break;

        case META_GRAB_OP_CLICKING_UNMAXIMIZE:
          if (control == META_FRAME_CONTROL_UNMAXIMIZE)
            meta_core_unmaximize (display, frame->xwindow);
          
          meta_core_end_grab_op (display, event->time);
          break;
          
        case META_GRAB_OP_CLICKING_DELETE:
          if (control == META_FRAME_CONTROL_DELETE)
            meta_core_delete (display, frame->xwindow, event->time);
          
          meta_core_end_grab_op (display, event->time);
          break;
          
        case META_GRAB_OP_CLICKING_MENU:
          meta_core_end_grab_op (display, event->time);
          break;

        case META_GRAB_OP_CLICKING_SHADE:
          if (control == META_FRAME_CONTROL_SHADE)
            meta_core_shade (display, frame->xwindow, event->time);
          
          meta_core_end_grab_op (display, event->time);
          break;
 
        case META_GRAB_OP_CLICKING_UNSHADE:
          if (control == META_FRAME_CONTROL_UNSHADE)
            meta_core_unshade (display, frame->xwindow, event->time);

          meta_core_end_grab_op (display, event->time);
          break;

        case META_GRAB_OP_CLICKING_ABOVE:
          if (control == META_FRAME_CONTROL_ABOVE)
            meta_core_make_above (display, frame->xwindow);
          
          meta_core_end_grab_op (display, event->time);
          break;
 
        case META_GRAB_OP_CLICKING_UNABOVE:
          if (control == META_FRAME_CONTROL_UNABOVE)
            meta_core_unmake_above (display, frame->xwindow);

          meta_core_end_grab_op (display, event->time);
          break;

        case META_GRAB_OP_CLICKING_STICK:
          if (control == META_FRAME_CONTROL_STICK)
            meta_core_stick (display, frame->xwindow);

          meta_core_end_grab_op (display, event->time);
          break;
 
        case META_GRAB_OP_CLICKING_UNSTICK:
          if (control == META_FRAME_CONTROL_UNSTICK)
            meta_core_unstick (display, frame->xwindow);

          meta_core_end_grab_op (display, event->time);
          break;
          
        default:
          break;
        }
    }
  
  return TRUE;
}

static void
clip_to_screen (cairo_region_t *region,
                MetaUIFrame    *frame)
{
  cairo_rectangle_int_t frame_area;
  cairo_rectangle_int_t screen_area = { 0, 0, 0, 0 };
  cairo_region_t *tmp_region;
  
  /* Chop off stuff outside the screen; this optimization
   * is crucial to handle huge client windows,
   * like "xterm -geometry 1000x1000"
   */
  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_FRAME_X, &frame_area.x,
                 META_CORE_GET_FRAME_Y, &frame_area.y,
                 META_CORE_GET_FRAME_WIDTH, &frame_area.width,
                 META_CORE_GET_FRAME_HEIGHT, &frame_area.height,
                 META_CORE_GET_SCREEN_WIDTH, &screen_area.width,
                 META_CORE_GET_SCREEN_HEIGHT, &screen_area.height,
                 META_CORE_GET_END);

  cairo_region_translate (region, frame_area.x, frame_area.y);

  tmp_region = cairo_region_create_rectangle (&frame_area);
  cairo_region_intersect (region, tmp_region);
  cairo_region_destroy (tmp_region);

  tmp_region = cairo_region_create_rectangle (&screen_area);
  cairo_region_intersect (region, tmp_region);
  cairo_region_destroy (tmp_region);

  cairo_region_translate (region, - frame_area.x, - frame_area.y);
}

static void
subtract_client_area (cairo_region_t *region,
                      MetaUIFrame    *frame)
{
  cairo_rectangle_int_t area;
  MetaFrameBorders borders;
  cairo_region_t *tmp_region;
  Display *display;
  
  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_CLIENT_WIDTH, &area.width,
                 META_CORE_GET_CLIENT_HEIGHT, &area.height,
                 META_CORE_GET_END);

  meta_uiframe_get_frame_borders (frame, &borders);

  area.x = borders.total.left;
  area.y = borders.total.top;

  tmp_region = cairo_region_create_rectangle (&area);
  cairo_region_subtract (region, tmp_region);
  cairo_region_destroy (tmp_region);
}

void
meta_uiframe_paint (MetaUIFrame  *frame,
                    cairo_t      *cr)
{
  GtkWidget *widget = GTK_WIDGET (frame);
  GtkStyleContext *style_gtk = gtk_widget_get_style_context (widget);
  GdkRectangle visible_rect;
  MetaFrameBorders borders;

  meta_uiframe_get_frame_borders (frame, &borders);
  gtk_widget_get_allocation (widget, &visible_rect);

  visible_rect.x += borders.invisible.left;
  visible_rect.y += borders.invisible.top;
  visible_rect.width -= borders.invisible.left + borders.invisible.right;
  visible_rect.height -= borders.invisible.top - borders.invisible.bottom;

  sync_state_flags (frame);

  gtk_render_background (style_gtk, cr,
                         visible_rect.x,
                         visible_rect.y,
                         visible_rect.width,
                         visible_rect.height);

  gtk_render_frame (style_gtk, cr,
                    visible_rect.x,
                    visible_rect.y,
                    visible_rect.width,
                    visible_rect.height);

  /* We chain up to paint the contents here so that the mask
   * we paint with meta_frame_render_background can be accurate
   * with children. */
  GTK_WIDGET_CLASS (meta_uiframe_parent_class)->draw (widget, cr);
}

static gboolean
meta_uiframe_draw (GtkWidget *widget,
                   cairo_t   *cr)
{
  MetaUIFrame *frame;
  cairo_region_t *region;
  cairo_rectangle_int_t clip;

  frame = META_UIFRAME (widget);
  gdk_cairo_get_clip_rectangle (cr, &clip);

  region = cairo_region_create_rectangle (&clip);

  clip_to_screen (region, frame);
  subtract_client_area (region, frame);

  if (cairo_region_num_rectangles (region) == 0)
    goto out;

  gdk_cairo_region (cr, region);
  cairo_clip (cr);

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 1, 1, 1, 1);
  cairo_paint (cr);
  cairo_restore (cr);

  meta_uiframe_paint (frame, cr);

 out:
  cairo_region_destroy (region);

  return TRUE;
}

static void
meta_uiframe_style_updated (GtkWidget *widget)
{
  MetaUIFrame *frame = META_UIFRAME (widget);
  GtkWidget *container = frame->container;
  MetaFrameBorders borders;

  meta_uiframe_get_frame_borders (frame, &borders);

  gtk_widget_set_margin_left (container, borders.total.left);
  gtk_widget_set_margin_right (container, borders.total.right);
  gtk_widget_set_margin_top (container, borders.invisible.top);
  gtk_widget_set_size_request (container, -1, borders.visible.top);
}

static MetaFrameControl
get_control (MetaUIFrame *frame,
             int x, int y)
{
  MetaFrameBorders borders;
  MetaFrameFlags flags;
  MetaFrameType type;
  gboolean has_vert, has_horiz;
  gboolean has_north_resize;
  cairo_rectangle_int_t client;
  int width, height;

  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_CLIENT_WIDTH, &width,
                 META_CORE_GET_CLIENT_HEIGHT, &height,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_END);

  meta_uiframe_get_frame_borders (frame, &borders);
  get_client_rect (&borders, width, height, &client);

  if (POINT_IN_RECT (x, y, client))
    return META_FRAME_CONTROL_CLIENT_AREA;

  has_north_resize = (type != META_FRAME_TYPE_ATTACHED);
  has_vert = (flags & META_FRAME_ALLOWS_VERTICAL_RESIZE) != 0;
  has_horiz = (flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE) != 0;

  /* South resize always has priority over north resize,
   * in case of overlap.
   */

  if (y >= (height - borders.total.bottom) &&
      x >= (width - borders.total.right))
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_SE;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y >= (height - borders.total.bottom) &&
           x <= borders.total.left)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_SW;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (borders.invisible.top) &&
           x <= borders.total.left && has_north_resize)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_NW;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (borders.invisible.top) &&
           x >= width - borders.total.right && has_north_resize)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_NE;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y < borders.invisible.top)
    {
      if (has_vert && has_north_resize)
        return META_FRAME_CONTROL_RESIZE_N;
    }
  else if (y >= (height - borders.total.bottom))
    {
      if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
    }
  else if (x <= borders.total.left)
    {
      if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (x >= (width - borders.total.right))
    {
      if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }

  if (y >= borders.total.top)
    return META_FRAME_CONTROL_NONE;
  else
    return META_FRAME_CONTROL_TITLE;
}

static void
meta_uiframe_class_init (MetaUIFrameClass *class)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS (class);
  widget_class = GTK_WIDGET_CLASS (class);

  gobject_class->finalize = meta_uiframe_finalize;

  widget_class->draw = meta_uiframe_draw;
  widget_class->style_updated = meta_uiframe_style_updated;
  widget_class->button_press_event = meta_uiframe_button_press_event;
  widget_class->button_release_event = meta_uiframe_button_release_event;
}
