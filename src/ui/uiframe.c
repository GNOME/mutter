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
#include <meta/theme.h>
#include <meta/prefs.h>
#include "ui.h"

#include "theme-private.h"

#ifdef HAVE_SHAPE
#include <X11/extensions/shape.h>
#endif

static void meta_uiframe_update_prelit_control (MetaUIFrame     *frame,
                                                MetaFrameControl control);

static GdkRectangle*    control_rect (MetaFrameControl   control,
                                      MetaFrameGeometry *fgeom);
static MetaFrameControl get_control  (MetaUIFrame       *frame,
                                      int                x,
                                      int                y);

G_DEFINE_TYPE (MetaUIFrame, meta_uiframe, GTK_TYPE_WINDOW);

static void
meta_uiframe_finalize (GObject *obj)
{
  MetaUIFrame *frame = META_UIFRAME (obj);

  if (frame->window)
    g_object_unref (frame->window);

  if (frame->layout)
    g_object_unref (G_OBJECT (frame->layout));

  if (frame->title)
    g_free (frame->title);
}

static void
meta_uiframe_init (MetaUIFrame *frame)
{
}

/* In order to use a style with a window it has to be attached to that
 * window. Actually, the colormaps just have to match, but since GTK+
 * already takes care of making sure that its cheap to attach a style
 * to multiple windows with the same colormap, we can just go ahead
 * and attach separately for each window.
 */
void
meta_uiframe_attach_style (MetaUIFrame *frame)
{
  char *variant = NULL;

  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_THEME_VARIANT, &variant,
                 META_CORE_GET_END);

  frame->tv = meta_theme_get_variant (meta_theme_get_current (),
                                      variant);
}

void
meta_uiframe_ensure_layout (MetaUIFrame *frame)
{
  MetaFrameFlags flags;
  MetaFrameType type;

  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_END);
  
  if (frame->layout == NULL)
    {
      frame->layout = gtk_widget_create_pango_layout (GTK_WIDGET (frame), frame->title);

      pango_layout_set_ellipsize (frame->layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_auto_dir (frame->layout, FALSE);

      /* Save some RAM */
      g_free (frame->title);
      frame->title = NULL;
    }
}

void
meta_uiframe_calc_geometry (MetaUIFrame       *frame,
                            MetaFrameGeometry *fgeom)
{
  int width, height;
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaButtonLayout button_layout;
  
  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), frame->xwindow,
                 META_CORE_GET_CLIENT_WIDTH, &width,
                 META_CORE_GET_CLIENT_HEIGHT, &height,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_END);

  meta_uiframe_ensure_layout (frame);

  meta_prefs_get_button_layout (&button_layout);
  
  meta_theme_calc_geometry (frame->tv->theme,
                            frame->tv->style_context,
                            type,
                            flags,
                            width, height,
                            &button_layout,
                            fgeom);
}

/* The client rectangle surrounds client window; it subtracts both
 * the visible and invisible borders from the frame window's size.
 */
static void
get_client_rect (MetaFrameGeometry     *fgeom,
                 int                    window_width,
                 int                    window_height,
                 cairo_rectangle_int_t *rect)
{
  rect->x = fgeom->borders.total.left;
  rect->y = fgeom->borders.total.top;
  rect->width = window_width - fgeom->borders.total.right - rect->x;
  rect->height = window_height - fgeom->borders.total.bottom - rect->y;
}

void
meta_uiframe_set_title (MetaUIFrame *frame,
                        const char  *title)
{
  g_free (frame->title);
  frame->title = g_strdup (title);
  
  if (frame->layout)
    {
      g_object_unref (frame->layout);
      frame->layout = NULL;
    }

  gtk_widget_queue_draw (GTK_WIDGET (frame));
}

static void
redraw_control (MetaUIFrame *frame,
                MetaFrameControl control)
{
  MetaFrameGeometry fgeom;
  GdkRectangle *rect;
  
  meta_uiframe_calc_geometry (frame, &fgeom);

  rect = control_rect (control, &fgeom);

  gdk_window_invalidate_rect (frame->window, rect, FALSE);
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
      
      frame->prelit_control = control;
      redraw_control (frame, control);

      if (op == META_GRAB_OP_CLICKING_MENU)
        {
          MetaFrameGeometry fgeom;
          GdkRectangle *rect;
          int dx, dy;
          
          meta_uiframe_calc_geometry (frame, &fgeom);
          
          rect = control_rect (META_FRAME_CONTROL_MENU, &fgeom);

          /* get delta to convert to root coords */
          dx = event->x_root - event->x;
          dy = event->y_root - event->y;
          
          /* Align to the right end of the menu rectangle if RTL */
          if (meta_ui_get_direction() == META_UI_DIRECTION_RTL)
            dx += rect->width;

          meta_core_show_window_menu (display,
                                      frame->xwindow,
                                      rect->x + dx,
                                      rect->y + rect->height + dy,
                                      event->button,
                                      event->time);
        }
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

      /* Update the prelit control regardless of what button the mouse
       * was released over; needed so that the new button can become
       * prelit so to let the user know that it can now be pressed.
       * :)
       */
      meta_uiframe_update_prelit_control (frame, control);
    }
  
  return TRUE;
}

static void
meta_uiframe_update_prelit_control (MetaUIFrame     *frame,
                                    MetaFrameControl control)
{
  MetaFrameControl old_control;
  MetaCursor cursor;


  meta_verbose ("Updating prelit control from %u to %u\n",
                frame->prelit_control, control);
  
  cursor = META_CURSOR_DEFAULT;
  
  switch (control)
    {
    case META_FRAME_CONTROL_CLIENT_AREA:
      break;
    case META_FRAME_CONTROL_NONE:
      break;
    case META_FRAME_CONTROL_TITLE:
      break;
    case META_FRAME_CONTROL_DELETE:
      break;
    case META_FRAME_CONTROL_MENU:
      break;
    case META_FRAME_CONTROL_MINIMIZE:
      break;
    case META_FRAME_CONTROL_MAXIMIZE:
      break;
    case META_FRAME_CONTROL_UNMAXIMIZE:
      break;
    case META_FRAME_CONTROL_SHADE:
      break;
    case META_FRAME_CONTROL_UNSHADE:
      break;
    case META_FRAME_CONTROL_ABOVE:
      break;
    case META_FRAME_CONTROL_UNABOVE:
      break;
    case META_FRAME_CONTROL_STICK:
      break;
    case META_FRAME_CONTROL_UNSTICK:
      break;
    case META_FRAME_CONTROL_RESIZE_SE:
      cursor = META_CURSOR_SE_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_S:
      cursor = META_CURSOR_SOUTH_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_SW:
      cursor = META_CURSOR_SW_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_N:
      cursor = META_CURSOR_NORTH_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_NE:
      cursor = META_CURSOR_NE_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_NW:
      cursor = META_CURSOR_NW_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_W:
      cursor = META_CURSOR_WEST_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_E:
      cursor = META_CURSOR_EAST_RESIZE;
      break;
    }        

  /* set/unset the prelight cursor */
  meta_core_set_screen_cursor (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                               frame->xwindow,
                               cursor);  

  switch (control)
    {
    case META_FRAME_CONTROL_MENU:
    case META_FRAME_CONTROL_MINIMIZE:
    case META_FRAME_CONTROL_MAXIMIZE:
    case META_FRAME_CONTROL_DELETE:
    case META_FRAME_CONTROL_SHADE:
    case META_FRAME_CONTROL_UNSHADE:
    case META_FRAME_CONTROL_ABOVE:
    case META_FRAME_CONTROL_UNABOVE:
    case META_FRAME_CONTROL_STICK:
    case META_FRAME_CONTROL_UNSTICK:
    case META_FRAME_CONTROL_UNMAXIMIZE:
      /* leave control set */
      break;
    default:
      /* Only prelight buttons */
      control = META_FRAME_CONTROL_NONE;
      break;
    }      

  if (control == frame->prelit_control)
    return;

  /* Save the old control so we can unprelight it */
  old_control = frame->prelit_control;

  frame->prelit_control = control;

  redraw_control (frame, old_control);
  redraw_control (frame, control);
}

static gboolean
meta_uiframe_motion_notify_event     (GtkWidget           *widget,
                                      GdkEventMotion      *event)
{
  MetaUIFrame *frame;
  MetaGrabOp grab_op;
  Display *display;
  
  frame = META_UIFRAME (widget);
  display = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (event->window));

  grab_op = meta_core_get_grab_op (display);
  
  switch (grab_op)
    {
    case META_GRAB_OP_CLICKING_MENU:
    case META_GRAB_OP_CLICKING_DELETE:
    case META_GRAB_OP_CLICKING_MINIMIZE:
    case META_GRAB_OP_CLICKING_MAXIMIZE:
    case META_GRAB_OP_CLICKING_UNMAXIMIZE:
    case META_GRAB_OP_CLICKING_SHADE:
    case META_GRAB_OP_CLICKING_UNSHADE:
    case META_GRAB_OP_CLICKING_ABOVE:
    case META_GRAB_OP_CLICKING_UNABOVE:
    case META_GRAB_OP_CLICKING_STICK:
    case META_GRAB_OP_CLICKING_UNSTICK:
      {
        MetaFrameControl control;
        int x, y;
        
        gdk_window_get_device_position (frame->window, event->device,
                                        &x, &y, NULL);

        /* Control is set to none unless it matches
         * the current grab
         */
        control = get_control (frame, x, y);
        if (! ((control == META_FRAME_CONTROL_MENU &&
                grab_op == META_GRAB_OP_CLICKING_MENU) ||
               (control == META_FRAME_CONTROL_DELETE &&
                grab_op == META_GRAB_OP_CLICKING_DELETE) ||
               (control == META_FRAME_CONTROL_MINIMIZE &&
                grab_op == META_GRAB_OP_CLICKING_MINIMIZE) ||
               ((control == META_FRAME_CONTROL_MAXIMIZE ||
                 control == META_FRAME_CONTROL_UNMAXIMIZE) &&
                (grab_op == META_GRAB_OP_CLICKING_MAXIMIZE ||
                 grab_op == META_GRAB_OP_CLICKING_UNMAXIMIZE)) ||
               (control == META_FRAME_CONTROL_SHADE &&
                grab_op == META_GRAB_OP_CLICKING_SHADE) ||
               (control == META_FRAME_CONTROL_UNSHADE &&
                grab_op == META_GRAB_OP_CLICKING_UNSHADE) ||
               (control == META_FRAME_CONTROL_ABOVE &&
                grab_op == META_GRAB_OP_CLICKING_ABOVE) ||
               (control == META_FRAME_CONTROL_UNABOVE &&
                grab_op == META_GRAB_OP_CLICKING_UNABOVE) ||
               (control == META_FRAME_CONTROL_STICK &&
                grab_op == META_GRAB_OP_CLICKING_STICK) ||
               (control == META_FRAME_CONTROL_UNSTICK &&
                grab_op == META_GRAB_OP_CLICKING_UNSTICK)))
           control = META_FRAME_CONTROL_NONE;
        
        /* Update prelit control and cursor */
        meta_uiframe_update_prelit_control (frame, control);
      }
      break;
    case META_GRAB_OP_NONE:
      {
        MetaFrameControl control;
        int x, y;
        
        gdk_window_get_device_position (frame->window, event->device,
                                        &x, &y, NULL);

        control = get_control (frame, x, y);

        /* Update prelit control and cursor */
        meta_uiframe_update_prelit_control (frame, control);
      }
      break;

    default:
      break;
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
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaFrameBorders borders;
  cairo_region_t *tmp_region;
  Display *display;
  
  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_CLIENT_WIDTH, &area.width,
                 META_CORE_GET_CLIENT_HEIGHT, &area.height,
                 META_CORE_GET_END);
  meta_theme_get_frame_borders (frame->tv->theme,
                                frame->tv->style_context,
                                type, flags,
                                &borders);

  area.x = borders.total.left;
  area.y = borders.total.top;

  tmp_region = cairo_region_create_rectangle (&area);
  cairo_region_subtract (region, tmp_region);
  cairo_region_destroy (tmp_region);
}

static void
meta_uiframe_paint (MetaUIFrame  *frame,
                    cairo_t      *cr)
{
  MetaFrameFlags flags;
  MetaFrameType type;
  GdkPixbuf *mini_icon;
  GdkPixbuf *icon;
  int w, h;
  MetaButtonState button_states[META_BUTTON_TYPE_LAST];
  Window grab_frame;
  int i;
  MetaButtonLayout button_layout;
  MetaGrabOp grab_op;
  Display *display;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  for (i = 0; i < META_BUTTON_TYPE_LAST; i++)
    button_states[i] = META_BUTTON_STATE_NORMAL;

  grab_frame = meta_core_get_grab_frame (display);
  grab_op = meta_core_get_grab_op (display);
  if (grab_frame != frame->xwindow)
    grab_op = META_GRAB_OP_NONE;
  
  /* Set prelight state */
  switch (frame->prelit_control)
    {
    case META_FRAME_CONTROL_MENU:
      if (grab_op == META_GRAB_OP_CLICKING_MENU)
        button_states[META_BUTTON_TYPE_MENU] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_MENU] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_MINIMIZE:
      if (grab_op == META_GRAB_OP_CLICKING_MINIMIZE)
        button_states[META_BUTTON_TYPE_MINIMIZE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_MINIMIZE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_MAXIMIZE:
      if (grab_op == META_GRAB_OP_CLICKING_MAXIMIZE)
        button_states[META_BUTTON_TYPE_MAXIMIZE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_MAXIMIZE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_UNMAXIMIZE:
      if (grab_op == META_GRAB_OP_CLICKING_UNMAXIMIZE)
        button_states[META_BUTTON_TYPE_MAXIMIZE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_MAXIMIZE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_SHADE:
      if (grab_op == META_GRAB_OP_CLICKING_SHADE)
        button_states[META_BUTTON_TYPE_SHADE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_SHADE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_UNSHADE:
      if (grab_op == META_GRAB_OP_CLICKING_UNSHADE)
        button_states[META_BUTTON_TYPE_UNSHADE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_UNSHADE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_ABOVE:
      if (grab_op == META_GRAB_OP_CLICKING_ABOVE)
        button_states[META_BUTTON_TYPE_ABOVE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_ABOVE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_UNABOVE:
      if (grab_op == META_GRAB_OP_CLICKING_UNABOVE)
        button_states[META_BUTTON_TYPE_UNABOVE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_UNABOVE] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_STICK:
      if (grab_op == META_GRAB_OP_CLICKING_STICK)
        button_states[META_BUTTON_TYPE_STICK] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_STICK] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_UNSTICK:
      if (grab_op == META_GRAB_OP_CLICKING_UNSTICK)
        button_states[META_BUTTON_TYPE_UNSTICK] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_UNSTICK] = META_BUTTON_STATE_PRELIGHT;
      break;
    case META_FRAME_CONTROL_DELETE:
      if (grab_op == META_GRAB_OP_CLICKING_DELETE)
        button_states[META_BUTTON_TYPE_CLOSE] = META_BUTTON_STATE_PRESSED;
      else
        button_states[META_BUTTON_TYPE_CLOSE] = META_BUTTON_STATE_PRELIGHT;
      break;
    default:
      break;
    }
  
  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_MINI_ICON, &mini_icon,
                 META_CORE_GET_ICON, &icon,
                 META_CORE_GET_CLIENT_WIDTH, &w,
                 META_CORE_GET_CLIENT_HEIGHT, &h,
                 META_CORE_GET_END);

  meta_uiframe_ensure_layout (frame);

  meta_prefs_get_button_layout (&button_layout);

  meta_theme_draw_frame_with_style (frame->tv->theme,
                                    frame->tv->style_context,
                                    cr,
                                    type,
                                    flags,
                                    w, h,
                                    frame->layout,
                                    &button_layout,
                                    button_states,
                                    mini_icon, icon);
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

  GTK_WIDGET_CLASS (meta_uiframe_parent_class)->draw (widget, cr);

 out:
  cairo_region_destroy (region);
  
  return TRUE;
}

static gboolean
meta_uiframe_enter_notify_event      (GtkWidget           *widget,
                                      GdkEventCrossing    *event)
{
  MetaUIFrame *frame;
  MetaFrameControl control;
  
  frame = META_UIFRAME (widget);

  control = get_control (frame, event->x, event->y);
  meta_uiframe_update_prelit_control (frame, control);
  
  return TRUE;
}

static gboolean
meta_uiframe_leave_notify_event      (GtkWidget           *widget,
                                      GdkEventCrossing    *event)
{
  MetaUIFrame *frame = META_UIFRAME (widget);

  meta_uiframe_update_prelit_control (frame, META_FRAME_CONTROL_NONE);
  
  return TRUE;
}

static GdkRectangle*
control_rect (MetaFrameControl control,
              MetaFrameGeometry *fgeom)
{
  GdkRectangle *rect;
  
  rect = NULL;
  switch (control)
    {
    case META_FRAME_CONTROL_TITLE:
      rect = &fgeom->title_rect;
      break;
    case META_FRAME_CONTROL_DELETE:
      rect = &fgeom->close_rect.visible;
      break;
    case META_FRAME_CONTROL_MENU:
      rect = &fgeom->menu_rect.visible;
      break;
    case META_FRAME_CONTROL_MINIMIZE:
      rect = &fgeom->min_rect.visible;
      break;
    case META_FRAME_CONTROL_MAXIMIZE:
    case META_FRAME_CONTROL_UNMAXIMIZE:
      rect = &fgeom->max_rect.visible;
      break;
    case META_FRAME_CONTROL_SHADE:
      rect = &fgeom->shade_rect.visible;
      break;
    case META_FRAME_CONTROL_UNSHADE:
      rect = &fgeom->unshade_rect.visible;
      break;
    case META_FRAME_CONTROL_ABOVE:
      rect = &fgeom->above_rect.visible;
      break;
    case META_FRAME_CONTROL_UNABOVE:
      rect = &fgeom->unabove_rect.visible;
      break;
    case META_FRAME_CONTROL_STICK:
      rect = &fgeom->stick_rect.visible;
      break;
    case META_FRAME_CONTROL_UNSTICK:
      rect = &fgeom->unstick_rect.visible;
      break;
    case META_FRAME_CONTROL_RESIZE_SE:
      break;
    case META_FRAME_CONTROL_RESIZE_S:
      break;
    case META_FRAME_CONTROL_RESIZE_SW:
      break;
    case META_FRAME_CONTROL_RESIZE_N:
      break;
    case META_FRAME_CONTROL_RESIZE_NE:
      break;
    case META_FRAME_CONTROL_RESIZE_NW:
      break;
    case META_FRAME_CONTROL_RESIZE_W:
      break;
    case META_FRAME_CONTROL_RESIZE_E:
      break;
    case META_FRAME_CONTROL_NONE:
      break;
    case META_FRAME_CONTROL_CLIENT_AREA:
      break;
    }

  return rect;
}

#define TOP_RESIZE_HEIGHT 4
static MetaFrameControl
get_control (MetaUIFrame *frame,
             int x, int y)
{
  MetaFrameGeometry fgeom;
  MetaFrameFlags flags;
  MetaFrameType type;
  gboolean has_vert, has_horiz;
  gboolean has_north_resize;
  cairo_rectangle_int_t client;

  meta_uiframe_calc_geometry (frame, &fgeom);
  get_client_rect (&fgeom, fgeom.width, fgeom.height, &client);

  if (POINT_IN_RECT (x, y, client))
    return META_FRAME_CONTROL_CLIENT_AREA;
  
  if (POINT_IN_RECT (x, y, fgeom.close_rect.clickable))
    return META_FRAME_CONTROL_DELETE;

  if (POINT_IN_RECT (x, y, fgeom.min_rect.clickable))
    return META_FRAME_CONTROL_MINIMIZE;

  if (POINT_IN_RECT (x, y, fgeom.menu_rect.clickable))
    return META_FRAME_CONTROL_MENU;

  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_END);

  has_north_resize = (type != META_FRAME_TYPE_ATTACHED);
  has_vert = (flags & META_FRAME_ALLOWS_VERTICAL_RESIZE) != 0;
  has_horiz = (flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE) != 0;

  if (POINT_IN_RECT (x, y, fgeom.title_rect))
    {
      if (has_vert && y <= TOP_RESIZE_HEIGHT && has_north_resize)
        return META_FRAME_CONTROL_RESIZE_N;
      else
        return META_FRAME_CONTROL_TITLE;
    }

  if (POINT_IN_RECT (x, y, fgeom.max_rect.clickable))
    {
      if (flags & META_FRAME_MAXIMIZED)
        return META_FRAME_CONTROL_UNMAXIMIZE;
      else
        return META_FRAME_CONTROL_MAXIMIZE;
    }
      
  if (POINT_IN_RECT (x, y, fgeom.shade_rect.clickable))
    {
      return META_FRAME_CONTROL_SHADE;
    }

  if (POINT_IN_RECT (x, y, fgeom.unshade_rect.clickable))
    {
      return META_FRAME_CONTROL_UNSHADE;
    }

  if (POINT_IN_RECT (x, y, fgeom.above_rect.clickable))
    {
      return META_FRAME_CONTROL_ABOVE;
    }

  if (POINT_IN_RECT (x, y, fgeom.unabove_rect.clickable))
    {
      return META_FRAME_CONTROL_UNABOVE;
    }

  if (POINT_IN_RECT (x, y, fgeom.stick_rect.clickable))
    {
      return META_FRAME_CONTROL_STICK;
    }

  if (POINT_IN_RECT (x, y, fgeom.unstick_rect.clickable))
    {
      return META_FRAME_CONTROL_UNSTICK;
    }

  /* South resize always has priority over north resize,
   * in case of overlap.
   */

  if (y >= (fgeom.height - fgeom.borders.total.bottom) &&
      x >= (fgeom.width - fgeom.borders.total.right))
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_SE;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y >= (fgeom.height - fgeom.borders.total.bottom) &&
           x <= fgeom.borders.total.left)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_SW;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (fgeom.borders.invisible.top) &&
           x <= fgeom.borders.total.left && has_north_resize)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_NW;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (fgeom.borders.invisible.top) &&
           x >= fgeom.width - fgeom.borders.total.right && has_north_resize)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_NE;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y < (fgeom.borders.invisible.top + TOP_RESIZE_HEIGHT))
    {
      if (has_vert && has_north_resize)
        return META_FRAME_CONTROL_RESIZE_N;
    }
  else if (y >= (fgeom.height - fgeom.borders.total.bottom))
    {
      if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
    }
  else if (x <= fgeom.borders.total.left)
    {
      if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (x >= (fgeom.width - fgeom.borders.total.right))
    {
      if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }

  if (y >= fgeom.borders.total.top)
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
  widget_class->button_press_event = meta_uiframe_button_press_event;
  widget_class->button_release_event = meta_uiframe_button_release_event;
  widget_class->motion_notify_event = meta_uiframe_motion_notify_event;
  widget_class->enter_notify_event = meta_uiframe_enter_notify_event;
  widget_class->leave_notify_event = meta_uiframe_leave_notify_event;
}
