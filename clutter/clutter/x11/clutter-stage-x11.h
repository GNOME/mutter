/* Clutter.
 * An OpenGL based 'interactive canvas' library.
 * Authored By Matthew Allum  <mallum@openedhand.com>
 * Copyright (C) 2006-2007 OpenedHand
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
 *
 */

#ifndef __CLUTTER_STAGE_X11_H__
#define __CLUTTER_STAGE_X11_H__

#include <clutter/clutter-group.h>
#include <clutter/clutter-stage.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "clutter-backend-x11.h"
#include "cogl/clutter-stage-cogl.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_X11                  (_clutter_stage_x11_get_type ())
#define CLUTTER_STAGE_X11(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_STAGE_X11, ClutterStageX11))
#define CLUTTER_IS_STAGE_X11(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_STAGE_X11))
#define CLUTTER_STAGE_X11_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_STAGE_X11, ClutterStageX11Class))
#define CLUTTER_IS_STAGE_X11_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_STAGE_X11))
#define CLUTTER_STAGE_X11_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_STAGE_X11, ClutterStageX11Class))

typedef struct _ClutterStageX11         ClutterStageX11;
typedef struct _ClutterStageX11Class    ClutterStageX11Class;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterStageX11, g_object_unref)

typedef enum
{
  STAGE_X11_WITHDRAWN = 1 << 1
} ClutterStageX11State;

struct _ClutterStageX11
{
  ClutterStageCogl parent_instance;

  CoglOnscreen *onscreen;
  Window xwin;
  gint xwin_width;
  gint xwin_height; /* FIXME target_width / height */

  ClutterStageView *legacy_view;
  GList *legacy_views;

  CoglFrameClosure *frame_closure;

  gchar *title;

  guint clipped_redraws_cool_off;

  ClutterStageX11State wm_state;

  guint is_foreign_xwin       : 1;
  guint is_cursor_visible     : 1;
  guint viewport_initialized  : 1;
  guint accept_focus          : 1;
};

struct _ClutterStageX11Class
{
  ClutterStageCoglClass parent_class;
};

CLUTTER_EXPORT
GType _clutter_stage_x11_get_type (void) G_GNUC_CONST;

void  _clutter_stage_x11_events_device_changed (ClutterStageX11 *stage_x11,
                                                ClutterInputDevice *device,
                                                ClutterDeviceManager *device_manager);

/* Private to subclasses */
void            _clutter_stage_x11_set_user_time                (ClutterStageX11 *stage_x11,
                                                                 guint32          user_time);

G_END_DECLS

#endif /* __CLUTTER_STAGE_H__ */
