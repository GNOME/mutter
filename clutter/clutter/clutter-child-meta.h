/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *             Jorn Baayen  <jorn@openedhand.com>
 *             Emmanuele Bassi  <ebassi@openedhand.com>
 *             Tomas Frydrych <tf@openedhand.com>
 *             Øyvind Kolås <ok@openedhand.com>
 *
 * Copyright (C) 2008 OpenedHand
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
 */

#ifndef __CLUTTER_CHILD_META_H__
#define __CLUTTER_CHILD_META_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>
#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_CHILD_META                 (clutter_child_meta_get_type ())
#define CLUTTER_CHILD_META(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_CHILD_META, ClutterChildMeta))
#define CLUTTER_CHILD_META_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_CHILD_META, ClutterChildMetaClass))
#define CLUTTER_IS_CHILD_META(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_CHILD_META))
#define CLUTTER_IS_CHILD_META_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_CHILD_META))
#define CLUTTER_CHILD_META_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_CHILD_META, ClutterChildMetaClass))

typedef struct _ClutterChildMetaClass           ClutterChildMetaClass;

struct _ClutterChildMeta
{
  /*< private >*/
  GObject parent_instance;

  /*< public >*/
  ClutterContainer *container;
  ClutterActor *actor;
};

/**
 * ClutterChildMetaClass:
 *
 * The #ClutterChildMetaClass contains only private data
 */
struct _ClutterChildMetaClass
{
  /*< private >*/
  GObjectClass parent_class;
}; 

CLUTTER_EXPORT
GType clutter_child_meta_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterContainer *      clutter_child_meta_get_container        (ClutterChildMeta *data);
CLUTTER_EXPORT
ClutterActor     *      clutter_child_meta_get_actor            (ClutterChildMeta *data);

G_END_DECLS

#endif /* __CLUTTER_CHILD_META_H__ */
