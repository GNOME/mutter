/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include "cogl/cogl.h"
#include "meta/meta-background.h"

CoglTexture * meta_background_get_texture (MetaBackground         *self,
                                           int                     monitor_index,
                                           MtkRectangle           *texture_area,
                                           CoglPipelineWrapMode   *wrap_mode);

ClutterColorState * meta_background_get_color_state (MetaBackground *self);
