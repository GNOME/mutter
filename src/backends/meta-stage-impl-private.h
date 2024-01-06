/*
 * Copyright (C) 2007,2008,2009,2010,2011  Intel Corporation.
 * Copyright (C) 2021 Red Hat
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
 * Written by:
 *  Matthew Allum
 *  Robert Bragg
 *  Neil Roberts
 *  Emmanuele Bassi
 *
 */

#pragma once

#include "backends/meta-backend-types.h"
#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_STAGE_IMPL                  (meta_stage_impl_get_type ())
#define META_STAGE_IMPL(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_STAGE_IMPL, MetaStageImpl))
#define META_IS_STAGE_IMPL(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_STAGE_IMPL))
#define META_STAGE_IMPL_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_STAGE_IMPL, MetaStageImplClass))
#define META_IS_STAGE_IMPL_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_STAGE_IMPL))
#define META_STAGE_IMPL_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_STAGE_IMPL, MetaStageImplClass))

typedef struct _MetaStageImpl         MetaStageImpl;
typedef struct _MetaStageImplClass    MetaStageImplClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaStageImpl, g_object_unref)

#define META_TYPE_STAGE_VIEW (meta_stage_view_get_type ())

struct _MetaStageImpl
{
  GObject parent_instance;

 /* the stage wrapper */
  ClutterStage *wrapper;
};

struct _MetaStageImplClass
{
  GObjectClass parent_class;
};

GType meta_stage_impl_get_type (void) G_GNUC_CONST;

MetaBackend * meta_stage_impl_get_backend (MetaStageImpl *stage_impl);

void meta_stage_impl_add_onscreen_frame_info (MetaStageImpl    *stage_impl,
                                              ClutterStageView *view);

G_END_DECLS
