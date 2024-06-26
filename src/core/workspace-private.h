/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file workspace.h    Workspaces
 *
 * A workspace is a set of windows which all live on the same
 * screen.  (You may also see the name "desktop" around the place,
 * which is the EWMH's name for the same thing.)  Only one workspace
 * of a screen may be active at once; all windows on all other workspaces
 * are unmapped.
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2004, 2005 Elijah Newren
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
#include "meta/workspace.h"

struct _MetaWorkspace
{
  GObject parent_instance;
  MetaDisplay *display;
  MetaWorkspaceManager *manager;

  GList *windows;

  /* The "MRU list", or "most recently used" list, is a list of
   * MetaWindows ordered based on the time the the user interacted
   * with the window most recently.
   *
   * For historical reasons, we keep an MRU list per workspace.
   * It used to be used to calculate the default focused window,
   * but isn't anymore, as the window next in the stacking order
   * can sometimes be not the window the user interacted with last,
   */
  GList *mru_list;

  GList  *list_containing_self;

  GHashTable *logical_monitor_data;

  MtkRectangle work_area_screen;
  GList  *screen_region;
  GList  *screen_edges;
  GList  *monitor_edges;
  GSList *builtin_struts;
  GSList *all_struts;
  guint work_areas_invalid : 1;

  guint showing_desktop : 1;
};

struct _MetaWorkspaceClass
{
  GObjectClass parent_class;
};

MetaWorkspace* meta_workspace_new           (MetaWorkspaceManager *workspace_manager);
void           meta_workspace_remove        (MetaWorkspace *workspace);
void           meta_workspace_add_window    (MetaWorkspace *workspace,
                                             MetaWindow    *window);
void           meta_workspace_remove_window (MetaWorkspace *workspace,
                                             MetaWindow    *window);
void           meta_workspace_relocate_windows (MetaWorkspace *workspace,
                                                MetaWorkspace *new_home);

void meta_workspace_get_work_area_for_logical_monitor (MetaWorkspace      *workspace,
                                                       MetaLogicalMonitor *logical_monitor,
                                                       MtkRectangle       *area);

void meta_workspace_invalidate_work_area (MetaWorkspace *workspace);

GList* meta_workspace_get_onscreen_region       (MetaWorkspace *workspace);
GList * meta_workspace_get_onmonitor_region (MetaWorkspace      *workspace,
                                             MetaLogicalMonitor *logical_monitor);

void meta_workspace_focus_default_window (MetaWorkspace *workspace,
                                          MetaWindow    *not_this_one,
                                          guint32        timestamp);
MetaWindow * meta_workspace_get_default_focus_window (MetaWorkspace *workspace,
                                                      MetaWindow    *not_this_one);
MetaWindow * meta_workspace_get_default_focus_window_at_point (MetaWorkspace *workspace,
                                                               MetaWindow    *not_this_one,
                                                               int            root_x,
                                                               int            root_y);
GList * meta_workspace_get_default_focus_candidates (MetaWorkspace *workspace);

void meta_workspace_index_changed (MetaWorkspace *workspace);

META_EXPORT_TEST
GSList * meta_workspace_get_builtin_struts (MetaWorkspace *workspace);
