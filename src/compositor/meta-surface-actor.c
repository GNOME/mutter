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
#include <clutter/clutter.h>
#include <cogl/cogl-wayland-server.h>
#include <cogl/cogl-texture-pixmap-x11.h>
#include <meta/meta-shaped-texture.h>
#include "meta-surface-actor.h"
#include "meta-wayland-private.h"
#include "meta-cullable.h"

#include "meta-shaped-texture-private.h"

struct _MetaSurfaceActorPrivate
{
  MetaShapedTexture *texture;
  MetaWaylandBuffer *buffer;

  GSList *ops;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaSurfaceActor, meta_surface_actor, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

static void meta_surface_actor_free_ops (MetaSurfaceActor *self);

static void
meta_surface_actor_dispose (GObject *object)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (object);

  meta_surface_actor_free_ops (self);
}

static void
meta_surface_actor_class_init (MetaSurfaceActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_surface_actor_dispose;

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
meta_surface_actor_init (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   META_TYPE_SURFACE_ACTOR,
                                                   MetaSurfaceActorPrivate);

  priv->texture = META_SHAPED_TEXTURE (meta_shaped_texture_new ());
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
update_area (MetaSurfaceActor *self,
             int x, int y, int width, int height)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  if (meta_is_wayland_compositor ())
    {
      struct wl_resource *resource = priv->buffer->resource;
      struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get (resource);

      if (shm_buffer)
        {
          CoglTexture2D *texture = COGL_TEXTURE_2D (priv->buffer->texture);
          cogl_wayland_texture_set_region_from_shm_buffer (texture, x, y, width, height, shm_buffer, x, y, 0, NULL);
        }
    }
  else
    {
      CoglTexturePixmapX11 *texture = COGL_TEXTURE_PIXMAP_X11 (meta_shaped_texture_get_texture (priv->texture));
      cogl_texture_pixmap_x11_update_area (texture, x, y, width, height);
    }
}

gboolean
meta_surface_actor_damage_all (MetaSurfaceActor *self,
                               cairo_region_t   *unobscured_region)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  CoglTexture *texture = meta_shaped_texture_get_texture (priv->texture);

  update_area (self, 0, 0, cogl_texture_get_width (texture), cogl_texture_get_height (texture));
  return meta_shaped_texture_update_area (priv->texture,
                                          0, 0,
                                          cogl_texture_get_width (texture),
                                          cogl_texture_get_height (texture),
                                          unobscured_region);
}

gboolean
meta_surface_actor_damage_area (MetaSurfaceActor *self,
                                int               x,
                                int               y,
                                int               width,
                                int               height,
                                cairo_region_t   *unobscured_region)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  update_area (self, x, y, width, height);
  return meta_shaped_texture_update_area (priv->texture,
                                          x, y, width, height,
                                          unobscured_region);
}

void
meta_surface_actor_attach_wayland_buffer (MetaSurfaceActor *self,
                                          MetaWaylandBuffer *buffer)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  priv->buffer = buffer;

  if (buffer)
    meta_shaped_texture_set_texture (priv->texture, buffer->texture);
  else
    meta_shaped_texture_set_texture (priv->texture, NULL);
}

void
meta_surface_actor_set_texture (MetaSurfaceActor *self,
                                CoglTexture      *texture)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  meta_shaped_texture_set_texture (priv->texture, texture);
}

void
meta_surface_actor_set_input_region (MetaSurfaceActor *self,
                                     cairo_region_t   *region)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  meta_shaped_texture_set_input_shape_region (priv->texture, region);
}

void
meta_surface_actor_set_opaque_region (MetaSurfaceActor *self,
                                      cairo_region_t   *region)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  meta_shaped_texture_set_opaque_region (priv->texture, region);
}

MetaSurfaceActor *
meta_surface_actor_new (void)
{
  return g_object_new (META_TYPE_SURFACE_ACTOR, NULL);
}

typedef enum {
  OP_SET_POSITION,
  OP_PLACE_ABOVE,
  OP_PLACE_BELOW,
} MetaSurfaceActorOpType;

typedef struct {
  MetaSurfaceActorOpType type;
} MetaSurfaceActorOp;

typedef struct {
  MetaSurfaceActorOpType type;
  ClutterActor *subsurface;
  int32_t x;
  int32_t y;
} MetaSurfaceActorOp_SetPosition;

typedef struct {
  MetaSurfaceActorOpType type;
  ClutterActor *subsurface;
  ClutterActor *sibling;
} MetaSurfaceActorOp_Stack;

void
meta_surface_actor_subsurface_set_position (MetaSurfaceActor *self,
                                            MetaSurfaceActor *subsurface,
                                            int32_t           x,
                                            int32_t           y)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  MetaSurfaceActorOp_SetPosition *op = g_slice_new0 (MetaSurfaceActorOp_SetPosition);

  op->type = OP_SET_POSITION;
  op->subsurface = CLUTTER_ACTOR (subsurface);
  op->x = x;
  op->y = y;

  priv->ops = g_slist_append (priv->ops, op);
}

static void
meta_surface_actor_stack_op (MetaSurfaceActor       *self,
                             MetaSurfaceActorOpType  type,
                             MetaSurfaceActor       *subsurface,
                             MetaSurfaceActor       *sibling)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  MetaSurfaceActorOp_Stack *op = g_slice_new0 (MetaSurfaceActorOp_Stack);

  op->type = type;
  op->subsurface = CLUTTER_ACTOR (subsurface);
  op->sibling = CLUTTER_ACTOR (sibling);

  priv->ops = g_slist_append (priv->ops, op);
}

void
meta_surface_actor_subsurface_place_above (MetaSurfaceActor *self,
                                           MetaSurfaceActor *subsurface,
                                           MetaSurfaceActor *sibling)
{
  meta_surface_actor_stack_op (self, OP_PLACE_ABOVE, subsurface, sibling);
}

void
meta_surface_actor_subsurface_place_below (MetaSurfaceActor *self,
                                           MetaSurfaceActor *subsurface,
                                           MetaSurfaceActor *sibling)
{
  meta_surface_actor_stack_op (self, OP_PLACE_BELOW, subsurface, sibling);
}

static void
meta_surface_actor_do_op (MetaSurfaceActor   *self,
                          MetaSurfaceActorOp *op)
{
  MetaSurfaceActorOp_SetPosition *op_pos = (MetaSurfaceActorOp_SetPosition *) op;
  MetaSurfaceActorOp_Stack *op_stack = (MetaSurfaceActorOp_Stack *) op;

  switch (op->type)
    {
    case OP_SET_POSITION:
      clutter_actor_set_position (op_pos->subsurface, op_pos->x, op_pos->y);
      break;
    case OP_PLACE_ABOVE:
      clutter_actor_set_child_above_sibling (CLUTTER_ACTOR (self), op_stack->subsurface, op_stack->sibling);
      break;
    case OP_PLACE_BELOW:
      clutter_actor_set_child_below_sibling (CLUTTER_ACTOR (self), op_stack->subsurface, op_stack->sibling);
      break;
    }
}

static void
meta_surface_actor_op_free (MetaSurfaceActorOp *op)
{
  g_slice_free (MetaSurfaceActorOp, op);
}

static void
meta_surface_actor_free_ops (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv = self->priv;

  g_slist_free_full (priv->ops, (GDestroyNotify) meta_surface_actor_op_free);
  priv->ops = NULL;
}

static void
meta_surface_actor_do_ops (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv = self->priv;
  GSList *l;

  for (l = priv->ops; l; l = l->next)
    meta_surface_actor_do_op (self, ((MetaSurfaceActorOp *) l->data));
}

void
meta_surface_actor_commit (MetaSurfaceActor *self)
{
  meta_surface_actor_do_ops (self);
}
