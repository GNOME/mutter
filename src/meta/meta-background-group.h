/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_GROUP_H
#define META_BACKGROUND_GROUP_H

#include "clutter/clutter.h"

#define META_TYPE_BACKGROUND_GROUP (meta_background_group_get_type ())
G_DECLARE_FINAL_TYPE (MetaBackgroundGroup,
                      meta_background_group,
                      META, BACKGROUND_GROUP,
                      ClutterActor)

ClutterActor *meta_background_group_new (void);

#endif /* META_BACKGROUND_GROUP_H */
