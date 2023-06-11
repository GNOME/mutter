/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include "meta/meta-background-content.h"

cairo_region_t *meta_background_content_get_clip_region (MetaBackgroundContent *self);

void meta_background_content_cull_unobscured (MetaBackgroundContent *self,
                                              cairo_region_t        *unobscured_region);

void meta_background_content_cull_redraw_clip (MetaBackgroundContent *self,
                                               cairo_region_t        *clip_region);
