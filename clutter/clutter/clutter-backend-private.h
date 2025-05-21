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
 */

#pragma once

#ifdef HAVE_FONTS
#include <cairo.h>
#endif

#include "clutter/clutter-backend.h"
#include "clutter/clutter-seat.h"
#include "clutter/clutter-stage-window.h"
#include "clutter/clutter-stage.h"

#define CLUTTER_BACKEND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_BACKEND, ClutterBackendClass))
#define CLUTTER_IS_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_BACKEND))
#define CLUTTER_BACKEND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_BACKEND, ClutterBackendClass))

G_BEGIN_DECLS

typedef struct _ClutterBackendPrivate   ClutterBackendPrivate;

struct _ClutterBackend
{
  /*< private >*/
  GObject parent_instance;

  ClutterContext *context;

  CoglRenderer *cogl_renderer;
  CoglDisplay *cogl_display;
  CoglContext *cogl_context;
  GSource *cogl_source;

  CoglOnscreen *dummy_onscreen;

#ifdef HAVE_FONTS
  cairo_font_options_t *font_options;
#endif

  float fallback_resource_scale;

  ClutterStageWindow *stage_window;

  ClutterInputMethod *input_method;
};

struct _ClutterBackendClass
{
  /*< private >*/
  GObjectClass parent_class;

  /* vfuncs */
  ClutterStageWindow *  (* create_stage)       (ClutterBackend  *backend,
                                                ClutterStage    *wrapper,
                                                GError         **error);
  CoglRenderer *        (* get_renderer)       (ClutterBackend  *backend,
                                                GError         **error);
  gboolean              (* create_context)     (ClutterBackend  *backend,
                                                GError         **error);

  ClutterSeat *         (* get_default_seat)   (ClutterBackend *backend);

  gboolean              (* is_display_server)  (ClutterBackend *backend);

  ClutterSprite * (* get_sprite) (ClutterBackend     *backend,
                                  ClutterStage       *stage,
                                  const ClutterEvent *for_event);

  ClutterSprite * (* lookup_sprite) (ClutterBackend       *backend,
                                     ClutterStage         *stage,
                                     ClutterInputDevice   *device,
                                     ClutterEventSequence *sequence);

  ClutterSprite * (* get_pointer_sprite) (ClutterBackend *backend,
                                          ClutterStage   *stage);

  void (* destroy_sprite) (ClutterBackend *backend,
                           ClutterSprite  *sprite);

  gboolean (* foreach_sprite) (ClutterBackend               *backend,
                               ClutterStage                 *stage,
                               ClutterStageInputForeachFunc  func,
                               gpointer                      user_data);

  ClutterKeyFocus * (* get_key_focus) (ClutterBackend *backend,
                                       ClutterStage   *stage);

  /* signals */
  void (* resolution_changed) (ClutterBackend *backend);
};

ClutterStageWindow *    _clutter_backend_create_stage                   (ClutterBackend         *backend,
                                                                         ClutterStage           *wrapper,
                                                                         GError                **error);
gboolean                _clutter_backend_create_context                 (ClutterBackend         *backend,
                                                                         GError                **error);

CLUTTER_EXPORT
ClutterStageWindow *    clutter_backend_get_stage_window                (ClutterBackend         *backend);

CLUTTER_EXPORT
void clutter_backend_set_fallback_resource_scale (ClutterBackend *backend,
                                                  float           fallback_resource_scale);

float clutter_backend_get_fallback_resource_scale (ClutterBackend *backend);

gboolean clutter_backend_is_display_server (ClutterBackend *backend);

gboolean clutter_backend_foreach_sprite (ClutterBackend               *backend,
                                         ClutterStage                 *stage,
                                         ClutterStageInputForeachFunc  func,
                                         gpointer                      user_data);

CLUTTER_EXPORT
void clutter_backend_destroy (ClutterBackend *backend);

G_END_DECLS
