/*
 * Copyright (C) 2023 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Austin Shafer <ashafer@nvidia.com>
 */

/**
 * MetaDrmTimeline
 *
 * MetaDrmTimeline is a helper for handling DRM syncobj operations. It
 * can import DRM syncobjs and export eventfds at a particular point.
 *
 * This is heavily inspired by wlroot's wlr_render_timeline, written by
 * Simon Ser.
 */

#include "config.h"

#include <fcntl.h>
#include <xf86drm.h>
#include <glib/gstdio.h>
#ifdef HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

#include "meta/util.h"
#include "wayland/meta-drm-timeline.h"

enum
{
  PROP_0,

  PROP_DRM_FD,
  PROP_SYNCOBJ_FD,

  N_PROPS
};

typedef struct _MetaDrmTimeline
{
  GObject parent;

  int drm;
  int drm_syncobj_fd;
  uint32_t drm_syncobj;
} MetaDrmTimeline;

static GParamSpec *obj_props[N_PROPS];

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MetaDrmTimeline, meta_drm_timeline, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                      initable_iface_init))

static void
meta_drm_timeline_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  MetaDrmTimeline *timeline = META_DRM_TIMELINE (object);

  switch (prop_id)
    {
    case PROP_DRM_FD:
      g_value_set_int (value, timeline->drm);
      break;
    case PROP_SYNCOBJ_FD:
      g_value_set_int (value, timeline->drm_syncobj_fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_drm_timeline_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaDrmTimeline *timeline = META_DRM_TIMELINE (object);
  int fd;

  switch (prop_id)
    {
    case PROP_DRM_FD:
      fd = g_value_get_int (value);
      timeline->drm = fcntl (fd, F_DUPFD_CLOEXEC, 0);
      break;
    case PROP_SYNCOBJ_FD:
      fd = g_value_get_int (value);
      timeline->drm_syncobj_fd = fcntl (fd, F_DUPFD_CLOEXEC, 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
meta_drm_timeline_initable_init (GInitable     *initable,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  MetaDrmTimeline *timeline = META_DRM_TIMELINE (initable);

  if (drmSyncobjFDToHandle (timeline->drm,
                            timeline->drm_syncobj_fd,
                            &timeline->drm_syncobj) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to import DRM syncobj");
      return FALSE;
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_drm_timeline_initable_init;
}

MetaDrmTimeline *
meta_drm_timeline_import_syncobj (int       fd,
                                  int       drm_syncobj,
                                  GError  **error)
{
  MetaDrmTimeline *timeline = g_initable_new (META_TYPE_DRM_TIMELINE,
                                              NULL, error,
                                              "drm-fd", fd,
                                              "syncobj-fd", drm_syncobj,
                                              NULL);

  return timeline;
}

int
meta_drm_timeline_get_eventfd (MetaDrmTimeline *timeline,
                               uint64_t         sync_point,
                               GError         **error)
{
  g_autofd int fd = -1;

#ifdef HAVE_EVENTFD
  fd = eventfd (0, EFD_CLOEXEC);
  if (fd < 0)
    return -1;

  if (drmSyncobjEventfd (timeline->drm, timeline->drm_syncobj,
                         sync_point, fd, 0) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "DRM_IOCTL_SYNCOBJ_EVENTFD: Failed to export eventfd");
      return -1;
    }
#endif

  return g_steal_fd (&fd);
}

gboolean
meta_drm_timeline_set_sync_point (MetaDrmTimeline *timeline,
                                  uint64_t         sync_point,
                                  int              sync_fd,
                                  GError         **error)
{
  uint32_t tmp;

  /* Import our syncfd at a new release point */
  if (drmSyncobjCreate (timeline->drm, 0, &tmp) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "Failed to create temporary syncobj");
      return FALSE;
    }

  if (drmSyncobjImportSyncFile (timeline->drm, tmp, sync_fd) != 0)
    goto end;

  if (drmSyncobjTransfer (timeline->drm, timeline->drm_syncobj,
                          sync_point, tmp, 0, 0) != 0)
    goto end;

  drmSyncobjDestroy (timeline->drm, tmp);
  return TRUE;

end:
  drmSyncobjDestroy (timeline->drm, tmp);
  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_NOT_SUPPORTED,
               "Failed to import syncfd at specified point");
  return FALSE;
}

static void
meta_drm_timeline_finalize (GObject *object)
{
  MetaDrmTimeline *timeline = META_DRM_TIMELINE (object);

  drmSyncobjDestroy (timeline->drm, timeline->drm_syncobj);
  g_clear_fd (&timeline->drm_syncobj_fd, NULL);
  g_clear_fd (&timeline->drm, NULL);

  G_OBJECT_CLASS (meta_drm_timeline_parent_class)->finalize (object);
}

static void
meta_drm_timeline_init (MetaDrmTimeline *timeline)
{
  timeline->drm = -1;
  timeline->drm_syncobj_fd = -1;
  timeline->drm_syncobj = -1;
}

static void
meta_drm_timeline_class_init (MetaDrmTimelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_drm_timeline_get_property;
  object_class->set_property = meta_drm_timeline_set_property;
  object_class->finalize = meta_drm_timeline_finalize;

  obj_props[PROP_DRM_FD] =
    g_param_spec_int ("drm-fd",
                      NULL,
                      NULL,
                      0, INT_MAX, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);

  obj_props[PROP_SYNCOBJ_FD] =
    g_param_spec_int ("syncobj-fd",
                      NULL,
                      NULL,
                      0, INT_MAX, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
