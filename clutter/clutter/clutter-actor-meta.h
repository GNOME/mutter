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

#ifndef __CLUTTER_ACTOR_META_H__
#define __CLUTTER_ACTOR_META_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_ACTOR_META (clutter_actor_meta_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterActorMeta, clutter_actor_meta,
                          CLUTTER, ACTOR_META, GInitiallyUnowned);

typedef struct _ClutterActorMetaPrivate ClutterActorMetaPrivate;

/**
 * ClutterActorMetaClass:
 * @set_actor: virtual function, invoked when attaching and detaching
 *   a #ClutterActorMeta instance to a #ClutterActor
 *
 * The #ClutterActorMetaClass structure contains
 * only private data
 *
 * Since: 1.4
 */
struct _ClutterActorMetaClass
{
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/

  /**
   * ClutterActorMetaClass::set_actor:
   * @meta: a #ClutterActorMeta
   * @actor: (allow-none): the actor attached to @meta, or %NULL
   *
   * Virtual function, called when @meta is attached or detached
   * from a #ClutterActor.
   */
  void (* set_actor) (ClutterActorMeta *meta,
                      ClutterActor     *actor);

  void (* set_enabled) (ClutterActorMeta *meta,
                        gboolean          is_enabled);

  /*< private >*/
  void (* _clutter_meta1) (void);
  void (* _clutter_meta2) (void);
  void (* _clutter_meta3) (void);
  void (* _clutter_meta4) (void);
  void (* _clutter_meta5) (void);
  void (* _clutter_meta6) (void);
};

CLUTTER_EXPORT
void            clutter_actor_meta_set_name     (ClutterActorMeta *meta,
                                                 const gchar      *name);
CLUTTER_EXPORT
const gchar *   clutter_actor_meta_get_name     (ClutterActorMeta *meta);
CLUTTER_EXPORT
void            clutter_actor_meta_set_enabled  (ClutterActorMeta *meta,
                                                 gboolean          is_enabled);
CLUTTER_EXPORT
gboolean        clutter_actor_meta_get_enabled  (ClutterActorMeta *meta);

CLUTTER_EXPORT
ClutterActor *  clutter_actor_meta_get_actor    (ClutterActorMeta *meta);

G_END_DECLS

#endif /* __CLUTTER_ACTOR_META_H__ */
