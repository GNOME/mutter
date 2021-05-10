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

#ifndef META_STAGE_VIEW_H
#define META_STAGE_VIEW_H

#include <cairo.h>

#include "clutter/clutter-mutter.h"

G_BEGIN_DECLS

#define META_TYPE_STAGE_VIEW (meta_stage_view_get_type ())

G_DECLARE_DERIVABLE_TYPE (MetaStageView,
                          meta_stage_view,
                          META, STAGE_VIEW,
                          ClutterStageView)

struct _MetaStageViewClass
{
  ClutterStageViewClass parent_class;
};

ClutterDamageHistory * meta_stage_view_get_damage_history (MetaStageView *view);
void meta_stage_view_perform_fake_swap (MetaStageView *view,
                                        int64_t        counter);

#endif /* META_STAGE_VIEW_H */
