/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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

#include "cogl/cogl.h"

#include "clutter/clutter-keymap.h"
#include "clutter/clutter-types.h"
#include "clutter/clutter-seat.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BACKEND            (clutter_backend_get_type ())
#define CLUTTER_BACKEND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BACKEND, ClutterBackend))
#define CLUTTER_IS_BACKEND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BACKEND))

typedef struct _ClutterBackend          ClutterBackend;
typedef struct _ClutterBackendClass     ClutterBackendClass;

CLUTTER_EXPORT
GType clutter_backend_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterBackend *                clutter_get_default_backend             (void);

CLUTTER_EXPORT
gdouble                         clutter_backend_get_resolution          (ClutterBackend             *backend);

CLUTTER_EXPORT
CoglContext *                   clutter_backend_get_cogl_context        (ClutterBackend             *backend);

CLUTTER_EXPORT
ClutterInputMethod *            clutter_backend_get_input_method        (ClutterBackend             *backend);

CLUTTER_EXPORT
void                            clutter_backend_set_input_method        (ClutterBackend             *backend,
                                                                         ClutterInputMethod         *method);
CLUTTER_EXPORT
ClutterSeat *                   clutter_backend_get_default_seat        (ClutterBackend             *backend);

CLUTTER_EXPORT
ClutterSprite * clutter_backend_get_sprite (ClutterBackend     *backend,
                                            ClutterStage       *stage,
                                            const ClutterEvent *for_event);

CLUTTER_EXPORT
ClutterSprite * clutter_backend_get_pointer_sprite (ClutterBackend *backend,
                                                    ClutterStage   *stage);

CLUTTER_EXPORT
ClutterKeyFocus * clutter_backend_get_key_focus (ClutterBackend *backend,
                                                 ClutterStage   *stage);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBackend, g_object_unref)

G_END_DECLS
