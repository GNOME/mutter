/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_CONTENT_PRIVATE_H
#define META_BACKGROUND_CONTENT_PRIVATE_H

#include "meta/meta-background-content.h"

cairo_region_t *meta_background_content_get_clip_region (MetaBackgroundContent *self);

void meta_background_content_cull_out (MetaBackgroundContent *self,
                                       cairo_region_t        *unobscured_region,
                                       cairo_region_t        *clip_region);

void meta_background_content_reset_culling (MetaBackgroundContent *self);

#endif /* META_BACKGROUND_CONTENT_PRIVATE_H */
