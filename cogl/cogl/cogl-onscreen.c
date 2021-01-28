/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011, 2013 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#include "cogl-config.h"

#include <gio/gio.h>

#include "cogl-util.h"
#include "cogl-onscreen-private.h"
#include "cogl-frame-info-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-onscreen-template-private.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl1-context.h"
#include "cogl-closure-list-private.h"
#include "cogl-poll-private.h"
#include "cogl-gtype-private.h"

typedef struct _CoglOnscreenPrivate
{
  CoglList frame_closures;

  CoglList dirty_closures;

  int64_t frame_counter;
  int64_t swap_frame_counter; /* frame counter at last all to
                               * cogl_onscreen_swap_region() or
                               * cogl_onscreen_swap_buffers() */
  GQueue pending_frame_infos;
} CoglOnscreenPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CoglOnscreen, cogl_onscreen, COGL_TYPE_FRAMEBUFFER)

static gpointer
cogl_dummy_copy (gpointer data)
{
  return data;
}

static void
cogl_dummy_free (gpointer data)
{
}

COGL_GTYPE_DEFINE_BOXED (FrameClosure, frame_closure,
                         cogl_dummy_copy,
                         cogl_dummy_free);
COGL_GTYPE_DEFINE_BOXED (OnscreenDirtyClosure,
                         onscreen_dirty_closure,
                         cogl_dummy_copy,
                         cogl_dummy_free);

G_DEFINE_QUARK (cogl-scanout-error-quark, cogl_scanout_error)

static gboolean
cogl_onscreen_allocate (CoglFramebuffer  *framebuffer,
                        GError          **error)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  /* If the winsys doesn't support dirty events then we'll report
   * one on allocation so that if the application only paints in
   * response to dirty events then it will at least paint once to
   * start */
  if (!_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_DIRTY_EVENTS))
    _cogl_onscreen_queue_full_dirty (onscreen);

  return TRUE;
}

static gboolean
cogl_onscreen_is_y_flipped (CoglFramebuffer *framebuffer)
{
  return FALSE;
}

static void
cogl_onscreen_init_from_template (CoglOnscreen *onscreen,
                                   CoglOnscreenTemplate *onscreen_template)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);

  _cogl_list_init (&priv->frame_closures);
  _cogl_list_init (&priv->dirty_closures);

  cogl_framebuffer_init_config (framebuffer, &onscreen_template->config);
}

static void
cogl_onscreen_constructed (GObject *object)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (object);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglOnscreenTemplate *onscreen_template;

  onscreen_template = ctx->display->onscreen_template;
  cogl_onscreen_init_from_template (onscreen, onscreen_template);

  G_OBJECT_CLASS (cogl_onscreen_parent_class)->constructed (object);
}

static void
cogl_onscreen_dispose (GObject *object)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (object);
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);
  CoglFrameInfo *frame_info;

  _cogl_closure_list_disconnect_all (&priv->frame_closures);
  _cogl_closure_list_disconnect_all (&priv->dirty_closures);

  while ((frame_info = g_queue_pop_tail (&priv->pending_frame_infos)))
    cogl_object_unref (frame_info);
  g_queue_clear (&priv->pending_frame_infos);

  G_OBJECT_CLASS (cogl_onscreen_parent_class)->dispose (object);
}

static void
cogl_onscreen_init (CoglOnscreen *onscreen)
{
}

static void
cogl_onscreen_class_init (CoglOnscreenClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglFramebufferClass *framebuffer_class = COGL_FRAMEBUFFER_CLASS (klass);

  object_class->constructed = cogl_onscreen_constructed;
  object_class->dispose = cogl_onscreen_dispose;

  framebuffer_class->allocate = cogl_onscreen_allocate;
  framebuffer_class->is_y_flipped = cogl_onscreen_is_y_flipped;
}

static void
notify_event (CoglOnscreen *onscreen,
              CoglFrameEvent event,
              CoglFrameInfo *info)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  _cogl_closure_list_invoke (&priv->frame_closures,
                             CoglFrameCallback,
                             onscreen, event, info);
}

static void
_cogl_dispatch_onscreen_cb (CoglContext *context)
{
  CoglOnscreenEvent *event, *tmp;
  CoglList queue;

  /* Dispatching the event callback may cause another frame to be
   * drawn which in may cause another event to be queued immediately.
   * To make sure this loop will only dispatch one set of events we'll
   * steal the queue and iterate that separately */
  _cogl_list_init (&queue);
  _cogl_list_insert_list (&queue, &context->onscreen_events_queue);
  _cogl_list_init (&context->onscreen_events_queue);

  g_clear_pointer (&context->onscreen_dispatch_idle,
                   _cogl_closure_disconnect);

  _cogl_list_for_each_safe (event, tmp, &queue, link)
    {
      CoglOnscreen *onscreen = event->onscreen;
      CoglFrameInfo *info = event->info;

      notify_event (onscreen, event->type, info);

      g_object_unref (onscreen);
      cogl_object_unref (info);

      g_free (event);
    }

  while (!_cogl_list_empty (&context->onscreen_dirty_queue))
    {
      CoglOnscreenQueuedDirty *qe =
        _cogl_container_of (context->onscreen_dirty_queue.next,
                            CoglOnscreenQueuedDirty,
                            link);
      CoglOnscreen *onscreen = qe->onscreen;
      CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

      _cogl_list_remove (&qe->link);

      _cogl_closure_list_invoke (&priv->dirty_closures,
                                 CoglOnscreenDirtyCallback,
                                 qe->onscreen,
                                 &qe->info);

      g_object_unref (qe->onscreen);

      g_free (qe);
    }
}

static void
_cogl_onscreen_queue_dispatch_idle (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  if (!ctx->onscreen_dispatch_idle)
    {
      ctx->onscreen_dispatch_idle =
        _cogl_poll_renderer_add_idle (ctx->display->renderer,
                                      (CoglIdleCallback)
                                      _cogl_dispatch_onscreen_cb,
                                      ctx,
                                      NULL);
    }
}

void
_cogl_onscreen_queue_dirty (CoglOnscreen *onscreen,
                            const CoglOnscreenDirtyInfo *info)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglOnscreenQueuedDirty *qe = g_new0 (CoglOnscreenQueuedDirty, 1);

  qe->onscreen = g_object_ref (onscreen);
  qe->info = *info;
  _cogl_list_insert (ctx->onscreen_dirty_queue.prev, &qe->link);

  _cogl_onscreen_queue_dispatch_idle (onscreen);
}

void
_cogl_onscreen_queue_full_dirty (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenDirtyInfo info;

  info.x = 0;
  info.y = 0;
  info.width = cogl_framebuffer_get_width (framebuffer);
  info.height = cogl_framebuffer_get_height (framebuffer);

  _cogl_onscreen_queue_dirty (onscreen, &info);
}

void
_cogl_onscreen_queue_event (CoglOnscreen *onscreen,
                            CoglFrameEvent type,
                            CoglFrameInfo *info)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  CoglOnscreenEvent *event = g_new0 (CoglOnscreenEvent, 1);

  event->onscreen = g_object_ref (onscreen);
  event->info = cogl_object_ref (info);
  event->type = type;

  _cogl_list_insert (ctx->onscreen_events_queue.prev, &event->link);

  _cogl_onscreen_queue_dispatch_idle (onscreen);
}

void
cogl_onscreen_bind (CoglOnscreen *onscreen)
{
  COGL_ONSCREEN_GET_CLASS (onscreen)->bind (onscreen);
}

void
cogl_onscreen_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                        const int *rectangles,
                                        int n_rectangles,
                                        CoglFrameInfo *info,
                                        gpointer user_data)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenClass *klass = COGL_ONSCREEN_GET_CLASS (onscreen);

  g_return_if_fail  (COGL_IS_ONSCREEN (framebuffer));

  info->frame_counter = priv->frame_counter;
  g_queue_push_tail (&priv->pending_frame_infos, info);

  _cogl_framebuffer_flush_journal (framebuffer);

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_SYNC_FRAME)))
    cogl_framebuffer_finish (framebuffer);

  klass->swap_buffers_with_damage (onscreen,
                                   rectangles,
                                   n_rectangles,
                                   info,
                                   user_data);

  cogl_framebuffer_discard_buffers (framebuffer,
                                    COGL_BUFFER_BIT_COLOR |
                                    COGL_BUFFER_BIT_DEPTH |
                                    COGL_BUFFER_BIT_STENCIL);

  if (!_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT))
    {
      CoglFrameInfo *info;

      g_warn_if_fail (priv->pending_frame_infos.length == 1);

      info = g_queue_pop_tail (&priv->pending_frame_infos);

      _cogl_onscreen_queue_event (onscreen, COGL_FRAME_EVENT_SYNC, info);
      _cogl_onscreen_queue_event (onscreen, COGL_FRAME_EVENT_COMPLETE, info);

      cogl_object_unref (info);
    }

  priv->frame_counter++;
}

void
cogl_onscreen_swap_buffers (CoglOnscreen  *onscreen,
                            CoglFrameInfo *info,
                            gpointer user_data)
{
  cogl_onscreen_swap_buffers_with_damage (onscreen, NULL, 0, info, user_data);
}

void
cogl_onscreen_swap_region (CoglOnscreen *onscreen,
                           const int *rectangles,
                           int n_rectangles,
                           CoglFrameInfo *info,
                           gpointer user_data)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenClass *klass = COGL_ONSCREEN_GET_CLASS (onscreen);

  g_return_if_fail  (COGL_IS_ONSCREEN (framebuffer));

  info->frame_counter = priv->frame_counter;
  g_queue_push_tail (&priv->pending_frame_infos, info);

  _cogl_framebuffer_flush_journal (framebuffer);

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_SYNC_FRAME)))
    cogl_framebuffer_finish (framebuffer);

  /* This should only be called if the winsys advertises
     COGL_WINSYS_FEATURE_SWAP_REGION */
  g_return_if_fail (klass->swap_region);

  klass->swap_region (onscreen,
                      rectangles,
                      n_rectangles,
                      info,
                      user_data);

  cogl_framebuffer_discard_buffers (framebuffer,
                                    COGL_BUFFER_BIT_COLOR |
                                    COGL_BUFFER_BIT_DEPTH |
                                    COGL_BUFFER_BIT_STENCIL);

  if (!_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT))
    {
      CoglFrameInfo *info;

      g_warn_if_fail (priv->pending_frame_infos.length == 1);

      info = g_queue_pop_tail (&priv->pending_frame_infos);

      _cogl_onscreen_queue_event (onscreen, COGL_FRAME_EVENT_SYNC, info);
      _cogl_onscreen_queue_event (onscreen, COGL_FRAME_EVENT_COMPLETE, info);

      cogl_object_unref (info);
    }

  priv->frame_counter++;
}

int
cogl_onscreen_get_buffer_age (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenClass *klass = COGL_ONSCREEN_GET_CLASS (onscreen);

  g_return_val_if_fail (COGL_IS_ONSCREEN (framebuffer), 0);

  if (!klass->get_buffer_age)
    return 0;

  return klass->get_buffer_age (onscreen);
}

gboolean
cogl_onscreen_direct_scanout (CoglOnscreen   *onscreen,
                              CoglScanout    *scanout,
                              CoglFrameInfo  *info,
                              gpointer        user_data,
                              GError        **error)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenClass *klass = COGL_ONSCREEN_GET_CLASS (onscreen);

  g_warn_if_fail (COGL_IS_ONSCREEN (framebuffer));
  g_warn_if_fail (_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT));

  if (!klass->direct_scanout)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Direct scanout not supported");
      return FALSE;
    }

  info->frame_counter = priv->frame_counter;
  g_queue_push_tail (&priv->pending_frame_infos, info);

  if (!klass->direct_scanout (onscreen,
                              scanout,
                              info,
                              user_data,
                              error))
    {
      g_queue_pop_tail (&priv->pending_frame_infos);
      return FALSE;
    }

  info->flags |= COGL_FRAME_INFO_FLAG_ZERO_COPY;
  priv->frame_counter++;
  return TRUE;
}

void
cogl_onscreen_add_frame_info (CoglOnscreen  *onscreen,
                              CoglFrameInfo *info)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  info->frame_counter = priv->frame_counter;
  g_queue_push_tail (&priv->pending_frame_infos, info);
}

CoglFrameInfo *
cogl_onscreen_peek_head_frame_info (CoglOnscreen *onscreen)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  return g_queue_peek_head (&priv->pending_frame_infos);
}

CoglFrameInfo *
cogl_onscreen_peek_tail_frame_info (CoglOnscreen *onscreen)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  return g_queue_peek_tail (&priv->pending_frame_infos);
}

CoglFrameInfo *
cogl_onscreen_pop_head_frame_info (CoglOnscreen *onscreen)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  return g_queue_pop_head (&priv->pending_frame_infos);
}

CoglFrameClosure *
cogl_onscreen_add_frame_callback (CoglOnscreen *onscreen,
                                  CoglFrameCallback callback,
                                  void *user_data,
                                  CoglUserDataDestroyCallback destroy)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  return _cogl_closure_list_add (&priv->frame_closures,
                                 callback,
                                 user_data,
                                 destroy);
}

void
cogl_onscreen_remove_frame_callback (CoglOnscreen *onscreen,
                                     CoglFrameClosure *closure)
{
  g_return_if_fail (closure);

  _cogl_closure_disconnect (closure);
}

void
_cogl_onscreen_notify_frame_sync (CoglOnscreen *onscreen, CoglFrameInfo *info)
{
  notify_event (onscreen, COGL_FRAME_EVENT_SYNC, info);
}

void
_cogl_onscreen_notify_complete (CoglOnscreen *onscreen, CoglFrameInfo *info)
{
  notify_event (onscreen, COGL_FRAME_EVENT_COMPLETE, info);
}

void
_cogl_framebuffer_winsys_update_size (CoglFramebuffer *framebuffer,
                                      int width, int height)
{
  if (cogl_framebuffer_get_width (framebuffer) == width &&
      cogl_framebuffer_get_height (framebuffer) == height)
    return;

  cogl_framebuffer_update_size (framebuffer, width, height);

  if (!_cogl_has_private_feature (cogl_framebuffer_get_context (framebuffer),
                                  COGL_PRIVATE_FEATURE_DIRTY_EVENTS))
    _cogl_onscreen_queue_full_dirty (COGL_ONSCREEN (framebuffer));
}

CoglOnscreenDirtyClosure *
cogl_onscreen_add_dirty_callback (CoglOnscreen *onscreen,
                                  CoglOnscreenDirtyCallback callback,
                                  void *user_data,
                                  CoglUserDataDestroyCallback destroy)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  return _cogl_closure_list_add (&priv->dirty_closures,
                                 callback,
                                 user_data,
                                 destroy);
}

void
cogl_onscreen_remove_dirty_callback (CoglOnscreen *onscreen,
                                     CoglOnscreenDirtyClosure *closure)
{
  g_return_if_fail (closure);

  _cogl_closure_disconnect (closure);
}

int64_t
cogl_onscreen_get_frame_counter (CoglOnscreen *onscreen)
{
  CoglOnscreenPrivate *priv = cogl_onscreen_get_instance_private (onscreen);

  return priv->frame_counter;
}
