/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington, Anders Carlsson
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#pragma once

#include "core/window-private.h"
#include "x11/meta-sync-counter.h"
#include "x11/window-x11.h"

G_BEGIN_DECLS

/*
 * Mirrors _NET_WM_BYPASS_COMPOSITOR preference values.
 */
typedef enum _MetaBypassCompositorHint
{
  META_BYPASS_COMPOSITOR_HINT_AUTO = 0,
  META_BYPASS_COMPOSITOR_HINT_ON = 1,
  META_BYPASS_COMPOSITOR_HINT_OFF = 2,
} MetaBypassCompositorHint;

typedef struct _MetaWindowX11Private MetaWindowX11Private;

struct _MetaWindowX11Private
{
  /* TRUE if the client forced these on */
  guint wm_state_skip_taskbar : 1;
  guint wm_state_skip_pager : 1;
  guint wm_take_focus : 1;
  guint wm_ping : 1;
  guint wm_delete_window : 1;

  /* Weird "_NET_WM_STATE_MODAL" flag */
  guint wm_state_modal : 1;

  /* Info on which props we got our attributes from */
  guint using_net_wm_name              : 1; /* vs. plain wm_name */
  guint using_net_wm_visible_name      : 1; /* tracked so we can clear it */

  Atom type_atom;

  XWindowAttributes attributes;

  /* Requested geometry */
  int border_width;

  gboolean showing_resize_popup;

  /* These are in server coordinates. If we have a frame, it's
   * relative to the frame. */
  MtkRectangle client_rect;

  Pixmap wm_hints_pixmap;
  Pixmap wm_hints_mask;

  /* Freeze/thaw on resize (for Xwayland) */
  gboolean thaw_after_paint;

  Visual *xvisual;

  Window xwindow;
  Window xclient_leader;
  Window xgroup_leader;

  /* window that gets updated net_wm_user_time values */
  Window user_time_window;

  /* Bypass compositor hints */
  MetaBypassCompositorHint bypass_compositor;

  /* maintained by group.c */
  MetaGroup *group;

  MetaSyncCounter sync_counter;
};

MetaWindowX11Private * meta_window_x11_get_private (MetaWindowX11 *window_x11);

void meta_window_x11_initialize_state (MetaWindow *window);

Window meta_window_x11_get_xgroup_leader (MetaWindow *window);

Window meta_window_x11_get_user_time_window (MetaWindow *window);

Window meta_window_x11_get_xtransient_for (MetaWindow *window);

gboolean meta_window_x11_has_pointer (MetaWindow *window);

gboolean meta_window_x11_same_application (MetaWindow *window,
                                           MetaWindow *other_window);

void meta_window_x11_shutdown_group (MetaWindow *window);

META_EXPORT
void meta_window_x11_group_leader_changed (MetaWindow *window);

G_END_DECLS
