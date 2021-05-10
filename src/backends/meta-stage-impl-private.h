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

#ifndef META_STAGE_IMPL_PRIVATE_H
#define META_STAGE_IMPL_PRIVATE_H

#include <cairo.h>

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_COGL                  (_clutter_stage_cogl_get_type ())
#define CLUTTER_STAGE_COGL(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_STAGE_COGL, ClutterStageCogl))
#define CLUTTER_IS_STAGE_COGL(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_STAGE_COGL))
#define CLUTTER_STAGE_COGL_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_STAGE_COGL, ClutterStageCoglClass))
#define CLUTTER_IS_STAGE_COGL_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_STAGE_COGL))
#define CLUTTER_STAGE_COGL_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_STAGE_COGL, ClutterStageCoglClass))

typedef struct _ClutterStageCogl         ClutterStageCogl;
typedef struct _ClutterStageCoglClass    ClutterStageCoglClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterStageCogl, g_object_unref)

#define CLUTTER_TYPE_STAGE_VIEW_COGL (clutter_stage_view_cogl_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterStageViewCogl, clutter_stage_view_cogl,
                          CLUTTER, STAGE_VIEW_COGL,
                          ClutterStageView)

struct _ClutterStageViewCoglClass
{
  ClutterStageViewClass parent_class;
};

struct _ClutterStageCogl
{
  GObject parent_instance;

 /* the stage wrapper */
  ClutterStage *wrapper;

  /* back pointer to the backend */
  ClutterBackend *backend;
};

struct _ClutterStageCoglClass
{
  GObjectClass parent_class;
};

CLUTTER_EXPORT
GType _clutter_stage_cogl_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
void _clutter_stage_cogl_presented (ClutterStageCogl *stage_cogl,
                                    CoglFrameEvent    frame_event,
                                    ClutterFrameInfo *frame_info);

CLUTTER_EXPORT
void clutter_stage_cogl_add_onscreen_frame_info (ClutterStageCogl *stage_cogl,
                                                 ClutterStageView *view);

G_END_DECLS

#endif /* META_STAGE_IMPL_PRIVATE_H */
