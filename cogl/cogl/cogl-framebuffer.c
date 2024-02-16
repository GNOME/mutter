/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#include "config.h"

#include <string.h>

#include "cogl/cogl-debug.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-display-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-template-private.h"
#include "cogl/cogl-clip-stack.h"
#include "cogl/cogl-journal-private.h"
#include "cogl/cogl-pipeline-state-private.h"
#include "cogl/cogl-primitive-private.h"
#include "cogl/cogl-offscreen.h"
#include "cogl/cogl1-context.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-primitives-private.h"
#include "cogl/cogl-trace.h"
#include "cogl/winsys/cogl-winsys-private.h"

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_DRIVER_CONFIG,
  PROP_WIDTH,
  PROP_HEIGHT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  DESTROY,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

#ifdef COGL_ENABLE_DEBUG
static GQuark wire_pipeline_key = 0;
#endif

typedef struct _CoglFramebufferPrivate
{
  CoglContext *context;

  /* The user configuration before allocation... */
  CoglFramebufferConfig config;

  CoglFramebufferDriverConfig driver_config;
  CoglFramebufferDriver *driver;

  int width;
  int height;
  /* Format of the pixels in the framebuffer (including the expected
     premult state) */
  CoglPixelFormat internal_format;
  gboolean allocated;

  CoglMatrixStack *modelview_stack;
  CoglMatrixStack *projection_stack;
  float viewport_x;
  float viewport_y;
  float viewport_width;
  float viewport_height;
  int viewport_age;
  int viewport_age_for_scissor_workaround;

  CoglClipStack *clip_stack;

  gboolean dither_enabled;
  gboolean depth_writing_enabled;
  CoglStereoMode stereo_mode;

  /* We journal the textured rectangles we want to submit to OpenGL so
   * we have an opportunity to batch them together into less draw
   * calls. */
  CoglJournal *journal;

  /* The scene of a given framebuffer may depend on images in other
   * framebuffers... */
  GList *deps;

  /* As part of an optimization for reading-back single pixels from a
   * framebuffer in some simple cases where the geometry is still
   * available in the journal we need to track the bounds of the last
   * region cleared, its color and we need to track when something
   * does in fact draw to that region so it is no longer clear.
   */
  float clear_color_red;
  float clear_color_green;
  float clear_color_blue;
  float clear_color_alpha;
  int clear_clip_x0;
  int clear_clip_y0;
  int clear_clip_x1;
  int clear_clip_y1;
  gboolean clear_clip_dirty;

  int samples_per_pixel;

  /* Whether the depth buffer was enabled for this framebuffer,
 * usually means it needs to be cleared before being reused next.
 */
  gboolean depth_buffer_clear_needed;
} CoglFramebufferPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglFramebuffer, cogl_framebuffer,
                                     G_TYPE_OBJECT)

uint32_t
cogl_framebuffer_error_quark (void)
{
  return g_quark_from_static_string ("cogl-framebuffer-error-quark");
}

gboolean
cogl_is_framebuffer (void *object)
{
  return COGL_IS_FRAMEBUFFER (object);
}

static void
cogl_framebuffer_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, priv->context);
      break;
    case PROP_DRIVER_CONFIG:
      g_value_set_pointer (value, &priv->driver_config);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, priv->width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, priv->height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cogl_framebuffer_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglFramebufferDriverConfig *driver_config;

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;
    case PROP_DRIVER_CONFIG:
      driver_config = g_value_get_pointer (value);
      if (driver_config)
        priv->driver_config = *driver_config;
      break;
    case PROP_WIDTH:
      priv->width = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      priv->height = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cogl_framebuffer_constructed (GObject *object)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  g_assert (priv->context);

  priv->internal_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE;
  priv->viewport_x = 0;
  priv->viewport_y = 0;
  priv->viewport_width = priv->width;
  priv->viewport_height = priv->height;
  priv->viewport_age = 0;
  priv->viewport_age_for_scissor_workaround = -1;
  priv->dither_enabled = TRUE;
  priv->depth_writing_enabled = TRUE;
  priv->depth_buffer_clear_needed = TRUE;

  priv->modelview_stack = cogl_matrix_stack_new (priv->context);
  priv->projection_stack = cogl_matrix_stack_new (priv->context);

  priv->samples_per_pixel = 0;

  priv->clip_stack = NULL;

  priv->journal = _cogl_journal_new (framebuffer);

  /* Ensure we know the framebuffer->clear_color* members can't be
   * referenced for our fast-path read-pixel optimization (see
   * _cogl_journal_try_read_pixel()) until some region of the
   * framebuffer is initialized.
   */
  priv->clear_clip_dirty = TRUE;

  /* XXX: We have to maintain a central list of all framebuffers
   * because at times we need to be able to flush all known journals.
   *
   * Examples where we need to flush all journals are:
   * - because journal entries can reference OpenGL texture
   *   coordinates that may not survive texture-atlas reorganization
   *   so we need the ability to flush those entries.
   * - because although we generally advise against modifying
   *   pipelines after construction we have to handle that possibility
   *   and since pipelines may be referenced in journal entries we
   *   need to be able to flush them before allowing the pipelines to
   *   be changed.
   *
   * Note we don't maintain a list of journals and associate
   * framebuffers with journals by e.g. having a journal->framebuffer
   * reference since that would introduce a circular reference.
   *
   * Note: As a future change to try and remove the need to index all
   * journals it might be possible to defer resolving of OpenGL
   * texture coordinates for rectangle primitives until we come to
   * flush a journal. This would mean for instance that a single
   * rectangle entry in a journal could later be expanded into
   * multiple quad primitives to handle sliced textures but would mean
   * we don't have to worry about retaining references to OpenGL
   * texture coordinates that may later become invalid.
   */
  priv->context->framebuffers = g_list_prepend (priv->context->framebuffers,
                                                framebuffer);
}

void
_cogl_framebuffer_set_internal_format (CoglFramebuffer *framebuffer,
                                       CoglPixelFormat internal_format)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->internal_format = internal_format;
}

CoglPixelFormat
cogl_framebuffer_get_internal_format (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->internal_format;
}

const CoglFramebufferConfig *
cogl_framebuffer_get_config (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return &priv->config;
}

void
cogl_framebuffer_init_config (CoglFramebuffer             *framebuffer,
                              const CoglFramebufferConfig *config)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->config = *config;
  g_object_ref (priv->config.swap_chain);
}

static void
cogl_framebuffer_dispose (GObject *object)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglContext *ctx = priv->context;

  if (priv->journal)
    {
      _cogl_journal_flush (priv->journal);

      g_signal_emit (framebuffer, signals[DESTROY], 0);

      _cogl_fence_cancel_fences_for_framebuffer (framebuffer);
    }

  g_clear_pointer (&priv->clip_stack, _cogl_clip_stack_unref);
  g_clear_object (&priv->modelview_stack);
  g_clear_object (&priv->projection_stack);
  g_clear_object (&priv->journal);

  ctx->framebuffers = g_list_remove (ctx->framebuffers, framebuffer);

  if (ctx->current_draw_buffer == framebuffer)
    ctx->current_draw_buffer = NULL;
  if (ctx->current_read_buffer == framebuffer)
    ctx->current_read_buffer = NULL;

  g_clear_object (&priv->driver);

  G_OBJECT_CLASS (cogl_framebuffer_parent_class)->dispose (object);
}

static void
cogl_framebuffer_init (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->width = -1;
  priv->height = -1;
}

static void
cogl_framebuffer_class_init (CoglFramebufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = cogl_framebuffer_dispose;
  object_class->constructed = cogl_framebuffer_constructed;
  object_class->get_property = cogl_framebuffer_get_property;
  object_class->set_property = cogl_framebuffer_set_property;

  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         COGL_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DRIVER_CONFIG] =
    g_param_spec_pointer ("driver-config", NULL, NULL,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  obj_props[PROP_WIDTH] =
    g_param_spec_int ("width", NULL, NULL,
                      -1, INT_MAX, -1,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT |
                      G_PARAM_STATIC_STRINGS);
  obj_props[PROP_HEIGHT] =
    g_param_spec_int ("height", NULL, NULL,
                      -1, INT_MAX, -1,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT |
                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[DESTROY] =
    g_signal_new (I_("destroy"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}

/* This version of cogl_clear can be used internally as an alternative
 * to avoid flushing the journal or the framebuffer state. This is
 * needed when doing operations that may be called while flushing
 * the journal */
void
_cogl_framebuffer_clear_without_flush4f (CoglFramebuffer *framebuffer,
                                         unsigned long buffers,
                                         float red,
                                         float green,
                                         float blue,
                                         float alpha)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  if (!buffers)
    {
      static gboolean shown = FALSE;

      if (!shown)
        {
	  g_warning ("You should specify at least one auxiliary buffer "
                     "when calling cogl_framebuffer_clear");
        }

      return;
    }

  cogl_framebuffer_driver_clear (priv->driver,
                                 buffers,
                                 red,
                                 green,
                                 blue,
                                 alpha);
}

void
_cogl_framebuffer_mark_clear_clip_dirty (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->clear_clip_dirty = TRUE;
}

void
cogl_framebuffer_set_depth_buffer_clear_needed (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->depth_buffer_clear_needed = TRUE;
}

void
cogl_framebuffer_clear4f (CoglFramebuffer *framebuffer,
                          unsigned long buffers,
                          float red,
                          float green,
                          float blue,
                          float alpha)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglClipStack *clip_stack = _cogl_framebuffer_get_clip_stack (framebuffer);
  gboolean had_depth_and_color_buffer_bits;
  int scissor_x0;
  int scissor_y0;
  int scissor_x1;
  int scissor_y1;

  had_depth_and_color_buffer_bits =
    (buffers & COGL_BUFFER_BIT_DEPTH) &&
    (buffers & COGL_BUFFER_BIT_COLOR);

  if (!priv->depth_buffer_clear_needed &&
      (buffers & COGL_BUFFER_BIT_DEPTH))
    buffers &= ~(COGL_BUFFER_BIT_DEPTH);

  if (buffers == 0)
    return;

  _cogl_clip_stack_get_bounds (clip_stack,
                               &scissor_x0, &scissor_y0,
                               &scissor_x1, &scissor_y1);

  /* NB: the previous clear could have had an arbitrary clip.
   * NB: everything for the last frame might still be in the journal
   *     but we can't assume anything about how each entry was
   *     clipped.
   * NB: Clutter will scissor its pick renders which would mean all
   *     journal entries have a common ClipStack entry, but without
   *     a layering violation Cogl has to explicitly walk the journal
   *     entries to determine if this is the case.
   * NB: We have a software only read-pixel optimization in the
   *     journal that determines the color at a given framebuffer
   *     coordinate for simple scenes without rendering with the GPU.
   *     When Clutter is hitting this fast-path we can expect to
   *     receive calls to clear the framebuffer with an un-flushed
   *     journal.
   * NB: To fully support software based picking for Clutter we
   *     need to be able to reliably detect when the contents of a
   *     journal can be discarded and when we can skip the call to
   *     glClear because it matches the previous clear request.
   */

  /* Note: we don't check for the stencil buffer being cleared here
   * since there isn't any public cogl api to manipulate the stencil
   * buffer.
   *
   * Note: we check for an exact clip match here because
   * 1) a smaller clip could mean existing journal entries may
   *    need to contribute to regions outside the new clear-clip
   * 2) a larger clip would mean we need to issue a real
   *    glClear and we only care about cases avoiding a
   *    glClear.
   *
   * Note: Comparing without an epsilon is considered
   * appropriate here.
   */
  if (had_depth_and_color_buffer_bits &&
      !priv->clear_clip_dirty &&
      priv->clear_color_red == red &&
      priv->clear_color_green == green &&
      priv->clear_color_blue == blue &&
      priv->clear_color_alpha == alpha &&
      scissor_x0 == priv->clear_clip_x0 &&
      scissor_y0 == priv->clear_clip_y0 &&
      scissor_x1 == priv->clear_clip_x1 &&
      scissor_y1 == priv->clear_clip_y1)
    {
      /* NB: We only have to consider the clip state of journal
       * entries if the current clear is clipped since otherwise we
       * know every pixel of the framebuffer is affected by the clear
       * and so all journal entries become redundant and can simply be
       * discarded.
       */
      if (clip_stack)
        {
          /*
           * Note: the function for checking the journal entries is
           * quite strict. It avoids detailed checking of all entry
           * clip_stacks by only checking the details of the first
           * entry and then it only verifies that the remaining
           * entries share the same clip_stack ancestry. This means
           * it's possible for some false negatives here but that will
           * just result in us falling back to a real clear.
           */
          if (_cogl_journal_all_entries_within_bounds (priv->journal,
                                                       scissor_x0, scissor_y0,
                                                       scissor_x1, scissor_y1))
            {
              _cogl_journal_discard (priv->journal);
              goto cleared;
            }
        }
      else
        {
          _cogl_journal_discard (priv->journal);
          goto cleared;
        }
    }

  COGL_NOTE (DRAW, "Clear begin");

  _cogl_framebuffer_flush_journal (framebuffer);

  /* NB: cogl_context_flush_framebuffer_state may disrupt various state (such
   * as the pipeline state) when flushing the clip stack, so should
   * always be done first when preparing to draw. */
  cogl_context_flush_framebuffer_state (context,
                                        framebuffer, framebuffer,
                                        COGL_FRAMEBUFFER_STATE_ALL);

  _cogl_framebuffer_clear_without_flush4f (framebuffer, buffers,
                                           red, green, blue, alpha);

  /* This is a debugging variable used to visually display the quad
   * batches from the journal. It is reset here to increase the
   * chances of getting the same colours for each frame during an
   * animation */
  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_RECTANGLES)) &&
      buffers & COGL_BUFFER_BIT_COLOR)
    {
      priv->context->journal_rectangles_color = 1;
    }

  COGL_NOTE (DRAW, "Clear end");

cleared:

  _cogl_framebuffer_mark_clear_clip_dirty (framebuffer);

  if (buffers & COGL_BUFFER_BIT_DEPTH)
    priv->depth_buffer_clear_needed = FALSE;

  if (had_depth_and_color_buffer_bits)
    {
      /* For our fast-path for reading back a single pixel of simple
       * scenes where the whole frame is in the journal we need to
       * track the cleared color of the framebuffer in case the point
       * read doesn't intersect any of the journal rectangles. */
      priv->clear_clip_dirty = FALSE;
      priv->clear_color_red = red;
      priv->clear_color_green = green;
      priv->clear_color_blue = blue;
      priv->clear_color_alpha = alpha;

      /* NB: A clear may be scissored so we need to track the extents
       * that the clear is applicable too... */
      _cogl_clip_stack_get_bounds (clip_stack,
                                   &priv->clear_clip_x0,
                                   &priv->clear_clip_y0,
                                   &priv->clear_clip_x1,
                                   &priv->clear_clip_y1);
    }
}

/* Note: the 'buffers' and 'color' arguments were switched around on
 * purpose compared to the original cogl_clear API since it was odd
 * that you would be expected to specify a color before even
 * necessarily choosing to clear the color buffer.
 */
void
cogl_framebuffer_clear (CoglFramebuffer *framebuffer,
                        unsigned long buffers,
                        const CoglColor *color)
{
  cogl_framebuffer_clear4f (framebuffer, buffers,
                            cogl_color_get_red (color),
                            cogl_color_get_green (color),
                            cogl_color_get_blue (color),
                            cogl_color_get_alpha (color));
}

/* We will lazily allocate framebuffers if necessary when querying
 * their size/viewport but note we need to be careful in the case of
 * onscreen framebuffers that are instantiated with an initial request
 * size that we don't trigger an allocation when this is queried since
 * that would lead to a recursion when the winsys backend queries this
 * requested size during allocation. */
static void
ensure_size_initialized (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  /* In the case of offscreen framebuffers backed by a texture then
   * until that texture has been allocated we might not know the size
   * of the framebuffer */
  if (priv->width < 0)
    {
      /* Currently we assume the size is always initialized for
       * onscreen framebuffers. */
      g_return_if_fail (COGL_IS_OFFSCREEN (framebuffer));

      /* We also assume the size would have been initialized if the
       * framebuffer were allocated. */
      g_return_if_fail (!priv->allocated);

      cogl_framebuffer_allocate (framebuffer, NULL);
    }
}

void
cogl_framebuffer_update_size (CoglFramebuffer *framebuffer,
                              int              width,
                              int              height)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->width = width;
  priv->height = height;

  cogl_framebuffer_set_viewport (framebuffer, 0, 0, width, height);
}

int
cogl_framebuffer_get_width (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  ensure_size_initialized (framebuffer);
  return priv->width;
}

int
cogl_framebuffer_get_height (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  ensure_size_initialized (framebuffer);
  return priv->height;
}

CoglClipStack *
_cogl_framebuffer_get_clip_stack (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->clip_stack;
}

void
cogl_framebuffer_set_viewport4fv (CoglFramebuffer *framebuffer,
                                  float *viewport)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  if (priv->viewport_x == viewport[0] &&
      priv->viewport_y == viewport[1] &&
      priv->viewport_width == viewport[2] &&
      priv->viewport_height == viewport[3])
    return;

  priv->viewport_x = viewport[0];
  priv->viewport_y = viewport[1];
  priv->viewport_width = viewport[2];
  priv->viewport_height = viewport[3];
  priv->viewport_age++;
}

void
cogl_framebuffer_set_viewport (CoglFramebuffer *framebuffer,
                               float x,
                               float y,
                               float width,
                               float height)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  g_return_if_fail (width > 0 && height > 0);

  if (priv->viewport_x == x &&
      priv->viewport_y == y &&
      priv->viewport_width == width &&
      priv->viewport_height == height)
    return;

  priv->viewport_x = x;
  priv->viewport_y = y;
  priv->viewport_width = width;
  priv->viewport_height = height;
}

float
cogl_framebuffer_get_viewport_x (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->viewport_x;
}

float
cogl_framebuffer_get_viewport_y (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->viewport_y;
}

float
cogl_framebuffer_get_viewport_width (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  ensure_size_initialized (framebuffer);
  return priv->viewport_width;
}

float
cogl_framebuffer_get_viewport_height (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  ensure_size_initialized (framebuffer);
  return priv->viewport_height;
}

void
cogl_framebuffer_get_viewport4f (CoglFramebuffer *framebuffer,
                                 float           *viewport_x,
                                 float           *viewport_y,
                                 float           *viewport_width,
                                 float           *viewport_height)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  ensure_size_initialized (framebuffer);

  *viewport_x = priv->viewport_x;
  *viewport_y = priv->viewport_y;
  *viewport_width = priv->viewport_width;
  *viewport_height = priv->viewport_height;
}

void
cogl_framebuffer_get_viewport4fv (CoglFramebuffer *framebuffer,
                                  float *viewport)
{
  cogl_framebuffer_get_viewport4f (framebuffer,
                                   &viewport[0],
                                   &viewport[1],
                                   &viewport[2],
                                   &viewport[3]);
}

CoglMatrixStack *
_cogl_framebuffer_get_modelview_stack (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->modelview_stack;
}

CoglMatrixStack *
_cogl_framebuffer_get_projection_stack (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->projection_stack;
}

void
_cogl_framebuffer_add_dependency (CoglFramebuffer *framebuffer,
                                  CoglFramebuffer *dependency)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  GList *l;

  for (l = priv->deps; l; l = l->next)
    {
      CoglFramebuffer *existing_dep = l->data;
      if (existing_dep == dependency)
        return;
    }

  /* TODO: generalize the primed-array type structure we e.g. use for
   * g_object_set_qdata_full or for pipeline children as a way to
   * avoid quite a lot of mid-scene micro allocations here... */
  priv->deps =
    g_list_prepend (priv->deps, g_object_ref (dependency));
}

void
_cogl_framebuffer_flush_journal (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  _cogl_journal_flush (priv->journal);
}

void
_cogl_framebuffer_flush_dependency_journals (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  g_list_foreach (priv->deps, (GFunc) _cogl_framebuffer_flush_journal, NULL);
  g_list_free_full (priv->deps, g_object_unref);
  priv->deps = NULL;
}

gboolean
cogl_framebuffer_is_allocated (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->allocated;
}

static gboolean
cogl_framebuffer_init_driver (CoglFramebuffer  *framebuffer,
                              GError          **error)

{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  const CoglDriverVtable *driver_vtable = priv->context->driver_vtable;
  CoglFramebufferDriver *driver;

  driver = driver_vtable->create_framebuffer_driver (priv->context,
                                                     framebuffer,
                                                     &priv->driver_config,
                                                     error);
  if (!driver)
    return FALSE;

  priv->driver = driver;
  return TRUE;
}

gboolean
cogl_framebuffer_allocate (CoglFramebuffer *framebuffer,
                           GError **error)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglFramebufferClass *klass = COGL_FRAMEBUFFER_GET_CLASS (framebuffer);

  if (priv->allocated)
    return TRUE;

  if (!klass->allocate (framebuffer, error))
    return FALSE;

  if (!cogl_framebuffer_init_driver (framebuffer, error))
    return FALSE;

  priv->allocated = TRUE;

  return TRUE;
}

static unsigned long
_cogl_framebuffer_compare_viewport_state (CoglFramebuffer *a,
                                          CoglFramebuffer *b)
{
  CoglFramebufferPrivate *priv_a = cogl_framebuffer_get_instance_private (a);
  CoglFramebufferPrivate *priv_b = cogl_framebuffer_get_instance_private (b);

  if (priv_a->viewport_x != priv_b->viewport_x ||
      priv_a->viewport_y != priv_b->viewport_y ||
      priv_a->viewport_width != priv_b->viewport_width ||
      priv_a->viewport_height != priv_b->viewport_height ||
      /* NB: we render upside down to offscreen framebuffers and that
       * can affect how we setup the GL viewport... */
      G_OBJECT_TYPE (a) != G_OBJECT_TYPE (b))
    return COGL_FRAMEBUFFER_STATE_VIEWPORT;
  else
    return 0;
}

static unsigned long
_cogl_framebuffer_compare_clip_state (CoglFramebuffer *a,
                                      CoglFramebuffer *b)
{
  CoglFramebufferPrivate *priv_a = cogl_framebuffer_get_instance_private (a);
  CoglFramebufferPrivate *priv_b = cogl_framebuffer_get_instance_private (b);

  if (priv_a->clip_stack != priv_b->clip_stack)
    return COGL_FRAMEBUFFER_STATE_CLIP;
  else
    return 0;
}

static unsigned long
_cogl_framebuffer_compare_dither_state (CoglFramebuffer *a,
                                        CoglFramebuffer *b)
{
  CoglFramebufferPrivate *priv_a = cogl_framebuffer_get_instance_private (a);
  CoglFramebufferPrivate *priv_b = cogl_framebuffer_get_instance_private (b);

  return priv_a->dither_enabled != priv_b->dither_enabled ?
    COGL_FRAMEBUFFER_STATE_DITHER : 0;
}

static unsigned long
_cogl_framebuffer_compare_modelview_state (CoglFramebuffer *a,
                                           CoglFramebuffer *b)
{
  /* We always want to flush the modelview state. All this does is set
     the current modelview stack on the context to the framebuffer's
     stack. */
  return COGL_FRAMEBUFFER_STATE_MODELVIEW;
}

static unsigned long
_cogl_framebuffer_compare_projection_state (CoglFramebuffer *a,
                                            CoglFramebuffer *b)
{
  /* We always want to flush the projection state. All this does is
     set the current projection stack on the context to the
     framebuffer's stack. */
  return COGL_FRAMEBUFFER_STATE_PROJECTION;
}

static unsigned long
_cogl_framebuffer_compare_front_face_winding_state (CoglFramebuffer *a,
                                                    CoglFramebuffer *b)
{
  if (G_OBJECT_TYPE (a) != G_OBJECT_TYPE (b))
    return COGL_FRAMEBUFFER_STATE_FRONT_FACE_WINDING;
  else
    return 0;
}

static unsigned long
_cogl_framebuffer_compare_depth_write_state (CoglFramebuffer *a,
                                             CoglFramebuffer *b)
{
  CoglFramebufferPrivate *priv_a = cogl_framebuffer_get_instance_private (a);
  CoglFramebufferPrivate *priv_b = cogl_framebuffer_get_instance_private (b);

  return priv_a->depth_writing_enabled != priv_b->depth_writing_enabled ?
    COGL_FRAMEBUFFER_STATE_DEPTH_WRITE : 0;
}

static unsigned long
_cogl_framebuffer_compare_stereo_mode (CoglFramebuffer *a,
				       CoglFramebuffer *b)
{
  CoglFramebufferPrivate *priv_a = cogl_framebuffer_get_instance_private (a);
  CoglFramebufferPrivate *priv_b = cogl_framebuffer_get_instance_private (b);

  return priv_a->stereo_mode != priv_b->stereo_mode ?
    COGL_FRAMEBUFFER_STATE_STEREO_MODE : 0;
}

unsigned long
_cogl_framebuffer_compare (CoglFramebuffer *a,
                           CoglFramebuffer *b,
                           unsigned long state)
{
  unsigned long differences = 0;
  int bit;

  if (state & COGL_FRAMEBUFFER_STATE_BIND)
    {
      differences |= COGL_FRAMEBUFFER_STATE_BIND;
      state &= ~COGL_FRAMEBUFFER_STATE_BIND;
    }

  COGL_FLAGS_FOREACH_START (&state, 1, bit)
    {
      /* XXX: We considered having an array of callbacks for each state index
       * that we'd call here but decided that this way the compiler is more
       * likely going to be able to in-line the comparison functions and use
       * the index to jump straight to the required code. */
      switch (bit)
        {
        case COGL_FRAMEBUFFER_STATE_INDEX_VIEWPORT:
          differences |=
            _cogl_framebuffer_compare_viewport_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_CLIP:
          differences |= _cogl_framebuffer_compare_clip_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_DITHER:
          differences |= _cogl_framebuffer_compare_dither_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_MODELVIEW:
          differences |=
            _cogl_framebuffer_compare_modelview_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_PROJECTION:
          differences |=
            _cogl_framebuffer_compare_projection_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_FRONT_FACE_WINDING:
          differences |=
            _cogl_framebuffer_compare_front_face_winding_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_DEPTH_WRITE:
          differences |=
            _cogl_framebuffer_compare_depth_write_state (a, b);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_STEREO_MODE:
          differences |=
            _cogl_framebuffer_compare_stereo_mode (a, b);
          break;
        default:
          g_warn_if_reached ();
        }
    }
  COGL_FLAGS_FOREACH_END;

  return differences;
}

void
cogl_context_flush_framebuffer_state (CoglContext          *ctx,
                                      CoglFramebuffer      *draw_buffer,
                                      CoglFramebuffer      *read_buffer,
                                      CoglFramebufferState  state)
{
  ctx->driver_vtable->flush_framebuffer_state (ctx,
                                               draw_buffer,
                                               read_buffer,
                                               state);
}

static void
cogl_framebuffer_query_bits (CoglFramebuffer     *framebuffer,
                             CoglFramebufferBits *bits)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  g_return_if_fail (priv->driver);

  cogl_framebuffer_driver_query_bits (priv->driver, bits);
}

int
cogl_framebuffer_get_red_bits (CoglFramebuffer *framebuffer)
{
  CoglFramebufferBits bits;

  cogl_framebuffer_query_bits (framebuffer, &bits);

  return bits.red;
}

int
cogl_framebuffer_get_green_bits (CoglFramebuffer *framebuffer)
{
  CoglFramebufferBits bits;

  cogl_framebuffer_query_bits (framebuffer, &bits);

  return bits.green;
}

int
cogl_framebuffer_get_blue_bits (CoglFramebuffer *framebuffer)
{
  CoglFramebufferBits bits;

  cogl_framebuffer_query_bits (framebuffer, &bits);

  return bits.blue;
}

int
cogl_framebuffer_get_alpha_bits (CoglFramebuffer *framebuffer)
{
  CoglFramebufferBits bits;

  cogl_framebuffer_query_bits (framebuffer, &bits);

  return bits.alpha;
}

int
cogl_framebuffer_get_depth_bits (CoglFramebuffer *framebuffer)
{
  CoglFramebufferBits bits;

  cogl_framebuffer_query_bits (framebuffer, &bits);

  return bits.depth;
}

gboolean
cogl_framebuffer_get_is_stereo (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->config.stereo_enabled;
}

CoglStereoMode
cogl_framebuffer_get_stereo_mode (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->stereo_mode;
}

void
cogl_framebuffer_set_stereo_mode (CoglFramebuffer *framebuffer,
				  CoglStereoMode   stereo_mode)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  if (priv->stereo_mode == stereo_mode)
    return;

  /* Stereo mode changes don't go through the journal */
  _cogl_framebuffer_flush_journal (framebuffer);

  priv->stereo_mode = stereo_mode;

  if (priv->context->current_draw_buffer == framebuffer)
    priv->context->current_draw_buffer_changes |=
      COGL_FRAMEBUFFER_STATE_STEREO_MODE;
}

gboolean
cogl_framebuffer_get_depth_write_enabled (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->depth_writing_enabled;
}

void
cogl_framebuffer_set_depth_write_enabled (CoglFramebuffer *framebuffer,
                                          gboolean depth_write_enabled)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  if (priv->depth_writing_enabled == depth_write_enabled)
    return;

  /* XXX: Currently depth write changes don't go through the journal */
  _cogl_framebuffer_flush_journal (framebuffer);

  priv->depth_writing_enabled = depth_write_enabled;

  if (priv->context->current_draw_buffer == framebuffer)
    priv->context->current_draw_buffer_changes |=
      COGL_FRAMEBUFFER_STATE_DEPTH_WRITE;
}

gboolean
cogl_framebuffer_get_dither_enabled (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->dither_enabled;
}

void
cogl_framebuffer_set_dither_enabled (CoglFramebuffer *framebuffer,
                                     gboolean dither_enabled)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  if (priv->dither_enabled == dither_enabled)
    return;

  priv->dither_enabled = dither_enabled;
}

int
cogl_framebuffer_get_samples_per_pixel (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  if (priv->allocated)
    return priv->samples_per_pixel;
  else
    return priv->config.samples_per_pixel;
}

void
cogl_framebuffer_set_samples_per_pixel (CoglFramebuffer *framebuffer,
                                        int samples_per_pixel)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  g_return_if_fail (!priv->allocated);

  priv->config.samples_per_pixel = samples_per_pixel;
}

void
cogl_framebuffer_update_samples_per_pixel (CoglFramebuffer *framebuffer,
                                           int              samples_per_pixel)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->samples_per_pixel = samples_per_pixel;
}

void
cogl_framebuffer_resolve_samples (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  cogl_framebuffer_resolve_samples_region (framebuffer,
                                           0, 0,
                                           priv->width,
                                           priv->height);

  /* TODO: Make this happen implicitly when the resolve texture next gets used
   * as a source, either via cogl_texture_get_data(), via cogl_read_pixels() or
   * if used as a source for rendering. We would also implicitly resolve if
   * necessary before freeing a CoglFramebuffer.
   *
   * This API should still be kept but it is optional, only necessary
   * if the user wants to explicitly control when the resolve happens e.g.
   * to ensure it's done in advance of it being used as a source.
   *
   * Every texture should have a CoglFramebuffer *needs_resolve member
   * internally. When the texture gets validated before being used as a source
   * we should first check the needs_resolve pointer and if set we'll
   * automatically call cogl_framebuffer_resolve_samples ().
   *
   * Calling cogl_framebuffer_resolve_samples() or
   * cogl_framebuffer_resolve_samples_region() should reset the textures
   * needs_resolve pointer to NULL.
   *
   * Rendering anything to a framebuffer will cause the corresponding
   * texture's ->needs_resolve pointer to be set.
   *
   * XXX: Note: we only need to address this TODO item when adding support for
   * EXT_framebuffer_multisample because currently we only support hardware
   * that resolves implicitly anyway.
   */
}

void
cogl_framebuffer_resolve_samples_region (CoglFramebuffer *framebuffer,
                                         int x,
                                         int y,
                                         int width,
                                         int height)
{
  /* NOP for now since we don't support EXT_framebuffer_multisample yet which
   * requires an explicit resolve. */
}

CoglContext *
cogl_framebuffer_get_context (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  g_return_val_if_fail (framebuffer != NULL, NULL);

  return priv->context;
}

CoglJournal *
cogl_framebuffer_get_journal (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->journal;
}

static gboolean
_cogl_framebuffer_try_fast_read_pixel (CoglFramebuffer *framebuffer,
                                       int x,
                                       int y,
                                       CoglReadPixelsFlags source,
                                       CoglBitmap *bitmap)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  gboolean found_intersection;
  CoglPixelFormat format;

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_DISABLE_FAST_READ_PIXEL)))
    return FALSE;

  if (source != COGL_READ_PIXELS_COLOR_BUFFER)
    return FALSE;

  format = cogl_bitmap_get_format (bitmap);

  if (format != COGL_PIXEL_FORMAT_RGBA_8888_PRE &&
      format != COGL_PIXEL_FORMAT_RGBA_8888)
    return FALSE;

  if (!_cogl_journal_try_read_pixel (priv->journal,
                                     x, y, bitmap,
                                     &found_intersection))
    return FALSE;

  /* If we can't determine the color from the primitives in the
   * journal then see if we can use the last recorded clear color
   */

  /* If _cogl_journal_try_read_pixel() failed even though there was an
   * intersection of the given point with a primitive in the journal
   * then we can't fallback to the framebuffer's last clear color...
   * */
  if (found_intersection)
    return TRUE;

  /* If the framebuffer has been rendered too since it was last
   * cleared then we can't return the last known clear color. */
  if (priv->clear_clip_dirty)
    return FALSE;

  if (x >= priv->clear_clip_x0 &&
      x < priv->clear_clip_x1 &&
      y >= priv->clear_clip_y0 &&
      y < priv->clear_clip_y1)
    {
      uint8_t *pixel;
      GError *ignore_error = NULL;

      /* we currently only care about cases where the premultiplied or
       * unpremultipled colors are equivalent... */
      if (priv->clear_color_alpha != 1.0)
        return FALSE;

      pixel = _cogl_bitmap_map (bitmap,
                                COGL_BUFFER_ACCESS_WRITE,
                                COGL_BUFFER_MAP_HINT_DISCARD,
                                &ignore_error);
      if (pixel == NULL)
        {
          g_error_free (ignore_error);
          return FALSE;
        }

      pixel[0] = priv->clear_color_red * 255.0;
      pixel[1] = priv->clear_color_green * 255.0;
      pixel[2] = priv->clear_color_blue * 255.0;
      pixel[3] = priv->clear_color_alpha * 255.0;

      _cogl_bitmap_unmap (bitmap);

      return TRUE;
    }

  return FALSE;
}

gboolean
_cogl_framebuffer_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                           int x,
                                           int y,
                                           CoglReadPixelsFlags source,
                                           CoglBitmap *bitmap,
                                           GError **error)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  int width;
  int height;

  g_return_val_if_fail (source & COGL_READ_PIXELS_COLOR_BUFFER, FALSE);
  g_return_val_if_fail (cogl_is_framebuffer (framebuffer), FALSE);

  if (!cogl_framebuffer_allocate (framebuffer, error))
    return FALSE;

  width = cogl_bitmap_get_width (bitmap);
  height = cogl_bitmap_get_height (bitmap);

  if (width == 1 && height == 1 && !priv->clear_clip_dirty)
    {
      /* If everything drawn so far for this frame is still in the
       * Journal then if all of the rectangles only have a flat
       * opaque color we have a fast-path for reading a single pixel
       * that avoids the relatively high cost of flushing primitives
       * to be drawn on the GPU (considering how simple the geometry
       * is in this case) and then blocking on the long GPU pipelines
       * for the result.
       */
      if (_cogl_framebuffer_try_fast_read_pixel (framebuffer,
                                                 x, y, source, bitmap))
        return TRUE;
    }

  /* make sure any batched primitives get emitted to the driver
   * before issuing our read pixels...
   */
  _cogl_framebuffer_flush_journal (framebuffer);

  return cogl_framebuffer_driver_read_pixels_into_bitmap (priv->driver,
                                                          x, y,
                                                          source,
                                                          bitmap,
                                                          error);
}

gboolean
cogl_framebuffer_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                          int x,
                                          int y,
                                          CoglReadPixelsFlags source,
                                          CoglBitmap *bitmap)
{
  GError *ignore_error = NULL;
  gboolean status =
    _cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                               x, y, source, bitmap,
                                               &ignore_error);
  g_clear_error (&ignore_error);
  return status;
}

gboolean
cogl_framebuffer_read_pixels (CoglFramebuffer *framebuffer,
                              int x,
                              int y,
                              int width,
                              int height,
                              CoglPixelFormat format,
                              uint8_t *pixels)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  int bpp;
  CoglBitmap *bitmap;
  gboolean ret;

  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);
  bitmap = cogl_bitmap_new_for_data (priv->context,
                                     width, height,
                                     format,
                                     bpp * width, /* rowstride */
                                     pixels);

  /* Note: we don't try and catch errors here since we created the
   * bitmap storage up-front and can assume we won't hit an
   * out-of-memory error which should be the only exception
   * this api throws.
   */
  ret = _cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                                   x, y,
                                                   COGL_READ_PIXELS_COLOR_BUFFER,
                                                   bitmap,
                                                   NULL);
  g_object_unref (bitmap);

  return ret;
}

gboolean
cogl_framebuffer_is_y_flipped (CoglFramebuffer *framebuffer)
{
  return COGL_FRAMEBUFFER_GET_CLASS (framebuffer)->is_y_flipped (framebuffer);
}

gboolean
cogl_blit_framebuffer (CoglFramebuffer *framebuffer,
                       CoglFramebuffer *dst,
                       int src_x,
                       int src_y,
                       int dst_x,
                       int dst_y,
                       int width,
                       int height,
                       GError **error)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglFramebufferPrivate *dst_priv =
    cogl_framebuffer_get_instance_private (dst);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  int src_x1, src_y1, src_x2, src_y2;
  int dst_x1, dst_y1, dst_x2, dst_y2;

  if (!cogl_has_feature (ctx, COGL_FEATURE_ID_BLIT_FRAMEBUFFER))
    {
      g_set_error_literal (error, COGL_SYSTEM_ERROR,
                           COGL_SYSTEM_ERROR_UNSUPPORTED,
                           "Cogl BLIT_FRAMEBUFFER is not supported by the system.");
      return FALSE;
    }

  /* The buffers must use the same premult convention */
  if (((priv->internal_format & COGL_PREMULT_BIT) !=
       (dst_priv->internal_format & COGL_PREMULT_BIT)) &&
      dst_priv->internal_format & COGL_A_BIT)
    {
      g_set_error_literal (error, COGL_SYSTEM_ERROR,
                           COGL_SYSTEM_ERROR_UNSUPPORTED,
                           "cogl_blit_framebuffer premult mismatch.");
      return FALSE;
    }

  /* Make sure any batched primitives get submitted to the driver
   * before blitting
   */
  _cogl_framebuffer_flush_journal (framebuffer);

  /* Make sure the current framebuffers are bound. We explicitly avoid
     flushing the clip state so we can bind our own empty state */
  cogl_context_flush_framebuffer_state (ctx,
                                        dst,
                                        framebuffer,
                                        (COGL_FRAMEBUFFER_STATE_ALL &
                                         ~COGL_FRAMEBUFFER_STATE_CLIP));

  /* Flush any empty clip stack because glBlitFramebuffer is affected
     by the scissor and we want to hide this feature for the Cogl API
     because it's not obvious to an app how the clip state will affect
     the scissor */
  _cogl_clip_stack_flush (NULL, dst);

  /* XXX: Because we are manually flushing clip state here we need to
   * make sure that the clip state gets updated the next time we flush
   * framebuffer state by marking the current framebuffer's clip state
   * as changed */
  ctx->current_draw_buffer_changes |= COGL_FRAMEBUFFER_STATE_CLIP;

  /* Offscreens we do the normal way, onscreens need an y-flip. Even if
   * we consider offscreens to be rendered upside-down, the offscreen
   * orientation is in this function's API. */
  if (cogl_framebuffer_is_y_flipped (framebuffer))
    {
      src_x1 = src_x;
      src_y1 = src_y;
      src_x2 = src_x + width;
      src_y2 = src_y + height;
    }
  else
    {
      src_x1 = src_x;
      src_y1 = cogl_framebuffer_get_height (framebuffer) - src_y;
      src_x2 = src_x + width;
      src_y2 = src_y1 - height;
    }

  if (cogl_framebuffer_is_y_flipped (dst))
    {
      dst_x1 = dst_x;
      dst_y1 = dst_y;
      dst_x2 = dst_x + width;
      dst_y2 = dst_y + height;
    }
  else
    {
      dst_x1 = dst_x;
      dst_y1 = cogl_framebuffer_get_height (dst) - dst_y;
      dst_x2 = dst_x + width;
      dst_y2 = dst_y1 - height;
    }

  ctx->glBlitFramebuffer (src_x1, src_y1, src_x2, src_y2,
                          dst_x1, dst_y1, dst_x2, dst_y2,
                          GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);

  return TRUE;
}

void
cogl_framebuffer_discard_buffers (CoglFramebuffer *framebuffer,
                                  unsigned long buffers)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  cogl_framebuffer_driver_discard_buffers (priv->driver, buffers);
}

void
cogl_framebuffer_finish (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  COGL_TRACE_BEGIN_SCOPED (Finish, "Cogl::Framebuffer::finish()");

  _cogl_framebuffer_flush_journal (framebuffer);

  cogl_framebuffer_driver_finish (priv->driver);
}

void
cogl_framebuffer_flush (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  COGL_TRACE_BEGIN_SCOPED (Flush, "Cogl::Framebuffer::flush()");

  _cogl_framebuffer_flush_journal (framebuffer);

  cogl_framebuffer_driver_flush (priv->driver);
}

void
cogl_framebuffer_push_matrix (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_push (modelview_stack);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_pop_matrix (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_pop (modelview_stack);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_identity_matrix (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_load_identity (modelview_stack);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_scale (CoglFramebuffer *framebuffer,
                        float x,
                        float y,
                        float z)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_scale (modelview_stack, x, y, z);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_translate (CoglFramebuffer *framebuffer,
                            float x,
                            float y,
                            float z)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_translate (modelview_stack, x, y, z);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_rotate (CoglFramebuffer *framebuffer,
                         float angle,
                         float x,
                         float y,
                         float z)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_rotate (modelview_stack, angle, x, y, z);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_rotate_euler (CoglFramebuffer *framebuffer,
                               const graphene_euler_t *euler)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_rotate_euler (modelview_stack, euler);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_transform (CoglFramebuffer         *framebuffer,
                            const graphene_matrix_t *matrix)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_multiply (modelview_stack, matrix);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_perspective (CoglFramebuffer *framebuffer,
                              float fov_y,
                              float aspect,
                              float z_near,
                              float z_far)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  float ymax = z_near * tanf (fov_y * G_PI / 360.0);

  cogl_framebuffer_frustum (framebuffer,
                            -ymax * aspect,  /* left */
                            ymax * aspect,   /* right */
                            -ymax,           /* bottom */
                            ymax,            /* top */
                            z_near,
                            z_far);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_PROJECTION;
    }
}

void
cogl_framebuffer_frustum (CoglFramebuffer *framebuffer,
                          float left,
                          float right,
                          float bottom,
                          float top,
                          float z_near,
                          float z_far)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);

  /* XXX: The projection matrix isn't currently tracked in the journal
   * so we need to flush all journaled primitives first... */
  _cogl_framebuffer_flush_journal (framebuffer);

  cogl_matrix_stack_load_identity (projection_stack);

  cogl_matrix_stack_frustum (projection_stack,
                             left,
                             right,
                             bottom,
                             top,
                             z_near,
                             z_far);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_PROJECTION;
    }
}

void
cogl_framebuffer_orthographic (CoglFramebuffer *framebuffer,
                               float x_1,
                               float y_1,
                               float x_2,
                               float y_2,
                               float near,
                               float far)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  graphene_matrix_t ortho;
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);

  /* XXX: The projection matrix isn't currently tracked in the journal
   * so we need to flush all journaled primitives first... */
  _cogl_framebuffer_flush_journal (framebuffer);

  graphene_matrix_init_ortho (&ortho, x_1, x_2, y_2, y_1, near, far);
  cogl_matrix_stack_set (projection_stack, &ortho);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_PROJECTION;
    }
}

void
cogl_framebuffer_get_modelview_matrix (CoglFramebuffer   *framebuffer,
                                       graphene_matrix_t *matrix)
{
  CoglMatrixEntry *modelview_entry =
    _cogl_framebuffer_get_modelview_entry (framebuffer);

  cogl_matrix_entry_get (modelview_entry, matrix);
}

void
cogl_framebuffer_set_modelview_matrix (CoglFramebuffer         *framebuffer,
                                       const graphene_matrix_t *matrix)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  cogl_matrix_stack_set (modelview_stack, matrix);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_MODELVIEW;
    }
}

void
cogl_framebuffer_get_projection_matrix (CoglFramebuffer   *framebuffer,
                                        graphene_matrix_t *matrix)
{
  CoglMatrixEntry *projection_entry =
    _cogl_framebuffer_get_projection_entry (framebuffer);

  cogl_matrix_entry_get (projection_entry, matrix);
}

void
cogl_framebuffer_set_projection_matrix (CoglFramebuffer         *framebuffer,
                                        const graphene_matrix_t *matrix)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);

  /* XXX: The projection matrix isn't currently tracked in the journal
   * so we need to flush all journaled primitives first... */
  _cogl_framebuffer_flush_journal (framebuffer);

  cogl_matrix_stack_set (projection_stack, matrix);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_PROJECTION;
    }
}

void
cogl_framebuffer_push_rectangle_clip (CoglFramebuffer *framebuffer,
                                      float x_1,
                                      float y_1,
                                      float x_2,
                                      float y_2)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixEntry *modelview_entry =
    _cogl_framebuffer_get_modelview_entry (framebuffer);
  CoglMatrixEntry *projection_entry =
    _cogl_framebuffer_get_projection_entry (framebuffer);
  /* XXX: It would be nicer if we stored the private viewport as a
   * vec4 so we could avoid this redundant copy. */
  float viewport[] = {
    priv->viewport_x,
    priv->viewport_y,
    priv->viewport_width,
    priv->viewport_height
  };

  priv->clip_stack =
    _cogl_clip_stack_push_rectangle (priv->clip_stack,
                                     x_1, y_1, x_2, y_2,
                                     modelview_entry,
                                     projection_entry,
                                     viewport);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_CLIP;
    }
}

void
cogl_framebuffer_push_primitive_clip (CoglFramebuffer *framebuffer,
                                      CoglPrimitive *primitive,
                                      float bounds_x1,
                                      float bounds_y1,
                                      float bounds_x2,
                                      float bounds_y2)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  CoglMatrixEntry *modelview_entry =
    _cogl_framebuffer_get_modelview_entry (framebuffer);
  CoglMatrixEntry *projection_entry =
    _cogl_framebuffer_get_projection_entry (framebuffer);
  /* XXX: It would be nicer if we stored the private viewport as a
   * vec4 so we could avoid this redundant copy. */
  float viewport[] = {
    priv->viewport_x,
    priv->viewport_y,
    priv->viewport_width,
    priv->viewport_height
  };

  priv->clip_stack =
    _cogl_clip_stack_push_primitive (priv->clip_stack,
                                     primitive,
                                     bounds_x1, bounds_y1,
                                     bounds_x2, bounds_y2,
                                     modelview_entry,
                                     projection_entry,
                                     viewport);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_CLIP;
    }
}

void
cogl_framebuffer_push_region_clip (CoglFramebuffer *framebuffer,
                                   MtkRegion       *region)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->clip_stack =
    cogl_clip_stack_push_region (priv->clip_stack,
                                 region);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_CLIP;
    }
}

void
cogl_framebuffer_pop_clip (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  priv->clip_stack = _cogl_clip_stack_pop (priv->clip_stack);

  if (priv->context->current_draw_buffer == framebuffer)
    {
      priv->context->current_draw_buffer_changes |=
        COGL_FRAMEBUFFER_STATE_CLIP;
    }
}

#ifdef COGL_ENABLE_DEBUG
static int
get_index (void *indices,
           CoglIndicesType type,
           int _index)
{
  if (!indices)
    return _index;

  switch (type)
    {
    case COGL_INDICES_TYPE_UNSIGNED_BYTE:
      return ((uint8_t *)indices)[_index];
    case COGL_INDICES_TYPE_UNSIGNED_SHORT:
      return ((uint16_t *)indices)[_index];
    case COGL_INDICES_TYPE_UNSIGNED_INT:
      return ((uint32_t *)indices)[_index];
    }

  g_return_val_if_reached (0);
}

static void
add_line (uint32_t *line_indices,
          int base,
          void *user_indices,
          CoglIndicesType user_indices_type,
          int index0,
          int index1,
          int *pos)
{
  index0 = get_index (user_indices, user_indices_type, index0);
  index1 = get_index (user_indices, user_indices_type, index1);

  line_indices[(*pos)++] = base + index0;
  line_indices[(*pos)++] = base + index1;
}

static int
get_line_count (CoglVerticesMode mode, int n_vertices)
{
  if (mode == COGL_VERTICES_MODE_TRIANGLES &&
      (n_vertices % 3) == 0)
    {
      return n_vertices;
    }
  else if (mode == COGL_VERTICES_MODE_TRIANGLE_FAN &&
           n_vertices >= 3)
    {
      return 2 * n_vertices - 3;
    }
  else if (mode == COGL_VERTICES_MODE_TRIANGLE_STRIP &&
           n_vertices >= 3)
    {
      return 2 * n_vertices - 3;
    }
    /* In the journal we are a bit sneaky and actually use GL_QUADS
     * which isn't actually a valid CoglVerticesMode! */
#ifdef HAVE_GL
  else if (mode == GL_QUADS && (n_vertices % 4) == 0)
    {
      return n_vertices;
    }
#endif

  g_return_val_if_reached (0);
}

static CoglIndices *
get_wire_line_indices (CoglContext *ctx,
                       CoglVerticesMode mode,
                       int first_vertex,
                       int n_vertices_in,
                       CoglIndices *user_indices,
                       int *n_indices)
{
  int n_lines;
  uint32_t *line_indices;
  CoglIndexBuffer *index_buffer;
  void *indices;
  CoglIndicesType indices_type;
  int base = first_vertex;
  int pos;
  int i;
  CoglIndices *ret;

  if (user_indices)
    {
      index_buffer = cogl_indices_get_buffer (user_indices);
      indices = _cogl_buffer_map (COGL_BUFFER (index_buffer),
                                  COGL_BUFFER_ACCESS_READ, 0,
                                  NULL);
      indices_type = cogl_indices_get_indices_type (user_indices);
    }
  else
    {
      index_buffer = NULL;
      indices = NULL;
      indices_type = COGL_INDICES_TYPE_UNSIGNED_BYTE;
    }

  n_lines = get_line_count (mode, n_vertices_in);

  /* Note: we are using COGL_INDICES_TYPE_UNSIGNED_INT so 4 bytes per index. */
  line_indices = g_malloc (4 * n_lines * 2);

  pos = 0;

  if (mode == COGL_VERTICES_MODE_TRIANGLES &&
      (n_vertices_in % 3) == 0)
    {
      for (i = 0; i < n_vertices_in; i += 3)
        {
          add_line (line_indices, base, indices, indices_type, i,   i+1, &pos);
          add_line (line_indices, base, indices, indices_type, i+1, i+2, &pos);
          add_line (line_indices, base, indices, indices_type, i+2, i,   &pos);
        }
    }
  else if (mode == COGL_VERTICES_MODE_TRIANGLE_FAN &&
           n_vertices_in >= 3)
    {
      add_line (line_indices, base, indices, indices_type, 0, 1, &pos);
      add_line (line_indices, base, indices, indices_type, 1, 2, &pos);
      add_line (line_indices, base, indices, indices_type, 0, 2, &pos);

      for (i = 3; i < n_vertices_in; i++)
        {
          add_line (line_indices, base, indices, indices_type, i - 1, i, &pos);
          add_line (line_indices, base, indices, indices_type, 0,     i, &pos);
        }
    }
  else if (mode == COGL_VERTICES_MODE_TRIANGLE_STRIP &&
           n_vertices_in >= 3)
    {
      add_line (line_indices, base, indices, indices_type, 0, 1, &pos);
      add_line (line_indices, base, indices, indices_type, 1, 2, &pos);
      add_line (line_indices, base, indices, indices_type, 0, 2, &pos);

      for (i = 3; i < n_vertices_in; i++)
        {
          add_line (line_indices, base, indices, indices_type, i - 1, i, &pos);
          add_line (line_indices, base, indices, indices_type, i - 2, i, &pos);
        }
    }
    /* In the journal we are a bit sneaky and actually use GL_QUADS
     * which isn't actually a valid CoglVerticesMode! */
#ifdef HAVE_GL
  else if (mode == GL_QUADS && (n_vertices_in % 4) == 0)
    {
      for (i = 0; i < n_vertices_in; i += 4)
        {
          add_line (line_indices,
                    base, indices, indices_type, i,     i + 1, &pos);
          add_line (line_indices,
                    base, indices, indices_type, i + 1, i + 2, &pos);
          add_line (line_indices,
                    base, indices, indices_type, i + 2, i + 3, &pos);
          add_line (line_indices,
                    base, indices, indices_type, i + 3, i,     &pos);
        }
    }
#endif

  if (user_indices)
    cogl_buffer_unmap (COGL_BUFFER (index_buffer));

  *n_indices = n_lines * 2;

  ret = cogl_indices_new (ctx,
                          COGL_INDICES_TYPE_UNSIGNED_INT,
                          line_indices,
                          *n_indices);

  g_free (line_indices);

  return ret;
}

static void
pipeline_destroyed_cb (CoglPipeline *weak_pipeline, void *user_data)
{
  CoglPipeline *original_pipeline = user_data;

  /* XXX: I think we probably need to provide a custom unref function for
   * CoglPipeline because it's possible that we will reach this callback
   * because original_pipeline is being freed which means g_object_unref
   * will have already freed any associated user data.
   *
   * Setting more user data here will *probably* succeed but that may allocate
   * a new user-data array which could be leaked.
   *
   * Potentially we could have a _cogl_object_free_user_data function so
   * that a custom unref function could be written that can destroy weak
   * pipeline children before removing user data.
   */
  g_object_set_qdata_full (G_OBJECT (original_pipeline),
                           wire_pipeline_key, NULL, NULL);

  g_object_unref (weak_pipeline);
}

static void
draw_wireframe (CoglContext *ctx,
                CoglFramebuffer *framebuffer,
                CoglPipeline *pipeline,
                CoglVerticesMode mode,
                int first_vertex,
                int n_vertices,
                CoglAttribute **attributes,
                int n_attributes,
                CoglIndices *indices,
                CoglDrawFlags flags)
{
  CoglIndices *wire_indices;
  CoglPipeline *wire_pipeline;
  int n_indices;
  wire_pipeline_key = g_quark_from_static_string ("framebuffer-wire-pipeline-key");
  wire_indices = get_wire_line_indices (ctx,
                                        mode,
                                        first_vertex,
                                        n_vertices,
                                        indices,
                                        &n_indices);

  wire_pipeline = g_object_get_qdata (G_OBJECT (pipeline),
                                      wire_pipeline_key);

  if (!wire_pipeline)
    {
      static CoglSnippet *snippet = NULL;

      wire_pipeline =
        _cogl_pipeline_weak_copy (pipeline, pipeline_destroyed_cb, NULL);

      g_object_set_qdata_full (G_OBJECT (pipeline),
                               wire_pipeline_key, wire_pipeline,
                               NULL);

      /* If we have glsl then the pipeline may have an associated
       * vertex program and since we'd like to see the results of the
       * vertex program in the wireframe we just add a final clobber
       * of the wire color leaving the rest of the state untouched. */

      /* The snippet is cached so that it will reuse the program
       * from the pipeline cache if possible */
      if (snippet == NULL)
        {
          snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                      NULL,
                                      NULL);
          cogl_snippet_set_replace (snippet,
                                    "cogl_color_out = "
                                    "vec4 (0.0, 1.0, 0.0, 1.0);\n");
        }

      cogl_pipeline_add_snippet (wire_pipeline, snippet);
    }

  /* temporarily disable the wireframe to avoid recursion! */
  flags |= COGL_DRAW_SKIP_DEBUG_WIREFRAME;
  _cogl_framebuffer_draw_indexed_attributes (
                                           framebuffer,
                                           wire_pipeline,
                                           COGL_VERTICES_MODE_LINES,
                                           0,
                                           n_indices,
                                           wire_indices,
                                           attributes,
                                           n_attributes,
                                           flags);
  COGL_DEBUG_SET_FLAG (COGL_DEBUG_WIREFRAME);

  g_object_unref (wire_indices);
}
#endif

/* This can be called directly by the CoglJournal to draw attributes
 * skipping the implicit journal flush, the framebuffer flush and
 * pipeline validation. */
void
_cogl_framebuffer_draw_attributes (CoglFramebuffer *framebuffer,
                                   CoglPipeline *pipeline,
                                   CoglVerticesMode mode,
                                   int first_vertex,
                                   int n_vertices,
                                   CoglAttribute **attributes,
                                   int n_attributes,
                                   CoglDrawFlags flags)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

#ifdef COGL_ENABLE_DEBUG
  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_WIREFRAME) &&
                  (flags & COGL_DRAW_SKIP_DEBUG_WIREFRAME) == 0) &&
      mode != COGL_VERTICES_MODE_LINES &&
      mode != COGL_VERTICES_MODE_LINE_LOOP &&
      mode != COGL_VERTICES_MODE_LINE_STRIP)
    draw_wireframe (priv->context,
                    framebuffer, pipeline,
                    mode, first_vertex, n_vertices,
                    attributes, n_attributes, NULL,
                    flags);
  else
#endif
    {
      cogl_framebuffer_driver_draw_attributes (priv->driver,
                                               pipeline,
                                               mode,
                                               first_vertex,
                                               n_vertices,
                                               attributes,
                                               n_attributes,
                                               flags);
    }
}

void
_cogl_framebuffer_draw_indexed_attributes (CoglFramebuffer *framebuffer,
                                           CoglPipeline *pipeline,
                                           CoglVerticesMode mode,
                                           int first_vertex,
                                           int n_vertices,
                                           CoglIndices *indices,
                                           CoglAttribute **attributes,
                                           int n_attributes,
                                           CoglDrawFlags flags)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

#ifdef COGL_ENABLE_DEBUG
  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_WIREFRAME) &&
                  (flags & COGL_DRAW_SKIP_DEBUG_WIREFRAME) == 0) &&
      mode != COGL_VERTICES_MODE_LINES &&
      mode != COGL_VERTICES_MODE_LINE_LOOP &&
      mode != COGL_VERTICES_MODE_LINE_STRIP)
    draw_wireframe (priv->context,
                    framebuffer, pipeline,
                    mode, first_vertex, n_vertices,
                    attributes, n_attributes, indices,
                    flags);
  else
#endif
    {
      cogl_framebuffer_driver_draw_indexed_attributes (priv->driver,
                                                       pipeline,
                                                       mode,
                                                       first_vertex,
                                                       n_vertices,
                                                       indices,
                                                       attributes,
                                                       n_attributes,
                                                       flags);
    }
}

void
cogl_framebuffer_draw_rectangle (CoglFramebuffer *framebuffer,
                                 CoglPipeline *pipeline,
                                 float x_1,
                                 float y_1,
                                 float x_2,
                                 float y_2)
{
  const float position[4] = {x_1, y_1, x_2, y_2};
  CoglMultiTexturedRect rect;

  /* XXX: All the _*_rectangle* APIs normalize their input into an array of
   * _CoglMultiTexturedRect rectangles and pass these on to our work horse;
   * _cogl_framebuffer_draw_multitextured_rectangles.
   */

  rect.position = position;
  rect.tex_coords = NULL;
  rect.tex_coords_len = 0;

  _cogl_framebuffer_draw_multitextured_rectangles (framebuffer,
                                                   pipeline,
                                                   &rect,
                                                   1);
}

void
cogl_framebuffer_draw_textured_rectangle (CoglFramebuffer *framebuffer,
                                          CoglPipeline *pipeline,
                                          float x_1,
                                          float y_1,
                                          float x_2,
                                          float y_2,
                                          float s_1,
                                          float t_1,
                                          float s_2,
                                          float t_2)
{
  const float position[4] = {x_1, y_1, x_2, y_2};
  const float tex_coords[4] = {s_1, t_1, s_2, t_2};
  CoglMultiTexturedRect rect;

  /* XXX: All the _*_rectangle* APIs normalize their input into an array of
   * CoglMultiTexturedRect rectangles and pass these on to our work horse;
   * _cogl_framebuffer_draw_multitextured_rectangles.
   */

  rect.position = position;
  rect.tex_coords = tex_coords;
  rect.tex_coords_len = 4;

  _cogl_framebuffer_draw_multitextured_rectangles (framebuffer,
                                                   pipeline,
                                                   &rect,
                                                   1);
}

void
cogl_framebuffer_draw_multitextured_rectangle (CoglFramebuffer *framebuffer,
                                               CoglPipeline *pipeline,
                                               float x_1,
                                               float y_1,
                                               float x_2,
                                               float y_2,
                                               const float *tex_coords,
                                               int tex_coords_len)
{
  const float position[4] = {x_1, y_1, x_2, y_2};
  CoglMultiTexturedRect rect;

  /* XXX: All the _*_rectangle* APIs normalize their input into an array of
   * CoglMultiTexturedRect rectangles and pass these on to our work horse;
   * _cogl_framebuffer_draw_multitextured_rectangles.
   */

  rect.position = position;
  rect.tex_coords = tex_coords;
  rect.tex_coords_len = tex_coords_len;

  _cogl_framebuffer_draw_multitextured_rectangles (framebuffer,
                                                   pipeline,
                                                   &rect,
                                                   1);
}

void
cogl_framebuffer_draw_rectangles (CoglFramebuffer *framebuffer,
                                  CoglPipeline *pipeline,
                                  const float *coordinates,
                                  unsigned int n_rectangles)
{
  CoglMultiTexturedRect *rects;
  int i;

  /* XXX: All the _*_rectangle* APIs normalize their input into an array of
   * CoglMultiTexturedRect rectangles and pass these on to our work horse;
   * _cogl_framebuffer_draw_multitextured_rectangles.
   */

  rects = g_alloca (n_rectangles * sizeof (CoglMultiTexturedRect));

  for (i = 0; i < n_rectangles; i++)
    {
      rects[i].position = &coordinates[i * 4];
      rects[i].tex_coords = NULL;
      rects[i].tex_coords_len = 0;
    }

  _cogl_framebuffer_draw_multitextured_rectangles (framebuffer,
                                                   pipeline,
                                                   rects,
                                                   n_rectangles);
}

void
cogl_framebuffer_draw_textured_rectangles (CoglFramebuffer *framebuffer,
                                           CoglPipeline *pipeline,
                                           const float *coordinates,
                                           unsigned int n_rectangles)
{
  CoglMultiTexturedRect *rects;
  int i;

  /* XXX: All the _*_rectangle* APIs normalize their input into an array of
   * _CoglMultiTexturedRect rectangles and pass these on to our work horse;
   * _cogl_framebuffer_draw_multitextured_rectangles.
   */

  rects = g_alloca (n_rectangles * sizeof (CoglMultiTexturedRect));

  for (i = 0; i < n_rectangles; i++)
    {
      rects[i].position = &coordinates[i * 8];
      rects[i].tex_coords = &coordinates[i * 8 + 4];
      rects[i].tex_coords_len = 4;
    }

  _cogl_framebuffer_draw_multitextured_rectangles (framebuffer,
                                                   pipeline,
                                                   rects,
                                                   n_rectangles);
}

CoglFramebufferDriver *
cogl_framebuffer_get_driver (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);

  return priv->driver;
}

CoglTimestampQuery *
cogl_framebuffer_create_timestamp_query (CoglFramebuffer *framebuffer)
{
  CoglFramebufferPrivate *priv =
    cogl_framebuffer_get_instance_private (framebuffer);
  const CoglDriverVtable *driver_vtable = priv->context->driver_vtable;

  g_return_val_if_fail (cogl_has_feature (priv->context,
                                          COGL_FEATURE_ID_TIMESTAMP_QUERY),
                        NULL);

  /* The timestamp query completes upon completion of all previously submitted
   * GL commands. So make sure those commands are indeed submitted by flushing
   * the journal.
   */
  _cogl_framebuffer_flush_journal (framebuffer);

  cogl_context_flush_framebuffer_state (priv->context,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  return driver_vtable->create_timestamp_query (priv->context);
}
