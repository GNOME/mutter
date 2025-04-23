/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * #MetaWindow property handling
 *
 * A system which can inspect sets of properties of given windows
 * and take appropriate action given their values.
 *
 * Note that all the meta_window_reload_property* functions require a
 * round trip to the server.
 *
 * The guts of this system are in meta_display_init_window_prop_hooks().
 * Reading this function will give you insight into how this all fits
 * together.
 */

/*
 * Copyright (C) 2001, 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2004, 2005 Elijah Newren
 * Copyright (C) 2009 Thomas Thurman
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

#define _XOPEN_SOURCE 600 /* for gethostname() */

#include "config.h"

#include "x11/window-props.h"

#include <X11/Xatom.h>
#include <unistd.h>
#include <string.h>

#include "compositor/compositor-private.h"
#include "core/meta-window-config-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/util-private.h"
#include "meta/meta-x11-group.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/meta-x11-frame.h"
#include "x11/window-x11-private.h"
#include "x11/window-x11.h"
#include "x11/xprops.h"

#ifndef HOST_NAME_MAX
/* Solaris headers apparently don't define this so do so manually; #326745 */
#define HOST_NAME_MAX 255
#endif

typedef void (* ReloadValueFunc) (MetaWindow    *window,
                                  MetaPropValue *value,
                                  gboolean       initial);

typedef enum
{
  NONE       = 0,
  LOAD_INIT  = (1 << 0),
  INCLUDE_OR = (1 << 1),
  INIT_ONLY  = (1 << 2),
  FORCE_INIT = (1 << 3),
} MetaPropHookFlags;

struct _MetaWindowPropHooks
{
  Atom property;
  MetaPropValueType type;
  ReloadValueFunc reload_func;
  MetaPropHookFlags flags;
};

static void init_prop_value            (MetaWindow          *window,
                                        MetaWindowPropHooks *hooks,
                                        MetaPropValue       *value);
static void reload_prop_value          (MetaWindow          *window,
                                        MetaWindowPropHooks *hooks,
                                        MetaPropValue       *value,
                                        gboolean             initial);
static MetaWindowPropHooks *find_hooks (MetaX11Display *x11_display,
                                        Atom            property);


void
meta_window_reload_property_from_xwindow (MetaWindow      *window,
                                          Window           xwindow,
                                          Atom             property,
                                          gboolean         initial)
{
  MetaPropValue value = { 0, };
  MetaWindowPropHooks *hooks;

  hooks = find_hooks (window->display->x11_display, property);
  if (!hooks)
    return;

  if ((hooks->flags & INIT_ONLY) && !initial)
    return;

  init_prop_value (window, hooks, &value);

  meta_prop_get_values (window->display->x11_display, xwindow,
                        &value, 1);

  reload_prop_value (window, hooks, &value,
                     initial);

  meta_prop_free_values (&value, 1);
}

static void
meta_window_reload_property (MetaWindow      *window,
                             Atom             property,
                             gboolean         initial)
{
  meta_window_reload_property_from_xwindow (window,
                                            meta_window_x11_get_xwindow (window),
                                            property,
                                            initial);
}

void
meta_window_load_initial_properties (MetaWindow *window)
{
  int i, j;
  MetaPropValue *values;
  int n_properties = 0;
  MetaX11Display *x11_display = window->display->x11_display;

  values = g_new0 (MetaPropValue, x11_display->n_prop_hooks);

  j = 0;
  for (i = 0; i < x11_display->n_prop_hooks; i++)
    {
      MetaWindowPropHooks *hooks = &x11_display->prop_hooks_table[i];
      if (hooks->flags & LOAD_INIT)
        {
          init_prop_value (window, hooks, &values[j]);
          ++j;
        }
    }
  n_properties = j;

  meta_prop_get_values (window->display->x11_display,
                        meta_window_x11_get_xwindow (window),
                        values, n_properties);

  j = 0;
  for (i = 0; i < x11_display->n_prop_hooks; i++)
    {
      MetaWindowPropHooks *hooks = &x11_display->prop_hooks_table[i];
      if (hooks->flags & LOAD_INIT)
        {
          /* If we didn't actually manage to load anything then we don't need
           * to call the reload function; this is different from a notification
           * where disappearance of a previously present value is significant.
           */
          if (values[j].type != META_PROP_VALUE_INVALID ||
              hooks->flags & FORCE_INIT)
            reload_prop_value (window, hooks, &values[j], TRUE);
          ++j;
        }
    }

  meta_prop_free_values (values, n_properties);

  g_free (values);
}

/* Fill in the MetaPropValue used to get the value of "property" */
static void
init_prop_value (MetaWindow          *window,
                 MetaWindowPropHooks *hooks,
                 MetaPropValue       *value)
{
  if (!hooks || hooks->type == META_PROP_VALUE_INVALID ||
      (window->override_redirect && !(hooks->flags & INCLUDE_OR)))
    {
      value->type = META_PROP_VALUE_INVALID;
      value->atom = None;
    }
  else
    {
      value->type = hooks->type;
      value->atom = hooks->property;
    }
}

static void
reload_prop_value (MetaWindow          *window,
                   MetaWindowPropHooks *hooks,
                   MetaPropValue       *value,
                   gboolean             initial)
{
  if (!(window->override_redirect && !(hooks->flags & INCLUDE_OR)))
    (* hooks->reload_func) (window, value, initial);
}

static void
reload_wm_client_machine (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));

  g_clear_pointer (&priv->wm_client_machine, g_free);

  if (value->type != META_PROP_VALUE_INVALID)
    priv->wm_client_machine = g_strdup (value->v.str);

  meta_topic (META_DEBUG_X11,
              "Window has client machine \"%s\"",
              priv->wm_client_machine ? priv->wm_client_machine : "unset");

  if (priv->wm_client_machine == NULL)
    {
      window->is_remote = FALSE;
    }
  else
    {
      char hostname[HOST_NAME_MAX + 1] = "";

      gethostname (hostname, HOST_NAME_MAX + 1);

      window->is_remote = g_strcmp0 (priv->wm_client_machine, hostname) != 0;
    }
}

static void
complain_about_broken_client (MetaWindow    *window,
                              MetaPropValue *value,
                              gboolean       initial)
{
  g_warning ("Window %s changed client leader window or SM client ID",
             window->desc);
}

static void
reload_net_wm_window_type (MetaWindow    *window,
                           MetaPropValue *value,
                           gboolean       initial)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);

  if (value->type != META_PROP_VALUE_INVALID)
    {
      int i;

      for (i = 0; i < value->v.atom_list.n_atoms; i++)
        {
          Atom atom = value->v.atom_list.atoms[i];

          /* We break as soon as we find one we recognize,
           * supposed to prefer those near the front of the list
           */
          if (atom == x11_display->atom__NET_WM_WINDOW_TYPE_DESKTOP ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DOCK ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLBAR ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_MENU ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_UTILITY ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_SPLASH ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DIALOG ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DROPDOWN_MENU ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_POPUP_MENU ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLTIP ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_NOTIFICATION ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_COMBO ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DND ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_NORMAL)
            {
              priv->type_atom = atom;
              break;
            }
        }
    }

  meta_window_x11_recalc_window_type (window);
}

static void
reload_icon_geometry (MetaWindow    *window,
                      MetaPropValue *value,
                      gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      if (value->v.cardinal_list.n_cardinals != 4)
        {
          meta_topic (META_DEBUG_X11,
                      "_NET_WM_ICON_GEOMETRY on %s has %d values instead of 4",
                      window->desc, value->v.cardinal_list.n_cardinals);
        }
      else
        {
          MtkRectangle geometry;

          geometry = MTK_RECTANGLE_INIT (value->v.cardinal_list.cardinals[0],
                                         value->v.cardinal_list.cardinals[1],
                                         value->v.cardinal_list.cardinals[2],
                                         value->v.cardinal_list.cardinals[3]);
          meta_window_protocol_to_stage_rect (window, &geometry, &geometry);

          meta_window_set_icon_geometry (window, &geometry);
        }
    }
  else
    {
      meta_window_set_icon_geometry (window, NULL);
    }
}

static void
meta_window_set_custom_frame_extents (MetaWindow      *window,
                                      MetaFrameBorder *extents,
                                      gboolean         is_initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (window_x11);
  if (extents)
    {
      if (priv->has_custom_frame_extents &&
          memcmp (&window->custom_frame_extents, extents, sizeof (MetaFrameBorder)) == 0)
        return;

      priv->has_custom_frame_extents = TRUE;
      window->custom_frame_extents = *extents;

      /* If we're setting the frame extents on map, then this is telling
       * us to adjust our understanding of the frame rect to match what
       * GTK+ thinks it is. Future changes to the frame extents should
       * trigger a resize and send a ConfigureRequest to the application.
       */
      if (is_initial)
        {
          MtkRectangle frame_rect;

          frame_rect = meta_window_config_get_rect (window->config);
          meta_window_client_rect_to_frame_rect (window, &frame_rect, &frame_rect);
          meta_window_config_set_rect (window->config, frame_rect);
          meta_window_client_rect_to_frame_rect (window, &window->unconstrained_rect, &window->unconstrained_rect);
        }
    }
  else
    {
      if (!priv->has_custom_frame_extents)
        return;

      priv->has_custom_frame_extents = FALSE;
      memset (&window->custom_frame_extents, 0, sizeof (window->custom_frame_extents));
    }

  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}

static void
reload_gtk_frame_extents (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      if (value->v.cardinal_list.n_cardinals != 4)
        {
          meta_topic (META_DEBUG_X11,
                      "_GTK_FRAME_EXTENTS on %s has %d values instead of 4",
                      window->desc, value->v.cardinal_list.n_cardinals);
        }
      else
        {
          int left, right, top, bottom;
          MetaFrameBorder extents;

          meta_window_protocol_to_stage_point (window,
                                               value->v.cardinal_list.cardinals[0],
                                               value->v.cardinal_list.cardinals[1],
                                               &left,
                                               &right,
                                               MTK_ROUNDING_STRATEGY_GROW);
          meta_window_protocol_to_stage_point (window,
                                               value->v.cardinal_list.cardinals[2],
                                               value->v.cardinal_list.cardinals[3],
                                               &top,
                                               &bottom,
                                               MTK_ROUNDING_STRATEGY_GROW);

          extents.left = left;
          extents.right = right;
          extents.top = top;
          extents.bottom = bottom;

          meta_window_set_custom_frame_extents (window, &extents, initial);
        }
    }
  else
    {
      meta_window_set_custom_frame_extents (window, NULL, initial);
    }
}

static void
reload_struts (MetaWindow    *window,
               MetaPropValue *value,
               gboolean       initial)
{
  meta_window_update_struts (window);
}

static void
reload_toplevel_tag (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  meta_window_set_tag (window, value->v.str);
}

static void
reload_wm_window_role (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  g_clear_pointer (&window->role, g_free);
  if (value->type != META_PROP_VALUE_INVALID)
    window->role = g_strdup (value->v.str);
}

static void
reload_net_wm_user_time (MetaWindow    *window,
                         MetaPropValue *value,
                         gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      uint32_t cardinal = value->v.cardinal;
      meta_window_set_user_time (window, cardinal);
    }
}

static void
reload_net_wm_user_time_window (MetaWindow    *window,
                                MetaPropValue *value,
                                gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
      MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);
      MetaWindow *prev_owner;
      MetaWindowX11Private *prev_owner_priv;

      /* Unregister old NET_WM_USER_TIME_WINDOW */
      if (priv->user_time_window != None)
        {
          /* See the comment to the meta_display_register_x_window call below. */
          meta_x11_display_unregister_x_window (window->display->x11_display,
                                                priv->user_time_window);
          /* Don't get events on not-managed windows */
          XSelectInput (window->display->x11_display->xdisplay,
                        priv->user_time_window,
                        NoEventMask);
        }

      /* Ensure the new user time window is not used on another MetaWindow,
       * and unset its user time window if that is the case.
       */
      prev_owner = meta_x11_display_lookup_x_window (window->display->x11_display,
                                                     value->v.xwindow);
      prev_owner_priv = meta_window_x11_get_private (META_WINDOW_X11 (prev_owner));
      if (prev_owner && prev_owner_priv->user_time_window == value->v.xwindow)
        {
          meta_x11_display_unregister_x_window (window->display->x11_display,
                                                value->v.xwindow);
          prev_owner_priv->user_time_window = None;
        }

      /* Obtain the new NET_WM_USER_TIME_WINDOW and register it */
      priv->user_time_window = value->v.xwindow;
      if (priv->user_time_window != None)
        {
          /* Kind of a hack; display.c:event_callback() ignores events
           * for unknown windows.  We make window->user_time_window
           * known by registering it with window (despite the fact
           * that window->xwindow is already registered with window).
           * This basically means that property notifies to either the
           * window->user_time_window or window->xwindow will be
           * treated identically and will result in functions for
           * window being called to update it.  Maybe we should ignore
           * any property notifies to window->user_time_window other
           * than atom__NET_WM_USER_TIME ones, but I just don't care
           * and it's not specified in the spec anyway.
           */
          meta_x11_display_register_x_window (window->display->x11_display,
                                              &priv->user_time_window,
                                              window);
          /* Just listen for property notify events */
          XSelectInput (window->display->x11_display->xdisplay,
                        priv->user_time_window,
                        PropertyChangeMask);

          /* Manually load the _NET_WM_USER_TIME field from the given window
           * at this time as well.  If the user_time_window ever broadens in
           * scope, we'll probably want to load all relevant properties here.
           */
          meta_window_reload_property_from_xwindow (
            window,
            priv->user_time_window,
            window->display->x11_display->atom__NET_WM_USER_TIME,
            initial);
        }
    }
}

#define MAX_TITLE_LENGTH 512

/**
 * set_title_text:
 *
 * Called by set_window_title() to set the value of @target to @title.
 * If required and @atom is set, it will update the appropriate property.
 *
 * Returns: %TRUE if a new title was set.
 */
static gboolean
set_title_text (MetaWindow  *window,
                gboolean     previous_was_modified,
                const char  *title,
                Atom         atom,
                char       **target)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));
  gboolean modified = FALSE;

  if (!target)
    return FALSE;

  g_free (*target);

  if (!title)
    *target = g_strdup ("");
  else if (g_utf8_strlen (title, MAX_TITLE_LENGTH + 1) > MAX_TITLE_LENGTH)
    {
      *target = meta_g_utf8_strndup (title, MAX_TITLE_LENGTH);
      modified = TRUE;
    }
  /* if WM_CLIENT_MACHINE indicates this machine is on a remote host
   * lets place that hostname in the title */
  else if (meta_window_is_remote (window))
    {
      *target = g_strdup_printf (_("%s (on %s)"),
                                 title, priv->wm_client_machine);
      modified = TRUE;
    }
  else
    *target = g_strdup (title);

  if (modified && atom != None)
    meta_prop_set_utf8_string_hint (window->display->x11_display,
                                    meta_window_x11_get_xwindow (window),
                                    atom, *target);

  /* Bug 330671 -- Don't forget to clear _NET_WM_VISIBLE_(ICON_)NAME */
  if (!modified && previous_was_modified)
    {
      mtk_x11_error_trap_push (window->display->x11_display->xdisplay);
      XDeleteProperty (window->display->x11_display->xdisplay,
                       meta_window_x11_get_xwindow (window),
                       atom);
      mtk_x11_error_trap_pop (window->display->x11_display->xdisplay);
    }

  return modified;
}

static void
set_window_title (MetaWindow *window,
                  const char *title)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);

  char *new_title = NULL;

  gboolean modified =
    set_title_text (window,
                    priv->using_net_wm_visible_name,
                    title,
                    window->display->x11_display->atom__NET_WM_VISIBLE_NAME,
                    &new_title);
  priv->using_net_wm_visible_name = modified;

  meta_window_set_title (window, new_title);

  g_free (new_title);
}

static void
reload_net_wm_name (MetaWindow    *window,
                    MetaPropValue *value,
                    gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);

  if (value->type != META_PROP_VALUE_INVALID)
    {
      set_window_title (window, value->v.str);
      priv->using_net_wm_name = TRUE;

      meta_topic (META_DEBUG_X11,
                  "Using _NET_WM_NAME for new title of %s: \"%s\"",
                  window->desc, window->title);
    }
  else
    {
      set_window_title (window, NULL);
      priv->using_net_wm_name = FALSE;
      if (!initial)
        meta_window_reload_property (window, XA_WM_NAME, FALSE);
    }
}

static void
reload_wm_name (MetaWindow    *window,
                MetaPropValue *value,
                gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);

  if (priv->using_net_wm_name)
    {
      meta_topic (META_DEBUG_X11,
                  "Ignoring WM_NAME \"%s\" as _NET_WM_NAME is set",
                  value->v.str);
      return;
    }

  if (value->type != META_PROP_VALUE_INVALID)
    {
      set_window_title (window, value->v.str);

      meta_topic (META_DEBUG_X11,
                  "Using WM_NAME for new title of %s: \"%s\"",
                  window->desc, window->title);
    }
  else
    {
      set_window_title (window, NULL);
    }
}

static void
meta_window_set_opaque_region (MetaWindow *window,
                               MtkRegion  *region)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));

  if (mtk_region_equal (priv->opaque_region, region))
    return;

  g_clear_pointer (&priv->opaque_region, mtk_region_unref);

  if (region != NULL)
    priv->opaque_region = mtk_region_ref (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}

static void
reload_opaque_region (MetaWindow    *window,
                      MetaPropValue *value,
                      gboolean       initial)
{
  MtkRegion *opaque_region = NULL;
  MetaFrame *frame;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      uint32_t *region = value->v.cardinal_list.cardinals;
      int nitems = value->v.cardinal_list.n_cardinals;

      MtkRectangle *rects;
      int i, rect_index, nrects;

      if (nitems % 4 != 0)
        {
          meta_topic (META_DEBUG_X11,
                      "_NET_WM_OPAQUE_REGION does not have a list of 4-tuples.");
          goto out;
        }

      /* empty region */
      if (nitems == 0)
        goto out;

      nrects = nitems / 4;

      rects = g_new (MtkRectangle, nrects);

      rect_index = 0;
      i = 0;
      while (i < nitems)
        {
          MtkRectangle region_rect = MTK_RECTANGLE_INIT (region[i + 0],
                                                         region[i + 1],
                                                         region[i + 2],
                                                         region[i + 3]);
          MtkRectangle *rect = &rects[rect_index];

          meta_window_protocol_to_stage_rect (window, &region_rect, rect);

          i += 4;
          rect_index++;
        }

      opaque_region = mtk_region_create_rectangles (rects, nrects);

      g_free (rects);
    }

 out:
  frame = meta_window_x11_get_frame (window);
  if (value->source_xwindow == meta_window_x11_get_xwindow (window))
    meta_window_set_opaque_region (window, opaque_region);
  else if (frame && value->source_xwindow == frame->xwindow)
    meta_frame_set_opaque_region (frame, opaque_region);

  g_clear_pointer (&opaque_region, mtk_region_unref);
}

static void
reload_mutter_hints (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      char     *new_hints = value->v.str;
      char     *old_hints = window->mutter_hints;
      gboolean  changed   = FALSE;

      if (new_hints)
        {
          if (!old_hints || strcmp (new_hints, old_hints))
            changed = TRUE;
        }
      else
        {
          if (old_hints)
            changed = TRUE;
        }

      if (changed)
        {
          g_free (old_hints);

          if (new_hints)
            window->mutter_hints = g_strdup (new_hints);
          else
            window->mutter_hints = NULL;

          g_object_notify (G_OBJECT (window), "mutter-hints");
        }
    }
  else if (window->mutter_hints)
    {
      g_free (window->mutter_hints);
      window->mutter_hints = NULL;

      g_object_notify (G_OBJECT (window), "mutter-hints");
    }
}

static void
reload_net_wm_state (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);
  gboolean maximize_horizontally = FALSE;
  gboolean maximize_vertically = FALSE;
  int i;

  if (!initial)
    {
      meta_topic (META_DEBUG_X11,
                  "Ignoring _NET_WM_STATE: we should be the one who set "
                  "the property in the first place");
      return;
    }

  meta_window_config_set_maximized_directions (window->config, FALSE, FALSE);
  meta_window_config_set_is_fullscreen (window->config, FALSE);
  priv->wm_state_modal = FALSE;
  priv->wm_state_skip_taskbar = FALSE;
  priv->wm_state_skip_pager = FALSE;
  window->wm_state_above = FALSE;
  window->wm_state_below = FALSE;
  window->wm_state_demands_attention = FALSE;

  if (value->type == META_PROP_VALUE_INVALID)
    return;

  i = 0;
  while (i < value->v.atom_list.n_atoms)
    {
      if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ)
        maximize_horizontally = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT)
        maximize_vertically = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_HIDDEN)
        window->minimize_after_placement = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_MODAL)
        priv->wm_state_modal = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_SKIP_TASKBAR)
        priv->wm_state_skip_taskbar = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_SKIP_PAGER)
        priv->wm_state_skip_pager = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_FULLSCREEN)
        {
          meta_window_config_set_is_fullscreen (window->config, TRUE);
          g_object_notify (G_OBJECT (window), "fullscreen");
        }
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_ABOVE)
        window->wm_state_above = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_BELOW)
        window->wm_state_below = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION)
        window->wm_state_demands_attention = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_STICKY)
        window->on_all_workspaces_requested = TRUE;

      ++i;
    }

  meta_window_config_set_maximized_directions (window->config,
                                               maximize_horizontally,
                                               maximize_vertically);

  meta_topic (META_DEBUG_X11,
              "Reloaded _NET_WM_STATE for %s",
              window->desc);

  meta_window_x11_recalc_window_type (window);
  meta_window_recalc_features (window);
}

static void
reload_mwm_hints (MetaWindow    *window,
                  MetaPropValue *value,
                  gboolean       initial)
{
  MotifWmHints *hints;
  gboolean old_decorated = window->decorated;

  window->mwm_decorated = TRUE;
  window->mwm_border_only = FALSE;
  window->mwm_has_close_func = TRUE;
  window->mwm_has_minimize_func = TRUE;
  window->mwm_has_maximize_func = TRUE;
  window->mwm_has_move_func = TRUE;
  window->mwm_has_resize_func = TRUE;

  if (value->type == META_PROP_VALUE_INVALID)
    {
      meta_topic (META_DEBUG_X11, "Window %s has no MWM hints", window->desc);
      meta_window_recalc_features (window);
      return;
    }

  hints = value->v.motif_hints;

  /* We support those MWM hints deemed non-stupid */

  meta_topic (META_DEBUG_X11, "Window %s has MWM hints",
              window->desc);

  if (hints->flags & MWM_HINTS_DECORATIONS)
    {
      meta_topic (META_DEBUG_X11, "Window %s sets MWM_HINTS_DECORATIONS 0x%x",
                  window->desc, hints->decorations);

      if (hints->decorations == 0)
        window->mwm_decorated = FALSE;
      /* some input methods use this */
      else if (hints->decorations == MWM_DECOR_BORDER)
        window->mwm_border_only = TRUE;
    }
  else
    {
      meta_topic (META_DEBUG_X11, "Decorations flag unset");
    }

  if (hints->flags & MWM_HINTS_FUNCTIONS)
    {
      gboolean toggle_value;

      meta_topic (META_DEBUG_X11, "Window %s sets MWM_HINTS_FUNCTIONS 0x%x",
                  window->desc, hints->functions);

      /* If _ALL is specified, then other flags indicate what to turn off;
       * if ALL is not specified, flags are what to turn on.
       * at least, I think so
       */

      if ((hints->functions & MWM_FUNC_ALL) == 0)
        {
          toggle_value = TRUE;

          meta_topic (META_DEBUG_X11, "Window %s disables all funcs then reenables some",
                      window->desc);
          window->mwm_has_close_func = FALSE;
          window->mwm_has_minimize_func = FALSE;
          window->mwm_has_maximize_func = FALSE;
          window->mwm_has_move_func = FALSE;
          window->mwm_has_resize_func = FALSE;
        }
      else
        {
          meta_topic (META_DEBUG_X11, "Window %s enables all funcs then disables some",
                      window->desc);
          toggle_value = FALSE;
        }

      if ((hints->functions & MWM_FUNC_CLOSE) != 0)
        {
          meta_topic (META_DEBUG_X11, "Window %s toggles close via MWM hints",
                      window->desc);
          window->mwm_has_close_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_MINIMIZE) != 0)
        {
          meta_topic (META_DEBUG_X11, "Window %s toggles minimize via MWM hints",
                      window->desc);
          window->mwm_has_minimize_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_MAXIMIZE) != 0)
        {
          meta_topic (META_DEBUG_X11, "Window %s toggles maximize via MWM hints",
                      window->desc);
          window->mwm_has_maximize_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_MOVE) != 0)
        {
          meta_topic (META_DEBUG_X11, "Window %s toggles move via MWM hints",
                      window->desc);
          window->mwm_has_move_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_RESIZE) != 0)
        {
          meta_topic (META_DEBUG_X11, "Window %s toggles resize via MWM hints",
                      window->desc);
          window->mwm_has_resize_func = toggle_value;
        }
    }
  else
    {
      meta_topic (META_DEBUG_X11, "Functions flag unset");
    }

  meta_window_recalc_features (window);

  /* We do all this anyhow at the end of meta_window_x11_new() */
  if (!window->constructing)
    {
      if (window->decorated)
        meta_window_ensure_frame (window);
      else
        meta_window_destroy_frame (window);

      meta_window_queue (window,
                         META_QUEUE_MOVE_RESIZE |
                         /* because ensure/destroy frame may unmap: */
                         META_QUEUE_CALC_SHOWING);

      if (old_decorated != window->decorated)
        g_object_notify (G_OBJECT (window), "decorated");
    }
}

static void
reload_wm_class (MetaWindow    *window,
                 MetaPropValue *value,
                 gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      g_autofree gchar *res_class = g_convert (value->v.class_hint.res_class, -1,
                                               "UTF-8", "LATIN1",
                                               NULL, NULL, NULL);
      g_autofree gchar *res_name = g_convert (value->v.class_hint.res_name, -1,
                                              "UTF-8", "LATIN1",
                                              NULL, NULL, NULL);
      meta_window_set_wm_class (window, res_class, res_name);
    }
  else
    {
      meta_window_set_wm_class (window, NULL, NULL);
    }

  meta_topic (META_DEBUG_X11, "Window %s class: '%s' name: '%s'",
              window->desc,
              window->res_class ? window->res_class : "none",
              window->res_name ? window->res_name : "none");
}

static void
reload_net_wm_desktop (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      window->initial_workspace_set = TRUE;
      window->initial_workspace = value->v.cardinal;
      meta_topic (META_DEBUG_PLACEMENT,
                  "Read initial workspace prop %d for %s",
                  window->initial_workspace, window->desc);
    }
}

static void
reload_net_startup_id (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  guint32 timestamp = window->net_wm_user_time;
  MetaWorkspace *workspace = NULL;

  g_free (window->startup_id);

  if (value->type != META_PROP_VALUE_INVALID)
    window->startup_id = g_strdup (value->v.str);
  else
    window->startup_id = NULL;

  /* Update timestamp and workspace on a running window */
  if (!window->constructing)
  {
    window->initial_timestamp_set = 0;
    window->initial_workspace_set = 0;

    if (meta_display_apply_startup_properties (window->display, window))
      {

        if (window->initial_timestamp_set)
          timestamp = window->initial_timestamp;
        if (window->initial_workspace_set)
          workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                                     window->initial_workspace);

        meta_window_activate_with_workspace (window, timestamp, workspace);
      }
  }

  meta_topic (META_DEBUG_X11,
              "New _NET_STARTUP_ID \"%s\" for %s",
              window->startup_id ? window->startup_id : "unset",
              window->desc);
}

static void
reload_update_counter (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      MetaSyncCounter *sync_counter;
      MetaFrame *frame = meta_window_x11_get_frame (window);

      if (value->source_xwindow == meta_window_x11_get_xwindow (window))
        sync_counter = meta_window_x11_get_sync_counter (window);
      else if (frame && value->source_xwindow == frame->xwindow)
        sync_counter = meta_frame_get_sync_counter (frame);
      else
        g_assert_not_reached ();

      if (value->v.xcounter_list.n_counters == 0)
        {
          meta_topic (META_DEBUG_X11, "_NET_WM_SYNC_REQUEST_COUNTER is empty");
          meta_sync_counter_set_counter (sync_counter, None, FALSE);
          return;
        }

      if (value->v.xcounter_list.n_counters == 1)
        {
          meta_sync_counter_set_counter (sync_counter,
                                         value->v.xcounter_list.counters[0],
                                         FALSE);
        }
      else
        {
          meta_sync_counter_set_counter (sync_counter,
                                         value->v.xcounter_list.counters[1],
                                         TRUE);
        }
    }
}

#define FLAG_IS_ON(hints,flag) \
  (((hints)->flags & (flag)) != 0)

#define FLAG_IS_OFF(hints,flag) \
  (((hints)->flags & (flag)) == 0)

#define FLAG_TOGGLED_ON(old,new,flag) \
  (FLAG_IS_OFF(old,flag) &&           \
   FLAG_IS_ON(new,flag))

#define FLAG_TOGGLED_OFF(old,new,flag) \
  (FLAG_IS_ON(old,flag) &&             \
   FLAG_IS_OFF(new,flag))

#define FLAG_CHANGED(old,new,flag) \
  (FLAG_TOGGLED_ON(old,new,flag) || FLAG_TOGGLED_OFF(old,new,flag))

static void
spew_size_hints_differences (const MetaSizeHints *old,
                             const MetaSizeHints *new)
{
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_USER_POSITION))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: USER_POSITION now %s",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_USER_POSITION) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_USER_SIZE))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: USER_SIZE now %s",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_USER_SIZE) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_POSITION))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_POSITION now %s",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_POSITION) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_SIZE))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_SIZE now %s",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_SIZE) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_MIN_SIZE))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_MIN_SIZE now %s (%d x %d -> %d x %d)",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_MIN_SIZE) ? "set" : "unset",
                old->min_width, old->min_height,
                new->min_width, new->min_height);
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_MAX_SIZE))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_MAX_SIZE now %s (%d x %d -> %d x %d)",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_MAX_SIZE) ? "set" : "unset",
                old->max_width, old->max_height,
                new->max_width, new->max_height);
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_RESIZE_INCREMENTS))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_RESIZE_INCREMENTS now %s (width_inc %d -> %d height_inc %d -> %d)",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_RESIZE_INCREMENTS) ? "set" : "unset",
                old->width_inc, new->width_inc,
                old->height_inc, new->height_inc);
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_ASPECT))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_ASPECT now %s (min %d/%d -> %d/%d max %d/%d -> %d/%d)",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_ASPECT) ? "set" : "unset",
                old->min_aspect.x, old->min_aspect.y,
                new->min_aspect.x, new->min_aspect.y,
                old->max_aspect.x, old->max_aspect.y,
                new->max_aspect.x, new->max_aspect.y);
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_BASE_SIZE))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_BASE_SIZE now %s (%d x %d -> %d x %d)",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_BASE_SIZE) ? "set" : "unset",
                old->base_width, old->base_height,
                new->base_width, new->base_height);
  if (FLAG_CHANGED (old, new, META_SIZE_HINTS_PROGRAM_WIN_GRAVITY))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PROGRAM_WIN_GRAVITY now %s  (%d -> %d)",
                FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_WIN_GRAVITY) ? "set" : "unset",
                old->win_gravity, new->win_gravity);
}

static gboolean
hints_have_changed (const MetaSizeHints *old,
                    const MetaSizeHints *new)
{
  /* 1. Check if the relevant values have changed if the flag is set. */

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_USER_POSITION) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_USER_POSITION) &&
       (old->x != new->x ||
        old->y != new->y)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_USER_SIZE) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_USER_SIZE) &&
       (old->width != new->width ||
        old->height != new->height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_POSITION) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_POSITION) &&
       (old->x != new->x ||
        old->y != new->y)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_SIZE) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_SIZE) &&
       (old->width != new->width ||
        old->height != new->height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_MIN_SIZE) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_MIN_SIZE) &&
       (old->min_width != new->min_width ||
        old->min_height != new->min_height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_MAX_SIZE) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_MAX_SIZE) &&
       (old->max_width != new->max_width ||
        old->max_height != new->max_height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_RESIZE_INCREMENTS) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_RESIZE_INCREMENTS) &&
       (old->width_inc != new->width_inc ||
        old->height_inc != new->height_inc)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_ASPECT) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_ASPECT) &&
       (old->min_aspect.x != new->min_aspect.x ||
        old->min_aspect.y != new->min_aspect.y ||
        old->max_aspect.x != new->max_aspect.x ||
        old->max_aspect.y != new->max_aspect.y)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_BASE_SIZE) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_BASE_SIZE) &&
       (old->base_width != new->base_width ||
        old->base_height != new->base_height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, META_SIZE_HINTS_PROGRAM_WIN_GRAVITY) ||
      (FLAG_IS_ON (new, META_SIZE_HINTS_PROGRAM_WIN_GRAVITY) &&
       (old->win_gravity != new->win_gravity)))
    return TRUE;

  /* 2. Check if the flags have been unset. */
  return FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_USER_POSITION) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_USER_POSITION) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_POSITION) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_SIZE) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_MIN_SIZE) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_MAX_SIZE) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_RESIZE_INCREMENTS) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_ASPECT) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_BASE_SIZE) ||
         FLAG_TOGGLED_OFF (old, new, META_SIZE_HINTS_PROGRAM_WIN_GRAVITY);
}

static void
scale_size_hints (MetaWindow    *window,
                  MetaSizeHints *hints)
{
  meta_window_protocol_to_stage_point (window,
                                       hints->x, hints->y,
                                       &hints->x, &hints->y,
                                       MTK_ROUNDING_STRATEGY_SHRINK);
  meta_window_protocol_to_stage_point (window,
                                       hints->width, hints->height,
                                       &hints->width, &hints->height,
                                       MTK_ROUNDING_STRATEGY_GROW);

  meta_window_protocol_to_stage_point (window,
                                       hints->min_width, hints->min_height,
                                       &hints->min_width, &hints->min_height,
                                       MTK_ROUNDING_STRATEGY_GROW);

  meta_window_protocol_to_stage_point (window,
                                       hints->max_width, hints->max_height,
                                       &hints->max_width, &hints->max_height,
                                       MTK_ROUNDING_STRATEGY_GROW);

  meta_window_protocol_to_stage_point (window,
                                       hints->width_inc, hints->height_inc,
                                       &hints->width_inc, &hints->height_inc,
                                       MTK_ROUNDING_STRATEGY_ROUND);

  meta_window_protocol_to_stage_point (window,
                                       hints->min_aspect.x, hints->min_aspect.y,
                                       &hints->min_aspect.x, &hints->min_aspect.y,
                                       MTK_ROUNDING_STRATEGY_ROUND);

  meta_window_protocol_to_stage_point (window,
                                       hints->max_aspect.x, hints->max_aspect.y,
                                       &hints->max_aspect.x, &hints->max_aspect.y,
                                       MTK_ROUNDING_STRATEGY_ROUND);

  meta_window_protocol_to_stage_point (window,
                                       hints->base_width, hints->base_height,
                                       &hints->base_width, &hints->base_height,
                                       MTK_ROUNDING_STRATEGY_GROW);
}

static void
reload_normal_hints (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      MetaSizeHints old_hints;
      gboolean hints_have_differences;

      meta_topic (META_DEBUG_GEOMETRY, "Updating WM_NORMAL_HINTS for %s", window->desc);

      old_hints = window->size_hints;

      if (value->v.size_hints.hints)
        {
          MetaSizeHints new_hints;

          new_hints = *(MetaSizeHints *) value->v.size_hints.hints;
          scale_size_hints (window, &new_hints);
          meta_window_set_normal_hints (window, &new_hints);
        }
      else
        {
          meta_window_set_normal_hints (window, NULL);
        }

      hints_have_differences = hints_have_changed (&old_hints,
                                                   &window->size_hints);
      if (hints_have_differences)
        {
          spew_size_hints_differences (&old_hints, &window->size_hints);
          meta_window_recalc_features (window);

          if (!initial)
            meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
        }
    }
}

static void
reload_wm_protocols (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  int i;

  meta_window_x11_set_wm_take_focus (window, FALSE);
  meta_window_x11_set_wm_ping (window, FALSE);
  meta_window_x11_set_wm_delete_window (window, FALSE);

  if (value->type == META_PROP_VALUE_INVALID)
    return;

  i = 0;
  while (i < value->v.atom_list.n_atoms)
    {
      if (value->v.atom_list.atoms[i] ==
          window->display->x11_display->atom_WM_TAKE_FOCUS)
        meta_window_x11_set_wm_take_focus (window, TRUE);
      else if (value->v.atom_list.atoms[i] ==
               window->display->x11_display->atom_WM_DELETE_WINDOW)
        meta_window_x11_set_wm_delete_window (window, TRUE);
      else if (value->v.atom_list.atoms[i] ==
               window->display->x11_display->atom__NET_WM_PING)
        meta_window_x11_set_wm_ping (window, TRUE);
      ++i;
    }

  meta_topic (META_DEBUG_X11,
              "New _NET_STARTUP_ID \"%s\" for %s",
              window->startup_id ? window->startup_id : "unset",
              window->desc);
}

static void
reload_wm_hints (MetaWindow    *window,
                 MetaPropValue *value,
                 gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);
  Window old_group_leader;
  gboolean urgent;

  old_group_leader = priv->xgroup_leader;

  /* Fill in defaults */
  window->input = TRUE;
  window->initially_iconic = FALSE;
  priv->xgroup_leader = None;
  priv->wm_hints_pixmap = None;
  priv->wm_hints_mask = None;
  urgent = FALSE;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      const XWMHints *hints = value->v.wm_hints;

      if (hints->flags & InputHint)
        window->input = hints->input;

      if (hints->flags & StateHint)
        window->initially_iconic = (hints->initial_state == IconicState);

      if (hints->flags & WindowGroupHint)
        priv->xgroup_leader = hints->window_group;

      if (hints->flags & IconPixmapHint)
        priv->wm_hints_pixmap = hints->icon_pixmap;

      if (hints->flags & IconMaskHint)
        priv->wm_hints_mask = hints->icon_mask;

      if (hints->flags & XUrgencyHint)
        urgent = TRUE;

      meta_topic (META_DEBUG_X11,
                  "Read WM_HINTS input: %d iconic: %d group leader: 0x%lx pixmap: 0x%lx mask: 0x%lx",
                  window->input, window->initially_iconic,
                  priv->xgroup_leader,
                  priv->wm_hints_pixmap,
                  priv->wm_hints_mask);
    }

  if (priv->xgroup_leader != old_group_leader)
    {
      meta_topic (META_DEBUG_X11,
                  "Window %s changed its group leader to 0x%lx",
                  window->desc, priv->xgroup_leader);

      meta_window_x11_group_leader_changed (window);
    }

  meta_window_set_urgent (window, urgent);

  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}

static gboolean
check_xtransient_for_loop (MetaWindow *window,
                           MetaWindow *parent)
{
  while (parent)
    {
      if (parent == window)
        return TRUE;

      parent = meta_x11_display_lookup_x_window (parent->display->x11_display,
                                                 meta_window_x11_get_xtransient_for (parent));
    }

  return FALSE;
}

static void
reload_transient_for (MetaWindow    *window,
                      MetaPropValue *value,
                      gboolean       initial)
{
  MetaWindow *parent = NULL;
  Window transient_for, current_transient_for;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      transient_for = value->v.xwindow;

      parent = meta_x11_display_lookup_x_window (window->display->x11_display,
                                                 transient_for);
      if (!parent)
        {
          meta_topic (META_DEBUG_X11,
                      "Invalid WM_TRANSIENT_FOR window 0x%lx specified for %s.",
                      transient_for, window->desc);
          transient_for = None;
        }
      else if (parent->override_redirect)
        {
          const gchar *window_kind = window->override_redirect ?
                                     "override-redirect" : "top-level";
          Window parent_xtransient_for = meta_window_x11_get_xtransient_for (parent);
          if (parent_xtransient_for != None)
            {
              /* We don't have to go through the parents, as per this code it is
               * not possible that a window has the WM_TRANSIENT_FOR set to an
               * override-redirect window anyways */
              meta_topic (META_DEBUG_X11,
                          "WM_TRANSIENT_FOR window %s for %s window %s is an "
                          "override-redirect window and this is not correct "
                          "according to the standard, so we'll fallback to "
                          "the first non-override-redirect window 0x%lx.",
                          parent->desc, window->desc, window_kind,
                          parent_xtransient_for);
              transient_for = parent_xtransient_for;
              parent =
                meta_x11_display_lookup_x_window (parent->display->x11_display,
                                                  transient_for);
            }
          else
            {
              meta_topic (META_DEBUG_X11,
                          "WM_TRANSIENT_FOR window %s for %s window %s is an "
                          "override-redirect window and this is not correct "
                          "according to the standard, so we'll fallback to "
                          "the root window.",
                          parent->desc, window_kind, window->desc);
              transient_for = parent->display->x11_display->xroot;
              parent = NULL;
            }
        }

      /* Make sure there is not a loop */
      if (check_xtransient_for_loop (window, parent))
        {
          meta_topic (META_DEBUG_X11,
                      "WM_TRANSIENT_FOR window 0x%lx for %s would create a loop.",
                      transient_for, window->desc);
          transient_for = None;
        }
    }
  else
    transient_for = None;

  current_transient_for = meta_window_x11_get_xtransient_for (window);
  if (transient_for == current_transient_for)
    return;


  current_transient_for = transient_for;
  if (current_transient_for != None)
    {
      meta_topic (META_DEBUG_X11, "Window %s transient for 0x%lx",
                  window->desc, current_transient_for);
    }
  else
    {
      meta_topic (META_DEBUG_X11, "Window %s is not transient", window->desc);
    }

  if (current_transient_for == None ||
      current_transient_for == window->display->x11_display->xroot)
    meta_window_set_transient_for (window, NULL);
  else
    {
      meta_window_set_transient_for (window, parent);
    }
}

static void
reload_gtk_theme_variant (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  char     *requested_variant = NULL;
  char     *current_variant   = window->gtk_theme_variant;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      requested_variant = value->v.str;
      meta_topic (META_DEBUG_X11,
                  "Requested \"%s\" theme variant for window %s.",
                  requested_variant, window->desc);
    }

  if (g_strcmp0 (requested_variant, current_variant) != 0)
    {
      g_free (current_variant);

      window->gtk_theme_variant = g_strdup (requested_variant);
    }
}

static void
reload_bypass_compositor (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);
  MetaBypassCompositorHint requested_value;
  MetaBypassCompositorHint current_value;

  if (value->type != META_PROP_VALUE_INVALID)
    requested_value = (MetaBypassCompositorHint) value->v.cardinal;
  else
    requested_value = META_BYPASS_COMPOSITOR_HINT_AUTO;

  current_value = priv->bypass_compositor;
  if (requested_value == current_value)
    return;

  if (requested_value == META_BYPASS_COMPOSITOR_HINT_ON)
    {
      meta_topic (META_DEBUG_X11,
                  "Request to bypass compositor for window %s.", window->desc);
    }
  else if (requested_value == META_BYPASS_COMPOSITOR_HINT_OFF)
    {
      meta_topic (META_DEBUG_X11,
                  "Request to don't bypass compositor for window %s.", window->desc);
    }
  else if (requested_value != META_BYPASS_COMPOSITOR_HINT_AUTO)
    {
      return;
    }

  priv->bypass_compositor = requested_value;
}

static void
reload_window_opacity (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)

{
  guint8 opacity = 0xFF;

  if (value->type != META_PROP_VALUE_INVALID)
    opacity = (guint8)((gfloat)value->v.cardinal * 255.0 / ((gfloat)0xffffffff));

  meta_window_set_opacity (window, opacity);
}

static void
reload_fullscreen_monitors (MetaWindow    *window,
                            MetaPropValue *value,
                            gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      if (value->v.cardinal_list.n_cardinals != 4)
        {
          meta_topic (META_DEBUG_X11,
                      "_NET_WM_FULLSCREEN_MONITORS on %s has %d values instead of 4",
                      window->desc, value->v.cardinal_list.n_cardinals);
        }
      else
        {
          MetaX11Display *x11_display = window->display->x11_display;
          int top_xinerama_index, bottom_xinerama_index;
          int left_xinerama_index, right_xinerama_index;
          MetaLogicalMonitor *top, *bottom, *left, *right;

          top_xinerama_index = (int)value->v.cardinal_list.cardinals[0];
          bottom_xinerama_index = (int)value->v.cardinal_list.cardinals[1];
          left_xinerama_index = (int)value->v.cardinal_list.cardinals[2];
          right_xinerama_index = (int)value->v.cardinal_list.cardinals[3];

          top =
            meta_x11_display_xinerama_index_to_logical_monitor (x11_display,
                                                                top_xinerama_index);
          bottom =
            meta_x11_display_xinerama_index_to_logical_monitor (x11_display,
                                                                bottom_xinerama_index);
          left =
            meta_x11_display_xinerama_index_to_logical_monitor (x11_display,
                                                                left_xinerama_index);
          right =
            meta_x11_display_xinerama_index_to_logical_monitor (x11_display,
                                                                right_xinerama_index);

          meta_window_update_fullscreen_monitors (window, top, bottom, left, right);
        }
    }
}

#define RELOAD_STRING(var_name, propname) \
  static void                                       \
  reload_ ## var_name (MetaWindow    *window,       \
                       MetaPropValue *value,        \
                       gboolean       initial)      \
  {                                                 \
    g_free (window->var_name);                      \
                                                    \
    if (value->type != META_PROP_VALUE_INVALID)     \
      window->var_name = g_strdup (value->v.str);   \
    else                                            \
      window->var_name = NULL;                      \
                                                    \
    g_object_notify (G_OBJECT (window), propname);  \
  }

RELOAD_STRING (gtk_unique_bus_name,         "gtk-unique-bus-name")
RELOAD_STRING (gtk_application_id,          "gtk-application-id")
RELOAD_STRING (gtk_application_object_path, "gtk-application-object-path")
RELOAD_STRING (gtk_window_object_path,      "gtk-window-object-path")
RELOAD_STRING (gtk_app_menu_object_path,    "gtk-app-menu-object-path")
RELOAD_STRING (gtk_menubar_object_path,     "gtk-menubar-object-path")

#undef RELOAD_STRING

/**
 * meta_x11_display_init_window_prop_hooks:
 * @x11_display: The #MetaX11Display
 *
 * Initialises the property hooks system.  Each row in the table named "hooks"
 * represents an action to take when a property is found on a newly-created
 * window, or when a property changes its value.
 *
 * The first column shows which atom the row concerns.
 * The second gives the type of the property data.  The property will be
 * queried for its new value, unless the type is given as
 * META_PROP_VALUE_INVALID, in which case nothing will be queried.
 * The third column gives the name of a callback which gets called with the
 * new value.  (If the new value was not retrieved because the second column
 * was META_PROP_VALUE_INVALID, the callback still gets called anyway.)
 * This value may be NULL, in which case no callback will be called.
 */
void
meta_x11_display_init_window_prop_hooks (MetaX11Display *x11_display)
{
  /* The ordering here is significant for the properties we load
   * initially: they are roughly ordered in the order we want them to
   * be gotten. We want to get window name and class first so we can
   * use them in error messages and such. However, name is modified
   * depending on wm_client_machine, so push it slightly sooner.
   *
   * For override-redirect windows, we pay attention to:
   *
   *  - properties that identify the window: useful for debugging
   *    purposes.
   *  - NET_WM_WINDOW_TYPE: can be used to do appropriate handling
   *    for different types of override-redirect windows.
   */
  MetaWindowPropHooks hooks[] = {
    { x11_display->atom_WM_CLIENT_MACHINE, META_PROP_VALUE_STRING,   reload_wm_client_machine, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_NAME,      META_PROP_VALUE_UTF8,     reload_net_wm_name,       LOAD_INIT | INCLUDE_OR },
    { XA_WM_CLASS,                         META_PROP_VALUE_CLASS_HINT, reload_wm_class,        LOAD_INIT | INCLUDE_OR },
    { XA_WM_NAME,                          META_PROP_VALUE_TEXT_PROPERTY, reload_wm_name,      LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__MUTTER_HINTS,     META_PROP_VALUE_TEXT_PROPERTY, reload_mutter_hints, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_OPAQUE_REGION, META_PROP_VALUE_CARDINAL_LIST, reload_opaque_region, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_DESKTOP,   META_PROP_VALUE_CARDINAL, reload_net_wm_desktop,    LOAD_INIT | INIT_ONLY },
    { x11_display->atom__NET_STARTUP_ID,   META_PROP_VALUE_UTF8,     reload_net_startup_id,    LOAD_INIT },
    { x11_display->atom__NET_WM_SYNC_REQUEST_COUNTER, META_PROP_VALUE_SYNC_COUNTER_LIST, reload_update_counter, LOAD_INIT | INCLUDE_OR },
    { XA_WM_NORMAL_HINTS,                  META_PROP_VALUE_SIZE_HINTS, reload_normal_hints,    LOAD_INIT },
    { x11_display->atom_WM_PROTOCOLS,      META_PROP_VALUE_ATOM_LIST, reload_wm_protocols,     LOAD_INIT },
    { XA_WM_HINTS,                         META_PROP_VALUE_WM_HINTS,  reload_wm_hints,         LOAD_INIT },
    { x11_display->atom__NET_WM_USER_TIME, META_PROP_VALUE_CARDINAL, reload_net_wm_user_time,  LOAD_INIT },
    { x11_display->atom__NET_WM_STATE,     META_PROP_VALUE_ATOM_LIST, reload_net_wm_state,     LOAD_INIT | INIT_ONLY },
    { x11_display->atom__MOTIF_WM_HINTS,   META_PROP_VALUE_MOTIF_HINTS, reload_mwm_hints,      LOAD_INIT },
    { XA_WM_TRANSIENT_FOR,                 META_PROP_VALUE_WINDOW,    reload_transient_for,    LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__GTK_THEME_VARIANT, META_PROP_VALUE_UTF8,     reload_gtk_theme_variant, LOAD_INIT },
    { x11_display->atom__GTK_APPLICATION_ID,               META_PROP_VALUE_UTF8,         reload_gtk_application_id,               LOAD_INIT },
    { x11_display->atom__GTK_UNIQUE_BUS_NAME,              META_PROP_VALUE_UTF8,         reload_gtk_unique_bus_name,              LOAD_INIT },
    { x11_display->atom__GTK_APPLICATION_OBJECT_PATH,      META_PROP_VALUE_UTF8,         reload_gtk_application_object_path,      LOAD_INIT },
    { x11_display->atom__GTK_WINDOW_OBJECT_PATH,           META_PROP_VALUE_UTF8,         reload_gtk_window_object_path,           LOAD_INIT },
    { x11_display->atom__GTK_APP_MENU_OBJECT_PATH,         META_PROP_VALUE_UTF8,         reload_gtk_app_menu_object_path,         LOAD_INIT },
    { x11_display->atom__GTK_MENUBAR_OBJECT_PATH,          META_PROP_VALUE_UTF8,         reload_gtk_menubar_object_path,          LOAD_INIT },
    { x11_display->atom__GTK_FRAME_EXTENTS,                META_PROP_VALUE_CARDINAL_LIST,reload_gtk_frame_extents,                LOAD_INIT },
    { x11_display->atom__NET_WM_USER_TIME_WINDOW, META_PROP_VALUE_WINDOW, reload_net_wm_user_time_window, LOAD_INIT },
    { x11_display->atom__NET_WM_ICON_GEOMETRY, META_PROP_VALUE_CARDINAL_LIST, reload_icon_geometry, LOAD_INIT },
    { x11_display->atom_WM_CLIENT_LEADER,  META_PROP_VALUE_INVALID, complain_about_broken_client, NONE },
    { x11_display->atom_SM_CLIENT_ID,      META_PROP_VALUE_INVALID, complain_about_broken_client, NONE },
    { x11_display->atom_WM_WINDOW_ROLE,    META_PROP_VALUE_STRING, reload_wm_window_role, LOAD_INIT | FORCE_INIT },
    { x11_display->atom__NET_WM_WINDOW_TYPE, META_PROP_VALUE_ATOM_LIST, reload_net_wm_window_type, LOAD_INIT | INCLUDE_OR | FORCE_INIT },
    { x11_display->atom__NET_WM_STRUT,         META_PROP_VALUE_INVALID, reload_struts, NONE },
    { x11_display->atom__NET_WM_STRUT_PARTIAL, META_PROP_VALUE_INVALID, reload_struts, NONE },
    { x11_display->atom__NET_WM_BYPASS_COMPOSITOR, META_PROP_VALUE_CARDINAL,  reload_bypass_compositor, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_WINDOW_OPACITY, META_PROP_VALUE_CARDINAL, reload_window_opacity, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_WINDOW_TAG,    META_PROP_VALUE_STRING, reload_toplevel_tag, LOAD_INIT },
    { x11_display->atom__NET_WM_FULLSCREEN_MONITORS, META_PROP_VALUE_CARDINAL_LIST, reload_fullscreen_monitors, LOAD_INIT | INIT_ONLY },
    { 0 },
  };
  MetaWindowPropHooks *table;
  MetaWindowPropHooks *cursor;

  table = g_memdup2 (hooks, sizeof (hooks)),
  cursor = table;

  g_assert (x11_display->prop_hooks == NULL);

  x11_display->prop_hooks_table = (gpointer) table;
  x11_display->prop_hooks = g_hash_table_new (NULL, NULL);

  while (cursor->property)
    {
      /* Doing initial loading doesn't make sense if we just want notification */
      g_assert (!((cursor->flags & LOAD_INIT) && cursor->type == META_PROP_VALUE_INVALID));

      /* Forcing initialization doesn't make sense if not loading initially */
      g_assert ((cursor->flags & LOAD_INIT) || !(cursor->flags & FORCE_INIT));

      /* Atoms are safe to use with GINT_TO_POINTER because it's safe with
       * anything 32 bits or less, and atoms are 32 bits with the top three
       * bits clear.  (Scheifler & Gettys, 2e, p372)
       */
      g_hash_table_insert (x11_display->prop_hooks,
                           GINT_TO_POINTER (cursor->property),
                           cursor);
      cursor++;
    }
  x11_display->n_prop_hooks = cursor - table;
}

void
meta_x11_display_free_window_prop_hooks (MetaX11Display *x11_display)
{
  g_hash_table_unref (x11_display->prop_hooks);
  x11_display->prop_hooks = NULL;

  g_free (x11_display->prop_hooks_table);
  x11_display->prop_hooks_table = NULL;
}

static MetaWindowPropHooks *
find_hooks (MetaX11Display *x11_display,
            Atom            property)
{
  return g_hash_table_lookup (x11_display->prop_hooks,
                              GINT_TO_POINTER (property));
}
