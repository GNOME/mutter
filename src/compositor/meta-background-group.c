/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * MetaBackgroundGroup:
 *
 * Container for background actors
 *
 * This class is a subclass of ClutterActor with special handling for
 * MetaBackgroundActor/MetaBackgroundGroup when painting children.
 * It makes sure to only draw the parts of the backgrounds not
 * occluded by opaque windows.
 *
 * See #MetaWindowGroup for more information behind the motivation,
 * and details on implementation.
 */

#include "config.h"

#include "compositor/meta-cullable.h"
#include "meta/meta-background-group.h"

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackgroundGroup, meta_background_group, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

static void
meta_background_group_class_init (MetaBackgroundGroupClass *klass)
{
}

static void
meta_background_group_cull_unobscured (MetaCullable *cullable,
                                       MtkRegion    *unobscured_region)
{
  meta_cullable_cull_unobscured_children (cullable, unobscured_region);
}

static void
meta_background_group_cull_redraw_clip (MetaCullable *cullable,
                                        MtkRegion    *clip_region)
{
  meta_cullable_cull_redraw_clip_children (cullable, clip_region);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_unobscured = meta_background_group_cull_unobscured;
  iface->cull_redraw_clip = meta_background_group_cull_redraw_clip;
}

static void
meta_background_group_init (MetaBackgroundGroup *self)
{
}

ClutterActor *
meta_background_group_new (void)
{
  MetaBackgroundGroup *background_group;

  background_group = g_object_new (META_TYPE_BACKGROUND_GROUP, NULL);

  return CLUTTER_ACTOR (background_group);
}
