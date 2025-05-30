/*
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2023 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#ifdef HAVE_FONTS
#include <pango/pango.h>
#endif

#include "clutter/clutter-context.h"
#include "clutter-stage-manager-private.h"

struct _ClutterContext
{
  GObject parent;

  ClutterBackend *backend;
  ClutterStageManager *stage_manager;

  GAsyncQueue *events_queue;

  /* the event filters added via clutter_event_add_filter. these are
   * ordered from least recently added to most recently added */
  GList *event_filters;

#ifdef HAVE_FONTS
  PangoRenderer *font_renderer;
  PangoFontMap *font_map;
#endif

  GSList *current_event;

  GList *repaint_funcs;
  guint last_repaint_id;

  ClutterSettings *settings;

  gboolean show_fps;
};

ClutterStageManager * clutter_context_get_stage_manager (ClutterContext *context);

gboolean clutter_context_get_show_fps (ClutterContext *context);

#ifdef HAVE_FONTS
PangoRenderer * clutter_context_get_font_renderer (ClutterContext *context);

/**
 * clutter_context_get_pango_fontmap: (skip)
 */
PangoFontMap * clutter_context_get_pango_fontmap (ClutterContext *context);
#endif
