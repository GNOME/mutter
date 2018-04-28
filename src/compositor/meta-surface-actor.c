/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-surface-actor
 * @title: MetaSurfaceActor
 * @short_description: An actor representing a surface in the scene graph
 *
 * A surface can be either a shaped texture, or a group of shaped texture,
 * used to draw the content of a window.
 */

#include <config.h>

#include "meta-surface-actor.h"

#include <clutter/clutter.h>
#include <meta/meta-shaped-texture.h>
#include "meta-cullable.h"
#include "meta-shaped-texture-private.h"

struct _MetaSurfaceActorPrivate
{
  MetaShapedTexture *texture;

  cairo_region_t *input_region;

  /* Freeze/thaw accounting */
  cairo_region_t *pending_damage;
  guint frozen : 1;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaSurfaceActor, meta_surface_actor, CLUTTER_TYPE_ACTOR,
                                  G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

enum {
  REPAINT_SCHEDULED,
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

static void
meta_surface_actor_pick (ClutterActor       *actor,
                         const ClutterColor *color)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (actor);
  MetaSurfaceActorPrivate *priv = self->priv;
  ClutterActorIter iter;
  ClutterActor *child;

  if (!clutter_actor_should_pick_paint (actor))
    return;

  /* If there is no region then use the regular pick */
  if (priv->input_region == NULL)
    CLUTTER_ACTOR_CLASS (meta_surface_actor_parent_class)->pick (actor, color);
  else
    {
      int n_rects;
      float *rectangles;
      int i;
      CoglPipeline *pipeline;
      CoglContext *ctx;
      CoglFramebuffer *fb;
      CoglColor cogl_color;

      n_rects = cairo_region_num_rectangles (priv->input_region);
      rectangles = g_alloca (sizeof (float) * 4 * n_rects);

      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          int pos = i * 4;

          cairo_region_get_rectangle (priv->input_region, i, &rect);

          rectangles[pos + 0] = rect.x;
          rectangles[pos + 1] = rect.y;
          rectangles[pos + 2] = rect.x + rect.width;
          rectangles[pos + 3] = rect.y + rect.height;
        }

      ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
      fb = cogl_get_draw_framebuffer ();

      cogl_color_init_from_4ub (&cogl_color, color->red, color->green, color->blue, color->alpha);

      pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color (pipeline, &cogl_color);
      cogl_framebuffer_draw_rectangles (fb, pipeline, rectangles, n_rects);
      cogl_object_unref (pipeline);
    }

  clutter_actor_iter_init (&iter, actor);

  while (clutter_actor_iter_next (&iter, &child))
    clutter_actor_paint (child);
}

static gboolean
meta_surface_actor_get_paint_volume (ClutterActor       *actor,
                                     ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}

static void
meta_surface_actor_dispose (GObject *object)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (object);
  MetaSurfaceActorPrivate *priv = self->priv;

  g_clear_pointer (&priv->input_region, cairo_region_destroy);

  G_OBJECT_CLASS (meta_surface_actor_parent_class)->dispose (object);
}

static void
meta_surface_actor_class_init (MetaSurfaceActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  object_class->dispose = meta_surface_actor_dispose;
  actor_class->pick = meta_surface_actor_pick;
  actor_class->get_paint_volume = meta_surface_actor_get_paint_volume;

  signals[REPAINT_SCHEDULED] = g_signal_new ("repaint-scheduled",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST,
                                             0,
                                             NULL, NULL, NULL,
                                             G_TYPE_NONE, 0);

  signals[SIZE_CHANGED] = g_signal_new ("size-changed",
                                        G_TYPE_FROM_CLASS (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);

  g_type_class_add_private (klass, sizeof (MetaSurfaceActorPrivate));
}

static void
meta_surface_actor_cull_out (MetaCullable   *cullable,
                             cairo_region_t *unobscured_region,
                             cairo_region_t *clip_region)
{
  meta_cullable_cull_out_children (cullable, unobscured_region, clip_region);
}

static void
meta_surface_actor_reset_culling (MetaCullable *cullable)
{
  meta_cullable_reset_culling_children (cullable);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_surface_actor_cull_out;
  iface->reset_culling = meta_surface_actor_reset_culling;
}

static void
texture_size_changed (MetaShapedTexture *texture,
                      gpointer           user_data)
{
  MetaSurfaceActor *actor = META_SURFACE_ACTOR (user_data);
  g_signal_emit (actor, signals[SIZE_CHANGED], 0);
}

static void
meta_surface_actor_init (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   META_TYPE_SURFACE_ACTOR,
                                                   MetaSurfaceActorPrivate);

  priv->texture = META_SHAPED_TEXTURE (meta_shaped_texture_new ());
  g_signal_connect_object (priv->texture, "size-changed",
                           G_CALLBACK (texture_size_changed), self, 0);
  clutter_actor_add_child (CLUTTER_ACTOR (self), CLUTTER_ACTOR (priv->texture));
}

cairo_surface_t *
meta_surface_actor_get_image (MetaSurfaceActor      *self,
                              cairo_rectangle_int_t *clip)
{
  return meta_shaped_texture_get_image (self->priv->texture, clip);
}

MetaShapedTexture *
meta_surface_actor_get_texture (MetaSurfaceActor *self)
{
  return self->priv->texture;
}

static void
meta_surface_actor_update_area (MetaSurfaceActor *self,
                                int x, int y, int width, int height)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  if (meta_shaped_texture_update_area (priv->texture, x, y, width, height))
    g_signal_emit (self, signals[REPAINT_SCHEDULED], 0);
}

gboolean
meta_surface_actor_is_obscured (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  return meta_shaped_texture_is_obscured (priv->texture);
}

void
meta_surface_actor_set_input_region (MetaSurfaceActor *self,
                                     cairo_region_t   *region)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  if (priv->input_region)
    cairo_region_destroy (priv->input_region);

  if (region)
    priv->input_region = cairo_region_reference (region);
  else
    priv->input_region = NULL;
}

void
meta_surface_actor_set_opaque_region (MetaSurfaceActor *self,
                                      cairo_region_t   *region)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  meta_shaped_texture_set_opaque_region (priv->texture, region);
}

cairo_region_t *
meta_surface_actor_get_opaque_region (MetaSurfaceActor *actor)
{
  MetaSurfaceActorPrivate *priv = actor->priv;
  return meta_shaped_texture_get_opaque_region (priv->texture);
}

static gboolean
is_frozen (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  return priv->frozen;
}

void
meta_surface_actor_process_damage (MetaSurfaceActor *self,
                                   int x, int y, int width, int height)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  if (is_frozen (self))
    {
      /* The window is frozen due to an effect in progress: we ignore damage
       * here on the off chance that this will stop the corresponding
       * texture_from_pixmap from being update.
       *
       * pending_damage tracks any damage that happened while the window was
       * frozen so that when can apply it when the window becomes unfrozen.
       *
       * It should be noted that this is an unreliable mechanism since it's
       * quite likely that drivers will aim to provide a zero-copy
       * implementation of the texture_from_pixmap extension and in those cases
       * any drawing done to the window is always immediately reflected in the
       * texture regardless of damage event handling.
       */
      cairo_rectangle_int_t rect = { .x = x, .y = y, .width = width, .height = height };

      if (!priv->pending_damage)
        priv->pending_damage = cairo_region_create_rectangle (&rect);
      else
        cairo_region_union_rectangle (priv->pending_damage, &rect);
      return;
    }

  META_SURFACE_ACTOR_GET_CLASS (self)->process_damage (self, x, y, width, height);

  if (meta_surface_actor_is_visible (self))
    meta_surface_actor_update_area (self, x, y, width, height);
}

void
meta_surface_actor_pre_paint (MetaSurfaceActor *self)
{
  META_SURFACE_ACTOR_GET_CLASS (self)->pre_paint (self);
}

gboolean
meta_surface_actor_is_argb32 (MetaSurfaceActor *self)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (self);
  CoglTexture *texture = meta_shaped_texture_get_texture (stex);

  /* If we don't have a texture, like during initialization, assume
   * that we're ARGB32.
   *
   * If we are unredirected and we have no texture assume that we are
   * not ARGB32 otherwise we wouldn't be unredirected in the first
   * place. This prevents us from continually redirecting and
   * unredirecting on every paint.
   */
  if (!texture)
    return !meta_surface_actor_is_unredirected (self);

  switch (cogl_texture_get_components (texture))
    {
    case COGL_TEXTURE_COMPONENTS_A:
    case COGL_TEXTURE_COMPONENTS_RGBA:
      return TRUE;
    case COGL_TEXTURE_COMPONENTS_RG:
    case COGL_TEXTURE_COMPONENTS_RGB:
    case COGL_TEXTURE_COMPONENTS_DEPTH:
      return FALSE;
    default:
      g_assert_not_reached ();
    }
}

gboolean
meta_surface_actor_is_visible (MetaSurfaceActor *self)
{
  return META_SURFACE_ACTOR_GET_CLASS (self)->is_visible (self);
}

void
meta_surface_actor_set_frozen (MetaSurfaceActor *self,
                               gboolean          frozen)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  priv->frozen = frozen;

  if (!frozen && priv->pending_damage)
    {
      int i, n_rects = cairo_region_num_rectangles (priv->pending_damage);
      cairo_rectangle_int_t rect;

      /* Since we ignore damage events while a window is frozen for certain effects
       * we need to apply the tracked damage now. */

      for (i = 0; i < n_rects; i++)
        {
          cairo_region_get_rectangle (priv->pending_damage, i, &rect);
          meta_surface_actor_process_damage (self, rect.x, rect.y,
                                             rect.width, rect.height);
        }
      g_clear_pointer (&priv->pending_damage, cairo_region_destroy);
    }
}

gboolean
meta_surface_actor_should_unredirect (MetaSurfaceActor *self)
{
  return META_SURFACE_ACTOR_GET_CLASS (self)->should_unredirect (self);
}

void
meta_surface_actor_set_unredirected (MetaSurfaceActor *self,
                                     gboolean          unredirected)
{
  META_SURFACE_ACTOR_GET_CLASS (self)->set_unredirected (self, unredirected);
}

gboolean
meta_surface_actor_is_unredirected (MetaSurfaceActor *self)
{
  return META_SURFACE_ACTOR_GET_CLASS (self)->is_unredirected (self);
}

MetaWindow *
meta_surface_actor_get_window (MetaSurfaceActor *self)
{
  return META_SURFACE_ACTOR_GET_CLASS (self)->get_window (self);
}
