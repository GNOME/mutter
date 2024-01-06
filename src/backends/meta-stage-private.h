/*
 * Copyright (C) 2012 Intel Corporation
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

#include "backends/meta-cursor.h"
#include "core/util-private.h"
#include "meta/boxes.h"
#include "meta/meta-stage.h"
#include "meta/types.h"

G_BEGIN_DECLS

typedef struct _MetaStageWatch MetaStageWatch;
typedef struct _MetaOverlay    MetaOverlay;

typedef enum
{
  META_STAGE_WATCH_BEFORE_PAINT,
  META_STAGE_WATCH_AFTER_ACTOR_PAINT,
  META_STAGE_WATCH_AFTER_OVERLAY_PAINT,
  META_STAGE_WATCH_AFTER_PAINT,
} MetaStageWatchPhase;

typedef void (* MetaStageWatchFunc) (MetaStage        *stage,
                                     ClutterStageView *view,
                                     const MtkRegion  *redraw_clip,
                                     ClutterFrame     *frame,
                                     gpointer          user_data);

ClutterActor     *meta_stage_new                     (MetaBackend *backend);

MetaOverlay      *meta_stage_create_cursor_overlay   (MetaStage   *stage);
void              meta_stage_remove_cursor_overlay   (MetaStage   *stage,
						      MetaOverlay *overlay);

void              meta_stage_update_cursor_overlay   (MetaStage            *stage,
                                                      MetaOverlay          *overlay,
                                                      CoglTexture          *texture,
                                                      graphene_rect_t      *rect,
                                                      MetaMonitorTransform  buffer_transform);

void meta_overlay_set_visible (MetaOverlay *overlay,
                               gboolean     is_visible);

void meta_stage_set_active (MetaStage *stage,
                            gboolean   is_active);

META_EXPORT_TEST
MetaStageWatch * meta_stage_watch_view (MetaStage           *stage,
                                        ClutterStageView    *view,
                                        MetaStageWatchPhase  watch_mode,
                                        MetaStageWatchFunc   callback,
                                        gpointer             user_data);

META_EXPORT_TEST
void meta_stage_remove_watch (MetaStage      *stage,
                              MetaStageWatch *watch);

G_END_DECLS
