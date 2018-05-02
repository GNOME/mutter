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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_STAGE_H
#define META_STAGE_H

#include <clutter/clutter.h>

#include "meta-cursor.h"
#include <meta/boxes.h>

G_BEGIN_DECLS

#define META_TYPE_STAGE            (meta_stage_get_type ())
#define META_STAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_STAGE, MetaStage))
#define META_STAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_STAGE, MetaStageClass))
#define META_IS_STAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_STAGE))
#define META_IS_STAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_STAGE))
#define META_STAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_STAGE, MetaStageClass))

typedef struct _MetaStage      MetaStage;
typedef struct _MetaStageClass MetaStageClass;
typedef struct _MetaOverlay    MetaOverlay;

struct _MetaStageClass
{
  ClutterStageClass parent_class;
};

struct _MetaStage
{
  ClutterStage parent;
};

GType             meta_stage_get_type                (void) G_GNUC_CONST;

ClutterActor     *meta_stage_new                     (void);

MetaOverlay      *meta_stage_create_cursor_overlay   (MetaStage   *stage);
void              meta_stage_remove_cursor_overlay   (MetaStage   *stage,
						      MetaOverlay *overlay);

void              meta_stage_update_cursor_overlay   (MetaStage   *stage,
                                                      MetaOverlay *overlay,
                                                      CoglTexture *texture,
                                                      ClutterRect *rect);

void meta_stage_set_active (MetaStage *stage,
                            gboolean   is_active);

void meta_stage_update_view_layout (MetaStage *stage);

G_END_DECLS

#endif /* META_STAGE_H */
