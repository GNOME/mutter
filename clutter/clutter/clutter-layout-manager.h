/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation.
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

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_LAYOUT_MANAGER (clutter_layout_manager_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterLayoutManager,
                          clutter_layout_manager,
                          CLUTTER,
                          LAYOUT_MANAGER,
                          GInitiallyUnowned)

/**
 * ClutterLayoutManagerClass:
 * @get_preferred_width: virtual function; override to provide a preferred
 *   width for the layout manager. See also the get_preferred_width()
 *   virtual function in #ClutterActor
 * @get_preferred_height: virtual function; override to provide a preferred
 *   height for the layout manager. See also the get_preferred_height()
 *   virtual function in #ClutterActor
 * @allocate: virtual function; override to allocate the children of the
 *   layout manager. See also the allocate() virtual function in
 *   #ClutterActor
 * @set_container: virtual function; override to set a back pointer
 *   on the [type@Clutter.Actor] using the layout manager. The implementation
 *   should not take a reference on the container, but just take a weak
 *   reference, to avoid potential leaks due to reference cycles
 * @get_child_meta_type: virtual function; override to return the #GType
 *   of the #ClutterLayoutMeta sub-class used by the #ClutterLayoutManager
 * @create_child_meta: virtual function; override to create a
 *   [type@Clutter.LayoutMeta] instance associated to a container
 *   [type@Clutter.Actor] and a child [type@Clutter.Actor], used to maintain
 *   layout manager specific properties
 * @layout_changed: class handler for the #ClutterLayoutManager::layout-changed
 *   signal
 *
 * The #ClutterLayoutManagerClass structure contains only private
 * data and should be accessed using the provided API
 */
struct _ClutterLayoutManagerClass
{
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/
  void               (* get_preferred_width)    (ClutterLayoutManager   *manager,
                                                 ClutterActor           *container,
                                                 gfloat                  for_height,
                                                 gfloat                 *min_width_p,
                                                 gfloat                 *nat_width_p);
  void               (* get_preferred_height)   (ClutterLayoutManager   *manager,
                                                 ClutterActor           *container,
                                                 gfloat                  for_width,
                                                 gfloat                 *min_height_p,
                                                 gfloat                 *nat_height_p);
  void               (* allocate)               (ClutterLayoutManager   *manager,
                                                 ClutterActor           *container,
                                                 const ClutterActorBox  *allocation);

  void               (* set_container)          (ClutterLayoutManager   *manager,
                                                 ClutterActor           *container);

  GType              (* get_child_meta_type)    (ClutterLayoutManager   *manager);
  ClutterLayoutMeta *(* create_child_meta)      (ClutterLayoutManager   *manager,
                                                 ClutterActor           *container,
                                                 ClutterActor           *actor);

  void               (* layout_changed)         (ClutterLayoutManager   *manager);
};

CLUTTER_EXPORT
void               clutter_layout_manager_get_preferred_width   (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 gfloat                  for_height,
                                                                 gfloat                 *min_width_p,
                                                                 gfloat                 *nat_width_p);
CLUTTER_EXPORT
void               clutter_layout_manager_get_preferred_height  (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 gfloat                  for_width,
                                                                 gfloat                 *min_height_p,
                                                                 gfloat                 *nat_height_p);
CLUTTER_EXPORT
void               clutter_layout_manager_allocate              (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 const ClutterActorBox  *allocation);

CLUTTER_EXPORT
void               clutter_layout_manager_set_container         (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container);
CLUTTER_EXPORT
void               clutter_layout_manager_layout_changed        (ClutterLayoutManager   *manager);

CLUTTER_EXPORT
GParamSpec *       clutter_layout_manager_find_child_property   (ClutterLayoutManager   *manager,
                                                                 const gchar            *name);
CLUTTER_EXPORT
GParamSpec **      clutter_layout_manager_list_child_properties (ClutterLayoutManager   *manager,
                                                                 guint                  *n_pspecs);

CLUTTER_EXPORT
ClutterLayoutMeta *clutter_layout_manager_get_child_meta        (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 ClutterActor           *actor);

CLUTTER_EXPORT
void               clutter_layout_manager_child_set             (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 ClutterActor           *actor,
                                                                 const gchar            *first_property,
                                                                 ...) G_GNUC_NULL_TERMINATED;
CLUTTER_EXPORT
void               clutter_layout_manager_child_get             (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 ClutterActor           *actor,
                                                                 const gchar            *first_property,
                                                                 ...) G_GNUC_NULL_TERMINATED;
CLUTTER_EXPORT
void               clutter_layout_manager_child_set_property    (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 ClutterActor           *actor,
                                                                 const gchar            *property_name,
                                                                 const GValue           *value);
CLUTTER_EXPORT
void               clutter_layout_manager_child_get_property    (ClutterLayoutManager   *manager,
                                                                 ClutterActor           *container,
                                                                 ClutterActor           *actor,
                                                                 const gchar            *property_name,
                                                                 GValue                 *value);

G_END_DECLS
