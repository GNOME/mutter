/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-actor-meta.h"
#include "clutter/clutter-paint-context.h"
#include "clutter/clutter-pick-context.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_EFFECT             (clutter_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterEffect,
                          clutter_effect,
                          CLUTTER,
                          EFFECT,
                          ClutterActorMeta)

/**
 * ClutterEffectClass:
 * @pre_paint: virtual function
 * @post_paint: virtual function
 * @modify_paint_volume: virtual function
 * @paint: virtual function
 * @pick: virtual function
 *
 * The #ClutterEffectClass structure contains only private data
 */
struct _ClutterEffectClass
{
  /*< private >*/
  ClutterActorMetaClass parent_class;

  /*< public >*/
  gboolean (* pre_paint)           (ClutterEffect           *effect,
                                    ClutterPaintNode        *node,
                                    ClutterPaintContext     *paint_context);
  void     (* post_paint)          (ClutterEffect           *effect,
                                    ClutterPaintNode        *node,
                                    ClutterPaintContext     *paint_context);

  gboolean (* modify_paint_volume) (ClutterEffect           *effect,
                                    ClutterPaintVolume      *volume);

  void     (* paint)               (ClutterEffect           *effect,
                                    ClutterPaintNode        *node,
                                    ClutterPaintContext     *paint_context,
                                    ClutterEffectPaintFlags  flags);
  void     (* paint_node)          (ClutterEffect           *effect,
                                    ClutterPaintNode        *node,
                                    ClutterPaintContext     *paint_context,
                                    ClutterEffectPaintFlags  flags);
  void     (* pick)                (ClutterEffect           *effect,
                                    ClutterPickContext      *pick_context);
};

CLUTTER_EXPORT
void    clutter_effect_queue_repaint    (ClutterEffect *effect);

/*
 * ClutterActor API
 */

CLUTTER_EXPORT
void           clutter_actor_add_effect            (ClutterActor  *self,
                                                    ClutterEffect *effect);
CLUTTER_EXPORT
void           clutter_actor_add_effect_with_name  (ClutterActor  *self,
                                                    const gchar   *name,
                                                    ClutterEffect *effect);
CLUTTER_EXPORT
void           clutter_actor_remove_effect         (ClutterActor  *self,
                                                    ClutterEffect *effect);
CLUTTER_EXPORT
void           clutter_actor_remove_effect_by_name (ClutterActor  *self,
                                                    const gchar   *name);
CLUTTER_EXPORT
GList *        clutter_actor_get_effects           (ClutterActor  *self);
CLUTTER_EXPORT
ClutterEffect *clutter_actor_get_effect            (ClutterActor  *self,
                                                    const gchar   *name);
CLUTTER_EXPORT
void           clutter_actor_clear_effects         (ClutterActor  *self);

CLUTTER_EXPORT
gboolean       clutter_actor_has_effects           (ClutterActor  *self);

G_END_DECLS
