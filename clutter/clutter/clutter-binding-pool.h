/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008  Intel Corporation.
 *
 * Authored By: Emmanuele Bassi <ebassi@linux.intel.com>
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

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "clutter/clutter-event.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BINDING_POOL       (clutter_binding_pool_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterBindingPool,
                      clutter_binding_pool,
                      CLUTTER,
                      BINDING_POOL,
                      GObject)

CLUTTER_EXPORT
ClutterBindingPool *  clutter_binding_pool_new              (const gchar         *name);
CLUTTER_EXPORT
ClutterBindingPool *  clutter_binding_pool_get_for_class    (gpointer             klass);
CLUTTER_EXPORT
ClutterBindingPool *  clutter_binding_pool_find             (const gchar         *name);

CLUTTER_EXPORT
void                  clutter_binding_pool_install_action   (ClutterBindingPool  *pool,
                                                             const gchar         *action_name,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers,
                                                             GCallback            callback,
                                                             gpointer             data,
                                                             GDestroyNotify       notify);
CLUTTER_EXPORT
void                  clutter_binding_pool_install_closure  (ClutterBindingPool  *pool,
                                                             const gchar         *action_name,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers,
                                                             GClosure            *closure);
CLUTTER_EXPORT
void                  clutter_binding_pool_override_action  (ClutterBindingPool  *pool,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers,
                                                             GCallback            callback,
                                                             gpointer             data,
                                                             GDestroyNotify       notify);
CLUTTER_EXPORT
void                  clutter_binding_pool_override_closure (ClutterBindingPool  *pool,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers,
                                                             GClosure            *closure);

CLUTTER_EXPORT
const gchar *         clutter_binding_pool_find_action      (ClutterBindingPool  *pool,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers);
CLUTTER_EXPORT
void                  clutter_binding_pool_remove_action    (ClutterBindingPool  *pool,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers);

CLUTTER_EXPORT
gboolean              clutter_binding_pool_activate         (ClutterBindingPool  *pool,
                                                             guint                key_val,
                                                             ClutterModifierType  modifiers,
                                                             GObject             *gobject);

CLUTTER_EXPORT
void                  clutter_binding_pool_block_action     (ClutterBindingPool  *pool,
                                                             const gchar         *action_name);
CLUTTER_EXPORT
void                  clutter_binding_pool_unblock_action   (ClutterBindingPool  *pool,
                                                             const gchar         *action_name);

G_END_DECLS
