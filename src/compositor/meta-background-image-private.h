/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include "clutter/clutter.h"
#include "meta/meta-background-image.h"

ClutterColorState * meta_background_image_get_color_state (MetaBackgroundImage *self,
                                                           ClutterContext      *ctx);
