/*
 * Copyright (C) 2021 Red Hat
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
 */

#include "config.h"

#include "backends/native/meta-kms-cursor-manager.h"

#include <glib.h>

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-thread.h"

typedef struct _MetaKmsCursorManagerImpl
{
  MetaKmsImpl *impl;
  GPtrArray *crtc_states;

  MetaKmsCursorQueryInImpl cursor_query_in_impl_func;
  gpointer cursor_query_in_impl_func_user_data;

  MetaKmsUpdateFilter *update_filter;
} MetaKmsCursorManagerImpl;

typedef struct _CrtcStateImpl
{
  gatomicrefcount ref_count;

  MetaKmsCursorManagerImpl *cursor_manager_impl;

  MetaKmsCrtc *crtc;
  MetaKmsPlane *cursor_plane;
  graphene_rect_t layout;
  float scale;
  MetaMonitorTransform transform;
  MetaDrmBuffer *buffer;
  graphene_point_t hotspot;

  gboolean cursor_invalidated;
  gboolean force_update;
  gboolean has_cursor;

  graphene_point_t pending_hotspot;
  MetaDrmBuffer *pending_buffer;
  MetaDrmBuffer *active_buffer;
  MetaDrmBuffer *presenting_buffer;
} CrtcStateImpl;

struct _MetaKmsCursorManager
{
  GObject parent;

  MetaKms *kms;
};

G_DEFINE_TYPE (MetaKmsCursorManager, meta_kms_cursor_manager,
               G_TYPE_OBJECT)

static GQuark quark_cursor_manager_impl;

static CrtcStateImpl *
find_crtc_state (MetaKmsCursorManagerImpl *cursor_manager_impl,
                 MetaKmsCrtc              *crtc)
{
  int i;

  if (!cursor_manager_impl->crtc_states)
    return NULL;

  for (i = 0; i < cursor_manager_impl->crtc_states->len; i++)
    {
      CrtcStateImpl *crtc_state_impl =
        g_ptr_array_index (cursor_manager_impl->crtc_states, i);

      if (crtc_state_impl->crtc == crtc)
        return crtc_state_impl;
    }

  return NULL;
}

static CrtcStateImpl *
crtc_state_impl_new (MetaKmsCursorManagerImpl *cursor_manager_impl,
                     MetaKmsCrtc              *crtc,
                     MetaKmsPlane             *cursor_plane,
                     graphene_rect_t           layout,
                     float                     scale,
                     MetaDrmBuffer            *buffer)
{
  CrtcStateImpl *crtc_state_impl;

  crtc_state_impl = g_new0 (CrtcStateImpl, 1);
  g_atomic_ref_count_init (&crtc_state_impl->ref_count);
  crtc_state_impl->cursor_manager_impl = cursor_manager_impl;
  crtc_state_impl->crtc = crtc;
  crtc_state_impl->cursor_plane = cursor_plane;
  crtc_state_impl->layout = layout;
  crtc_state_impl->scale = scale;
  crtc_state_impl->buffer = buffer;

  return crtc_state_impl;
}

static CrtcStateImpl *
crtc_state_impl_ref (CrtcStateImpl *crtc_state_impl)
{
  g_atomic_ref_count_inc (&crtc_state_impl->ref_count);
  return crtc_state_impl;
}

static void
crtc_state_impl_unref (CrtcStateImpl *crtc_state_impl)
{
  if (g_atomic_ref_count_dec (&crtc_state_impl->ref_count))
    {
      g_warn_if_fail (!crtc_state_impl->buffer);
      g_warn_if_fail (!crtc_state_impl->pending_buffer);
      g_warn_if_fail (!crtc_state_impl->active_buffer);
      g_warn_if_fail (!crtc_state_impl->presenting_buffer);
      g_free (crtc_state_impl);
    }
}

static void
crtc_state_impl_swap_buffer (CrtcStateImpl  *crtc_state_impl,
                             MetaDrmBuffer **from_buffer_ref,
                             MetaDrmBuffer **to_buffer_ref)
{
  MetaDrmBuffer *buffer_to_release;

  if (*from_buffer_ref == *to_buffer_ref)
    {
      buffer_to_release = g_steal_pointer (from_buffer_ref);
    }
  else
    {
      buffer_to_release = g_steal_pointer (to_buffer_ref);
      *to_buffer_ref = g_steal_pointer (from_buffer_ref);
    }

  if (buffer_to_release)
    {
      MetaKmsDevice *device = meta_kms_crtc_get_device (crtc_state_impl->crtc);
      MetaKms *kms = meta_kms_device_get_kms (device);

      meta_thread_queue_callback (META_THREAD (kms),
                                  NULL, NULL,
                                  buffer_to_release,
                                  g_object_unref);
    }
}

static void
crtc_state_impl_notify_presented (CrtcStateImpl *crtc_state_impl)
{
  crtc_state_impl_swap_buffer (crtc_state_impl,
                               &crtc_state_impl->active_buffer,
                               &crtc_state_impl->presenting_buffer);
}

static void
cursor_page_flip_feedback_flipped (MetaKmsCrtc  *crtc,
                                   unsigned int  sequence,
                                   unsigned int  tv_sec,
                                   unsigned int  tv_usec,
                                   gpointer      user_data)
{
  CrtcStateImpl *crtc_state_impl = user_data;

  crtc_state_impl_notify_presented (crtc_state_impl);
}

static void
cursor_page_flip_feedback_ready (MetaKmsCrtc *crtc,
                                 gpointer     user_data)
{
}

static void
cursor_page_flip_feedback_mode_set_fallback (MetaKmsCrtc *crtc,
                                             gpointer     user_data)
{
  CrtcStateImpl *crtc_state_impl = user_data;

  crtc_state_impl_notify_presented (crtc_state_impl);
}

static void
cursor_page_flip_feedback_discarded (MetaKmsCrtc  *crtc,
                                     gpointer      user_data,
                                     const GError *error)
{
}

static const MetaKmsPageFlipListenerVtable cursor_page_flip_listener_vtable = {
  .flipped = cursor_page_flip_feedback_flipped,
  .ready = cursor_page_flip_feedback_ready,
  .mode_set_fallback = cursor_page_flip_feedback_mode_set_fallback,
  .discarded = cursor_page_flip_feedback_discarded,
};

static void
cursor_result_feedback (const MetaKmsFeedback *feedback,
                        gpointer               user_data)
{
  CrtcStateImpl *crtc_state_impl = user_data;

  switch (meta_kms_feedback_get_result (feedback))
    {
    case META_KMS_FEEDBACK_PASSED:
      break;
    case META_KMS_FEEDBACK_FAILED:
      return;
    }

  crtc_state_impl->cursor_invalidated = FALSE;

  crtc_state_impl_swap_buffer (crtc_state_impl,
                               &crtc_state_impl->pending_buffer,
                               &crtc_state_impl->active_buffer);
}

static const MetaKmsResultListenerVtable cursor_result_listener_vtable = {
  .feedback = cursor_result_feedback,
};

static gboolean
get_current_cursor_position (MetaKmsCursorManagerImpl *cursor_manager_impl,
                             float                    *x,
                             float                    *y)
{
  gpointer user_data;

  if (!cursor_manager_impl->cursor_query_in_impl_func)
    return FALSE;

  user_data = cursor_manager_impl->cursor_query_in_impl_func_user_data;
  cursor_manager_impl->cursor_query_in_impl_func (x, y, user_data);

  return TRUE;
}

static gboolean
calculate_cursor_rect (CrtcStateImpl          *crtc_state_impl,
                       MetaDrmBuffer          *buffer,
                       const graphene_point_t *hotspot,
                       float                   x,
                       float                   y,
                       graphene_rect_t        *out_cursor_rect)
{
  int crtc_x, crtc_y, crtc_width, crtc_height;
  int buffer_width, buffer_height;
  graphene_rect_t cursor_rect;

  crtc_x = (x - crtc_state_impl->layout.origin.x) * crtc_state_impl->scale;
  crtc_y = (y - crtc_state_impl->layout.origin.y) * crtc_state_impl->scale;
  crtc_width = roundf (crtc_state_impl->layout.size.width *
                       crtc_state_impl->scale);
  crtc_height = roundf (crtc_state_impl->layout.size.height *
                        crtc_state_impl->scale);

  meta_monitor_transform_transform_point (crtc_state_impl->transform,
                                          &crtc_width, &crtc_height,
                                          &crtc_x, &crtc_y);

  buffer_width = meta_drm_buffer_get_width (buffer);
  buffer_height = meta_drm_buffer_get_height (buffer);

  cursor_rect = (graphene_rect_t) {
    .origin = {
      .x = crtc_x - hotspot->x,
      .y = crtc_y - hotspot->y,
    },
    .size = {
      .width = buffer_width,
      .height = buffer_height,
    },
  };
  if (cursor_rect.origin.x + cursor_rect.size.width > 0.0 &&
      cursor_rect.origin.y + cursor_rect.size.height > 0.0 &&
      cursor_rect.origin.x < crtc_width &&
      cursor_rect.origin.y < crtc_height)
    {
      if (out_cursor_rect)
        *out_cursor_rect = cursor_rect;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static MetaKmsUpdate *
maybe_update_cursor_plane (MetaKmsCursorManagerImpl  *cursor_manager_impl,
                           MetaKmsCrtc               *crtc,
                           MetaKmsUpdate             *update,
                           MetaDrmBuffer            **old_buffer)
{
  MetaKmsImpl *impl = cursor_manager_impl->impl;
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
  MetaKmsDevice *device;
  CrtcStateImpl *crtc_state_impl;
  float x, y;
  MetaDrmBuffer *buffer;
  const graphene_point_t *hotspot;
  gboolean should_have_cursor;
  gboolean did_have_cursor;
  graphene_rect_t cursor_rect;
  MetaKmsPlane *cursor_plane;

  g_assert (old_buffer && !*old_buffer);

  if (!get_current_cursor_position (cursor_manager_impl, &x, &y))
    return update;

  crtc_state_impl = find_crtc_state (cursor_manager_impl, crtc);
  g_return_val_if_fail (crtc_state_impl, update);

  cursor_plane = crtc_state_impl->cursor_plane;
  if (!cursor_plane)
    return update;

  if (!crtc_state_impl->cursor_invalidated)
    return update;

  device = meta_kms_crtc_get_device (crtc_state_impl->crtc);
  buffer = crtc_state_impl->buffer;
  hotspot = &crtc_state_impl->hotspot;

  if (buffer)
    {
      should_have_cursor = calculate_cursor_rect (crtc_state_impl,
                                                  buffer,
                                                  hotspot,
                                                  x, y,
                                                  &cursor_rect);
    }
  else
    {
      should_have_cursor = FALSE;
    }

  did_have_cursor = crtc_state_impl->has_cursor;
  crtc_state_impl->has_cursor = should_have_cursor;

  if (!should_have_cursor && !did_have_cursor)
    return update;

  if (!update)
    {
      MetaKmsImplDevice *impl_device =
        meta_kms_device_get_impl_device (device);

      update = meta_kms_update_new (device);
      meta_kms_update_realize (update, impl_device);
    }

  if (should_have_cursor)
    {
      int width, height;
      MetaFixed16Rectangle src_rect;
      MtkRectangle dst_rect;
      MetaKmsAssignPlaneFlag assign_plane_flags =
        META_KMS_ASSIGN_PLANE_FLAG_NONE;
      MetaKmsPlaneAssignment *plane_assignment;

      if (crtc_state_impl->pending_buffer != crtc_state_impl->buffer)
        {
          *old_buffer = g_steal_pointer (&crtc_state_impl->pending_buffer);
          crtc_state_impl->pending_buffer = g_object_ref (buffer);
        }
      else
        {
          assign_plane_flags |= META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED;
        }

      width = meta_drm_buffer_get_width (buffer);
      height = meta_drm_buffer_get_height (buffer);

      src_rect = (MetaFixed16Rectangle) {
        .x = meta_fixed_16_from_int (0),
        .y = meta_fixed_16_from_int (0),
        .width = meta_fixed_16_from_int (width),
        .height = meta_fixed_16_from_int (height),
      };
      dst_rect = (MtkRectangle) {
        .x = round (cursor_rect.origin.x),
        .y = round (cursor_rect.origin.y),
        .width = round (cursor_rect.size.width),
        .height = round (cursor_rect.size.height),
      };

      plane_assignment = meta_kms_update_assign_plane (update,
                                                       crtc, cursor_plane,
                                                       buffer,
                                                       src_rect, dst_rect,
                                                       assign_plane_flags);

      if (meta_kms_plane_supports_cursor_hotspot (cursor_plane))
        {
          meta_kms_plane_assignment_set_cursor_hotspot (plane_assignment,
                                                        (int) roundf (hotspot->x),
                                                        (int) roundf (hotspot->y));
        }
    }
  else
    {
      *old_buffer = g_steal_pointer (&crtc_state_impl->pending_buffer);
      meta_kms_update_unassign_plane (update, crtc, cursor_plane);
    }

  meta_kms_update_add_page_flip_listener (update,
                                          crtc,
                                          &cursor_page_flip_listener_vtable,
                                          meta_thread_impl_get_main_context (thread_impl),
                                          crtc_state_impl_ref (crtc_state_impl),
                                          (GDestroyNotify) crtc_state_impl_unref);
  meta_kms_update_add_result_listener (update,
                                       &cursor_result_listener_vtable,
                                       meta_thread_impl_get_main_context (thread_impl),
                                       crtc_state_impl_ref (crtc_state_impl),
                                       (GDestroyNotify) crtc_state_impl_unref);

  return update;
}

static void
free_old_buffers (gpointer      retval,
                  const GError *error,
                  gpointer      user_data)
{
  GList *old_buffers = retval;

  g_list_free_full (old_buffers, g_object_unref);
}

static MetaKmsUpdate *
update_filter_cb (MetaKmsImpl       *impl,
                  MetaKmsCrtc       *crtc,
                  MetaKmsUpdate     *update,
                  MetaKmsUpdateFlag  flags,
                  gpointer           user_data)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
  MetaKmsCursorManagerImpl *cursor_manager_impl = user_data;

  if (flags & META_KMS_UPDATE_FLAG_TEST_ONLY)
    return update;

  if (flags & META_KMS_UPDATE_FLAG_MODE_SET)
    {
      GPtrArray *crtc_states = cursor_manager_impl->crtc_states;
      GList *old_buffers = NULL;
      int i;

      g_return_val_if_fail (crtc_states, update);

      for (i = 0; i < crtc_states->len; i++)
        {
          CrtcStateImpl *crtc_state_impl = g_ptr_array_index (crtc_states, i);
          MetaKmsCrtc *crtc = crtc_state_impl->crtc;
          MetaDrmBuffer *old_buffer = NULL;

          if (meta_kms_crtc_get_device (crtc) !=
              meta_kms_update_get_device (update))
            continue;

          update = maybe_update_cursor_plane (cursor_manager_impl,
                                              crtc, update, &old_buffer);
          if (old_buffer)
            old_buffers = g_list_prepend (old_buffers, old_buffer);
        }

      if (old_buffers)
        {
          meta_thread_queue_callback (meta_thread_impl_get_thread (thread_impl),
                                      g_main_context_default (),
                                      NULL,
                                      old_buffers,
                                      (GDestroyNotify) free_old_buffers);
        }
    }
  else
    {
      MetaDrmBuffer *old_buffer = NULL;

      update = maybe_update_cursor_plane (cursor_manager_impl,
                                          crtc, update, &old_buffer);

      if (old_buffer)
        {
          meta_thread_queue_callback (meta_thread_impl_get_thread (thread_impl),
                                      g_main_context_default (),
                                      NULL,
                                      old_buffer, g_object_unref);
        }
    }

  return update;
}

static void
meta_kms_cursor_manager_impl_free (MetaKmsCursorManagerImpl *cursor_manager_impl)
{
  g_warn_if_fail (!cursor_manager_impl->crtc_states);

  g_free (cursor_manager_impl);
}

static MetaKmsCursorManagerImpl *
ensure_cursor_manager_impl (MetaKmsImpl *impl)
{
  MetaKmsCursorManagerImpl *cursor_manager_impl;

  cursor_manager_impl = g_object_get_qdata (G_OBJECT (impl),
                                            quark_cursor_manager_impl);
  if (!cursor_manager_impl)
    {
      cursor_manager_impl = g_new0 (MetaKmsCursorManagerImpl, 1);
      cursor_manager_impl->impl = impl;

      g_object_set_qdata (G_OBJECT (impl),
                          quark_cursor_manager_impl,
                          cursor_manager_impl);

      cursor_manager_impl->update_filter =
        meta_kms_impl_add_update_filter (impl, update_filter_cb,
                                         cursor_manager_impl);
    }
  return cursor_manager_impl;
}

static gpointer
finalize_in_impl (MetaThreadImpl  *thread_impl,
                  gpointer         user_data,
                  GError         **error)
{
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);
  MetaKmsCursorManagerImpl *cursor_manager_impl;

  cursor_manager_impl = g_object_steal_qdata (G_OBJECT (impl),
                                              quark_cursor_manager_impl);
  if (cursor_manager_impl)
    {
      GPtrArray *crtc_states;

      meta_kms_impl_remove_update_filter (impl,
                                          cursor_manager_impl->update_filter);
      crtc_states = g_steal_pointer (&cursor_manager_impl->crtc_states);
      meta_kms_cursor_manager_impl_free (cursor_manager_impl);
      return crtc_states;
    }
  else
    {
      return NULL;
    }
}

static void
meta_kms_cursor_manager_finalize (GObject *object)
{
  MetaKmsCursorManager *cursor_manager = META_KMS_CURSOR_MANAGER (object);
  GPtrArray *crtc_states;

  crtc_states =
    meta_thread_run_impl_task_sync (META_THREAD (cursor_manager->kms),
                                    finalize_in_impl, NULL, NULL);
  g_clear_pointer (&crtc_states, g_ptr_array_unref);

  G_OBJECT_CLASS (meta_kms_cursor_manager_parent_class)->finalize (object);
}

static void
meta_kms_cursor_manager_class_init (MetaKmsCursorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_cursor_manager_finalize;

  quark_cursor_manager_impl =
    g_quark_from_static_string ("-meta-kms-cursor-manager-quark");
}

static void
meta_kms_cursor_manager_init (MetaKmsCursorManager *cursor_manager)
{
}

MetaKmsCursorManager *
meta_kms_cursor_manager_new (MetaKms *kms)
{
  MetaKmsCursorManager *cursor_manager;

  cursor_manager = g_object_new (META_TYPE_KMS_CURSOR_MANAGER, NULL);
  cursor_manager->kms = kms;

  return cursor_manager;
}

typedef struct
{
  MetaKmsCursorQueryInImpl func;
  gpointer user_data;
} SetQueryFuncData;

static gpointer
set_query_func_in_impl (MetaThreadImpl  *thread_impl,
                        gpointer         user_data,
                        GError         **error)
{
  SetQueryFuncData *data = user_data;
  MetaKmsCursorManagerImpl *cursor_manager_impl =
    ensure_cursor_manager_impl (META_KMS_IMPL (thread_impl));

  cursor_manager_impl->cursor_query_in_impl_func = data->func;
  cursor_manager_impl->cursor_query_in_impl_func_user_data = data->user_data;

  return NULL;
}

void
meta_kms_cursor_manager_set_query_func (MetaKmsCursorManager     *cursor_manager,
                                        MetaKmsCursorQueryInImpl  func,
                                        gpointer                  user_data)
{
  SetQueryFuncData *data;

  data = g_new0 (SetQueryFuncData, 1);
  data->func = func;
  data->user_data = user_data;

  meta_thread_post_impl_task (META_THREAD (cursor_manager->kms),
                              set_query_func_in_impl,
                              data, g_free,
                              NULL, NULL);
}

static gpointer
position_changed_in_impl (MetaThreadImpl  *thread_impl,
                          gpointer         user_data,
                          GError         **error)
{
  MetaKmsCursorManagerImpl *cursor_manager_impl =
    ensure_cursor_manager_impl (META_KMS_IMPL (thread_impl));
  const graphene_point_t *position = user_data;
  GPtrArray *crtc_states;
  int i;

  crtc_states = cursor_manager_impl->crtc_states;
  g_return_val_if_fail (crtc_states, NULL);

  for (i = 0; i < crtc_states->len; i++)
    {
      CrtcStateImpl *crtc_state_impl = g_ptr_array_index (crtc_states, i);
      MetaDrmBuffer *buffer;
      const graphene_point_t *hotspot;
      gboolean did_have_cursor;
      gboolean should_have_cursor;

      buffer = crtc_state_impl->buffer;
      hotspot = &crtc_state_impl->hotspot;

      if (buffer)
        {
          should_have_cursor = calculate_cursor_rect (crtc_state_impl,
                                                      buffer,
                                                      hotspot,
                                                      position->x,
                                                      position->y,
                                                      NULL);
        }
      else
        {
          should_have_cursor = FALSE;
        }

      did_have_cursor = crtc_state_impl->has_cursor;

      if (did_have_cursor != should_have_cursor ||
          should_have_cursor)
        {
          MetaKmsCrtc *crtc = crtc_state_impl->crtc;
          MetaKmsDevice *device = meta_kms_crtc_get_device (crtc);
          MetaKmsImplDevice *impl_device =
            meta_kms_device_get_impl_device (device);

          crtc_state_impl->cursor_invalidated = TRUE;

          meta_kms_impl_device_schedule_process (impl_device,
                                                 crtc_state_impl->crtc);
        }
    }

  return NULL;
}

static graphene_point_t *
copy_point (const graphene_point_t *point)
{
  return graphene_point_init_from_point (graphene_point_alloc (), point);
}

void
meta_kms_cursor_manager_position_changed_in_input_impl (MetaKmsCursorManager   *cursor_manager,
                                                        const graphene_point_t *position)
{
  meta_thread_post_impl_task (META_THREAD (cursor_manager->kms),
                              position_changed_in_impl,
                              copy_point (position),
                              (GDestroyNotify) graphene_point_free,
                              NULL, NULL);
}

typedef struct
{
  MetaKmsCrtc *crtc;
  MetaDrmBuffer *buffer;
  MetaMonitorTransform transform;
  graphene_point_t hotspot;
} UpdateSpriteData;

static gpointer
update_sprite_in_impl (MetaThreadImpl  *thread_impl,
                       gpointer         user_data,
                       GError         **error)
{
  MetaKmsCursorManagerImpl *cursor_manager_impl =
    ensure_cursor_manager_impl (META_KMS_IMPL (thread_impl));
  UpdateSpriteData *data = user_data;
  MetaKmsCrtc *crtc = data->crtc;
  MetaKmsDevice *device = meta_kms_crtc_get_device (crtc);
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (device);
  CrtcStateImpl *crtc_state_impl;
  MetaDrmBuffer *old_buffer;

  crtc_state_impl = find_crtc_state (cursor_manager_impl, crtc);
  g_return_val_if_fail (crtc_state_impl, NULL);

  old_buffer = g_steal_pointer (&crtc_state_impl->buffer);
  crtc_state_impl->buffer = g_steal_pointer (&data->buffer);
  crtc_state_impl->transform = data->transform;
  crtc_state_impl->hotspot = data->hotspot;
  crtc_state_impl->cursor_invalidated = TRUE;

  meta_kms_impl_device_schedule_process (impl_device,
                                         crtc_state_impl->crtc);

  if (old_buffer)
    {
      MetaThread *thread = meta_thread_impl_get_thread (thread_impl);

      meta_thread_queue_callback (thread,
                                  NULL, NULL,
                                  old_buffer, g_object_unref);
    }

  return NULL;
}

void
meta_kms_cursor_manager_update_sprite (MetaKmsCursorManager   *cursor_manager,
                                       MetaKmsCrtc            *crtc,
                                       MetaDrmBuffer          *buffer,
                                       MetaMonitorTransform    transform,
                                       const graphene_point_t *hotspot)
{
  UpdateSpriteData *data;

  data = g_new0 (UpdateSpriteData, 1);
  data->crtc = crtc;
  data->buffer = buffer ? g_object_ref (buffer) : NULL;
  data->transform = transform;
  if (hotspot)
    data->hotspot = *hotspot;

  meta_thread_post_impl_task (META_THREAD (cursor_manager->kms),
                              update_sprite_in_impl,
                              data, g_free,
                              NULL, NULL);
}

static void
crtc_state_impl_clear_in_main (CrtcStateImpl *crtc_state_impl)
{
  g_clear_object (&crtc_state_impl->buffer);
  g_clear_object (&crtc_state_impl->pending_buffer);
  g_clear_object (&crtc_state_impl->active_buffer);
  g_clear_object (&crtc_state_impl->presenting_buffer);
  crtc_state_impl_unref (crtc_state_impl);
}

static void
clear_crtc_states_in_impl (MetaThreadImpl *thread_impl,
                           GPtrArray      *crtc_states)
{
  MetaThread *thread = meta_thread_impl_get_thread (thread_impl);

  meta_thread_queue_callback (thread,
                              NULL, NULL,
                              crtc_states,
                              (GDestroyNotify) g_ptr_array_unref);
}

static gpointer
update_viewports_in_impl (MetaThreadImpl  *thread_impl,
                          gpointer         user_data,
                          GError         **error)
{
  MetaKmsCursorManagerImpl *cursor_manager_impl =
    ensure_cursor_manager_impl (META_KMS_IMPL (thread_impl));
  GArray *layouts = user_data;
  GPtrArray *crtc_states;
  int i;

  crtc_states =
    g_ptr_array_new_full (layouts->len,
                          (GDestroyNotify) crtc_state_impl_clear_in_main);

  for (i = 0; i < layouts->len; i++)
    {
      MetaKmsCrtcLayout *crtc_layout =
        &g_array_index (layouts, MetaKmsCrtcLayout, i);
      CrtcStateImpl *crtc_state_impl;
      CrtcStateImpl *old_crtc_state;

      old_crtc_state = find_crtc_state (cursor_manager_impl, crtc_layout->crtc);
      if (old_crtc_state)
        {
          crtc_state_impl =
            crtc_state_impl_new (cursor_manager_impl,
                                 crtc_layout->crtc,
                                 crtc_layout->cursor_plane,
                                 crtc_layout->layout,
                                 crtc_layout->scale,
                                 g_steal_pointer (&old_crtc_state->buffer));
        }
      else
        {
          crtc_state_impl =
            crtc_state_impl_new (cursor_manager_impl,
                                 crtc_layout->crtc,
                                 crtc_layout->cursor_plane,
                                 crtc_layout->layout,
                                 crtc_layout->scale,
                                 NULL);
        }

      crtc_state_impl->cursor_invalidated = TRUE;
      g_ptr_array_add (crtc_states, crtc_state_impl);
    }

  if (cursor_manager_impl->crtc_states)
    clear_crtc_states_in_impl (thread_impl, cursor_manager_impl->crtc_states);
  cursor_manager_impl->crtc_states = crtc_states;
  return NULL;
}

void
meta_kms_cursor_manager_update_crtc_layout (MetaKmsCursorManager *cursor_manager,
                                            GArray               *layouts)
{
  meta_thread_post_impl_task (META_THREAD (cursor_manager->kms),
                              update_viewports_in_impl,
                              g_array_ref (layouts),
                              (GDestroyNotify) g_array_unref,
                              NULL, NULL);
}
